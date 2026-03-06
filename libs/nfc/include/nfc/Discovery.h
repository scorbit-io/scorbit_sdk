// --------------------------------------------------------------------
//  Project:           Scorbitron SLB
//  Description:       Base class for network detection
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2026-02-27	| Initial Release
// --------------------------------------------------------------------


#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#else
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <string_view>
#include "Util.h"

class NetworkDiscovery
{
    private:
    static constexpr uint16_t kDiscoveryPort = 37020;

    static constexpr uint32_t kMagic = 0x4E445343; // NDSC - Network Discovery
    static constexpr uint16_t kVersion = 1;
    static constexpr uint16_t kPacketTypeDiscover = 1;
    static constexpr uint16_t kPacketTypeResponse = 2;

    #ifdef _WIN32
    using SocketHandle = SOCKET;
    using SocketLength = int;
    using SocketRecvResult = int;
    static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
    #else
    using SocketHandle = int;
    using SocketLength = socklen_t;
    using SocketRecvResult = ssize_t;
    static constexpr SocketHandle kInvalidSocket = -1;
    #endif

    using IPv4Address = uint32_t;

    #pragma pack(push, 1)
    // Packet that is sent as a UDP broadcast
    // to discover devices on the network
    struct DiscoveryRequestPacket
    {
        uint32_t Magic;
        uint16_t Version;
        uint16_t Type;
        uint32_t RequestId;
    };

    // Response packet to a discovery broadcast
    struct DiscoveryResponsePacket
    {
        uint32_t Magic;
        uint16_t Version;
        uint16_t Type;
        uint32_t RequestId;
        uint8_t Data[1024];
    };
    #pragma pack(pop)

    public:
    NetworkDiscovery() : m_Running(false), m_ListenSocket(kInvalidSocket)
    {
    }

    ~NetworkDiscovery()
    {
        UnInitialize();
    }

    NetworkDiscovery(const NetworkDiscovery&) = delete;
    NetworkDiscovery& operator=(const NetworkDiscovery&) = delete;

    class DeviceInfo_t
    {
        public:
		std::string GameName;           // "Iron Maiden LE - Legacy of the Beast"
        std::string ProviderInfo;       // "scorbitd 0.4.12"
        uint64_t    ProviderSerial = 0; // 21950
		std::string ExtraInfo;          // Anything. For example, the result of '--spike infos'
		std::string IpAddress;          // This field is filled by the discovery system
    };

    bool Initialize(const DeviceInfo_t &DeviceInfo)
    { 
        // Make sure sockets are available
        if (!EnsureSocketsReady()) return false;
        // UnInitialize the class if necessary
        UnInitialize();
        // Initialize the class
        m_DeviceInfo = DeviceInfo;
        m_Running = true;
        m_Thread = std::thread(&NetworkDiscovery::ListenerThreadProc, this);
        return true;
    }
    void UnInitialize()
    {
        // stop the listening thread
        m_Running = false;
        if (m_Thread.joinable()) m_Thread.join();
        // Delete its socket
        SocketHandle socketFd = m_ListenSocket.exchange(kInvalidSocket);
        if (socketFd != kInvalidSocket) CloseSocket(socketFd);
    }

    static std::vector<DeviceInfo_t> Discover(int timeoutMs = 1000)
    {
        std::vector<DeviceInfo_t> devices;

        // Make sure sockets are available
        if (!EnsureSocketsReady()) return devices;

        // Create a socket for broadcasts
        SocketHandle sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == kInvalidSocket) return devices;
        SetIntSocketOption(sock, SOL_SOCKET, SO_BROADCAST, 1);
        SetIntSocketOption(sock, SOL_SOCKET, SO_REUSEADDR, 1);
        
        // Initialize and bind it
        sockaddr_in localAddr;
        std::memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(0);
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(sock, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) != 0)
        {
            CloseSocket(sock);
            return devices;
        }

        // Prepare the discovery packet
        DiscoveryRequestPacket request;
        std::memset(&request, 0, sizeof(request));
        request.Magic = htonl(kMagic);
        request.Version = htons(kVersion);
        request.Type = htons(kPacketTypeDiscover);

        // Generate a request id
        static std::atomic<uint32_t> counter(1);
        uint32_t now = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        request.RequestId = htonl(now ^ counter.fetch_add(1));

        // And send it on all broadcast addresses
        std::vector<IPv4Address> broadcastAddresses = GetBroadcastAddresses();
        for (IPv4Address broadcastIp : broadcastAddresses)
        {
            sockaddr_in destAddr;
            std::memset(&destAddr, 0, sizeof(destAddr));
            destAddr.sin_family = AF_INET;
            destAddr.sin_port = htons(kDiscoveryPort);
            destAddr.sin_addr.s_addr = broadcastIp;
            ::sendto(
                sock,
                reinterpret_cast<const char*>(&request),
                static_cast<int>(sizeof(request)),
                0,
                reinterpret_cast<sockaddr*>(&destAddr),
                sizeof(destAddr));
        }

        // Wait for devices to replay
        std::set<std::string> seenKeys;
        // Until timeout is reached
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            // Is timeout reached ?
            int remainingMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count());
            if (remainingMs <= 0) 
                break;

            // Wait for a reply
            int waitMs = (remainingMs < 100) ? remainingMs : 100;
            if (!WaitForReadable(sock, waitMs)) 
                continue;

            // Read packet
            uint8_t buffer[sizeof(DiscoveryResponsePacket)];
            sockaddr_in fromAddr;
            SocketLength fromLen = static_cast<SocketLength>(sizeof(fromAddr));
            std::memset(&fromAddr, 0, sizeof(fromAddr));
            SocketRecvResult received = ::recvfrom(
                sock,
                reinterpret_cast<char*>(buffer),
                static_cast<int>(sizeof(buffer)),
                0,
                reinterpret_cast<sockaddr*>(&fromAddr),
                &fromLen);
            if (received < 0)
            {
                int errorCode = GetLastSocketError();
                if (IsRetryableSocketError(errorCode)) 
                    continue;
                break;
            }

            // Packet has the correct length ?
            if (static_cast<size_t>(received) < sizeof(DiscoveryResponsePacket)) 
                continue;

            // Packet is correct ?
            DiscoveryResponsePacket response;
            std::memcpy(&response, buffer, sizeof(response));
            if ((ntohl(response.Magic) != kMagic) ||
                (ntohs(response.Version) != kVersion) ||
                (ntohs(response.Type) != kPacketTypeResponse) ||
                (response.RequestId != request.RequestId))
                    continue;

            // Extract device IP
            char ipBuffer[INET_ADDRSTRLEN]; std::memset(ipBuffer, 0, sizeof(ipBuffer));
            if (::inet_ntop(AF_INET, &fromAddr.sin_addr, ipBuffer, sizeof(ipBuffer)) == nullptr)
                continue;

            // Decode response info
            auto ReadNextZ = [](const uint8_t* buffer, size_t bufferSize, size_t& pos) -> std::string
                {
                    // Return empty if there is nothing left to read
                    if (pos >= bufferSize) return {};

                    // Treat a leading '\0' as end-of-list sentinel
                    if (buffer[pos] == 0) { pos++; return {}; }
					// Read a \0-terminated string from the buffer
                    const uint8_t* start = buffer + pos;
                    size_t remaining = bufferSize - pos;
                    const void* zero = std::memchr(start, 0, remaining);
                    if (zero == nullptr) return {}; // No terminator found within bounds
                    const uint8_t* end = static_cast<const uint8_t*>(zero);
                    // Move the position after it
                    size_t len = static_cast<size_t>(end - start);
                    pos += len + 1;
                    return std::string(reinterpret_cast<const char*>(start), len);
                };
            DeviceInfo_t info;
			// Insert the IP address in the device info (as a string)
            info.IpAddress = ipBuffer;
            // Read GameName, ProviderInfo, ProviderSerial and ExtraInfo in the response data
            size_t Pos = 0;
			info.GameName = ReadNextZ(response.Data, sizeof(response.Data), Pos);
			info.ProviderInfo = ReadNextZ(response.Data, sizeof(response.Data), Pos);
			info.ProviderSerial = Util::ReadUInt64LE(response.Data, sizeof(response.Data), Pos); Pos += 8;
			info.ExtraInfo = ReadNextZ(response.Data, sizeof(response.Data), Pos);

            // Store this device info in the list
            if (seenKeys.insert(info.IpAddress).second)
                devices.push_back(info);
        }

        CloseSocket(sock);
        return devices;
    }

    private:
    void ListenerThreadProc()
    {
        // Create the listening socket
        SocketHandle sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == kInvalidSocket) { m_Running = false; return; }

        // Initialize it
        SetIntSocketOption(sock, SOL_SOCKET, SO_REUSEADDR, 1);
        sockaddr_in localAddr;
        std::memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(kDiscoveryPort);
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        // Bind it
        if (::bind(sock, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) != 0)
        {
            CloseSocket(sock);
            m_Running = false;
            return;
        }

        m_ListenSocket = sock;
        std::random_device rd;
        std::mt19937 rng(rd());

        const int kMinResponseDelayMs = 5;
        const int kMaxResponseDelayMs = 50;

        // Loop untill this thread is stopped
        while (m_Running)
        {
            // Wait for a packet
            if (!WaitForReadable(sock, 200))
                continue;

            // Read packet
            uint8_t buffer[512];
            sockaddr_in fromAddr;
            SocketLength fromLen = static_cast<SocketLength>(sizeof(fromAddr));
            std::memset(&fromAddr, 0, sizeof(fromAddr));
            SocketRecvResult received = ::recvfrom(
                sock,
                reinterpret_cast<char*>(buffer),
                static_cast<int>(sizeof(buffer)),
                0,
                reinterpret_cast<sockaddr*>(&fromAddr),
                &fromLen);
            if (received < 0)
            {
                int errorCode = GetLastSocketError();

                if (!m_Running)
                    break;

                if (IsRetryableSocketError(errorCode))
                    continue;

                break;
            }

            // Pack is correct ?
            if (static_cast<size_t>(received) < sizeof(DiscoveryRequestPacket))
                continue;

            // Packet is correct ?
            DiscoveryRequestPacket request;
            std::memcpy(&request, buffer, sizeof(request));
            if ((ntohl(request.Magic) != kMagic) ||
                (ntohs(request.Version) != kVersion) ||
                (ntohs(request.Type) != kPacketTypeDiscover))
                    continue;

            // Prepare response info
            DiscoveryResponsePacket response;
            std::memset(&response, 0, sizeof(response));
            response.Magic = htonl(kMagic);
            response.Version = htons(kVersion);
            response.Type = htons(kPacketTypeResponse);
            response.RequestId = request.RequestId;
            auto AppendZ = [](uint8_t* buffer, size_t bufferSize, size_t& pos, std::string_view s) -> size_t
                {
                    // Only take first part of the string if it contains a \0, and ignore the rest
                    size_t z = s.find('\0');
                    if (z != std::string_view::npos) s = s.substr(0, z);
                    if (pos >= bufferSize) return 0;
                    // Copy the string in the buffer
                    size_t remaining = bufferSize - pos;
                    size_t toCopy = s.size();
                    if (toCopy > remaining - 1) toCopy = remaining - 1;
                    if (toCopy != 0) std::memcpy(buffer + pos, s.data(), toCopy);
                    // Add a \0 at the end of the string
                    buffer[pos + toCopy] = 0;
                    pos += toCopy + 1;
                    return toCopy;
                };
			// Write GameName, ProviderInfo, ProviderSerial and ExtraInfo in the response data
            size_t Pos = 0;
            AppendZ(response.Data, sizeof(response.Data), Pos, m_DeviceInfo.GameName);
            AppendZ(response.Data, sizeof(response.Data), Pos, m_DeviceInfo.ProviderInfo);
			Util::WriteLE(reinterpret_cast<uint8_t*>(response.Data), sizeof(response.Data), Pos, m_DeviceInfo.ProviderSerial); Pos += 8;
            AppendZ(response.Data, sizeof(response.Data), Pos, m_DeviceInfo.ExtraInfo);

            // Wait for a random delay to prevent collisions when all devices answer at the same time
            if (kMaxResponseDelayMs > 0)
            {
                std::uniform_int_distribution<int> dist(kMinResponseDelayMs, kMaxResponseDelayMs);
                int delayMs = dist(rng);
                int remainingMs = delayMs;
                while (m_Running && remainingMs > 0)
                {
                    int sliceMs = (remainingMs < 5) ? remainingMs : 5;
                    std::this_thread::sleep_for(std::chrono::milliseconds(sliceMs));
                    remainingMs -= sliceMs;
                }
            }
            if (!m_Running) break;

            // Send reply
            char ipBuffer[INET_ADDRSTRLEN]; std::memset(ipBuffer, 0, sizeof(ipBuffer));
            if (::inet_ntop(AF_INET, &fromAddr.sin_addr, ipBuffer, sizeof(ipBuffer)) == nullptr)
                std::snprintf(ipBuffer, sizeof(ipBuffer), "???");
            INF("Received a valid discovery request from %s\n", ipBuffer);
            ::sendto(
                sock,
                reinterpret_cast<const char*>(&response),
                static_cast<int>(sizeof(response)),
                0,
                reinterpret_cast<sockaddr*>(&fromAddr),
                sizeof(fromAddr));
        }

        SocketHandle socketFd = m_ListenSocket.exchange(kInvalidSocket);
        if (socketFd != kInvalidSocket)
            CloseSocket(socketFd);

        m_Running = false;
    }

    static bool EnsureSocketsReady()
    {
        #ifdef _WIN32
        // For Windows, we need the Socket system to be initialized
        static std::once_flag once;
        static bool ready = false;

        std::call_once(
            once,
            []()
            {
                WSADATA wsaData;
                ready = (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);

                if (ready)
                {
                    std::atexit(
                        []()
                        {
                            ::WSACleanup();
                        });
                }
            });

        return ready;
        #else
        return true;
        #endif
    }

    static std::string FixedBufferToString(const char* buffer, size_t size)
    {
        size_t length = 0;
        while (length < size && buffer[length] != '\0') length++;
        return std::string(buffer, length);
    }

    static void CopyStringToFixed(char *dst, size_t dstSize, const std::string& src)
    {
        if (dst == nullptr || dstSize == 0) return;

        std::memset(dst, 0, dstSize);

        size_t copyLen = src.size();
        if (copyLen >= dstSize) copyLen = dstSize - 1;
        std::memcpy(dst, src.data(), copyLen);
    }

    static bool WaitForReadable(SocketHandle sock, int timeoutMs)
    {
        // Wait for a packet

        if (sock == kInvalidSocket)
            return false;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);

        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        #ifdef _WIN32
        int result = ::select(0, &readSet, nullptr, nullptr, &tv);
        #else
        int result = ::select(sock + 1, &readSet, nullptr, nullptr, &tv);
        #endif

        return (result > 0) && FD_ISSET(sock, &readSet);
    }

    static void SetIntSocketOption(SocketHandle sock, int level, int option, int value)
    {
        #ifdef _WIN32
        ::setsockopt(
            sock,
            level,
            option,
            reinterpret_cast<const char*>(&value),
            static_cast<int>(sizeof(value)));
        #else
        ::setsockopt(sock, level, option, &value, sizeof(value));
        #endif
    }

    static int GetLastSocketError()
    {
        #ifdef _WIN32
        return ::WSAGetLastError();
        #else
        return errno;
        #endif
    }

    static bool IsRetryableSocketError(int errorCode)
    {
        #ifdef _WIN32
        return (errorCode == WSAEWOULDBLOCK) || (errorCode == WSAEINTR);
        #else
        return (errorCode == EAGAIN) || (errorCode == EWOULDBLOCK) || (errorCode == EINTR);
        #endif
    }

    static void CloseSocket(SocketHandle sock)
    {
        if (sock == kInvalidSocket)
            return;

        #ifdef _WIN32
        ::closesocket(sock);
        #else
        ::close(sock);
        #endif
    }

    static uint32_t PrefixLengthToMask(uint32_t prefixLength)
    {
        if (prefixLength == 0)
            return 0;

        if (prefixLength >= 32)
            return 0xFFFFFFFFu;

        return 0xFFFFFFFFu << (32 - prefixLength);
    }

    static std::vector<IPv4Address> GetBroadcastAddresses()
    {
        std::vector<IPv4Address> addresses;
        std::set<IPv4Address> unique;

        #ifdef _WIN32
        ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
        ULONG bufferSize = 15000;

        std::vector<unsigned char> buffer(bufferSize);

        ULONG result = ::GetAdaptersAddresses(
            AF_INET,
            flags,
            nullptr,
            reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()),
            &bufferSize);

        if (result == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(bufferSize);

            result = ::GetAdaptersAddresses(
                AF_INET,
                flags,
                nullptr,
                reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()),
                &bufferSize);
        }

        if (result == NO_ERROR)
        {
            PIP_ADAPTER_ADDRESSES adapter =
                reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

            while (adapter != nullptr)
            {
                PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress;

                while (unicast != nullptr)
                {
                    if ((unicast->Address.lpSockaddr != nullptr) &&
                        (unicast->Address.lpSockaddr->sa_family == AF_INET))
                    {
                        sockaddr_in* ipv4 =
                            reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);

                        uint32_t ipHost = ntohl(ipv4->sin_addr.s_addr);

                        if ((ipHost & 0xFF000000u) != 0x7F000000u)
                        {
                            uint32_t prefixLength = static_cast<uint32_t>(unicast->OnLinkPrefixLength);

                            if (prefixLength < 32)
                            {
                                uint32_t mask = PrefixLengthToMask(prefixLength);
                                uint32_t broadcastHost = (ipHost & mask) | (~mask);
                                unique.insert(htonl(broadcastHost));
                            }
                        }
                    }

                    unicast = unicast->Next;
                }

                adapter = adapter->Next;
            }
        }

        #else

        ifaddrs* ifaddr = nullptr;

        if (::getifaddrs(&ifaddr) == 0)
        {
            for (ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
            {
                if ((ifa->ifa_addr == nullptr) ||
                    (ifa->ifa_addr->sa_family != AF_INET) ||
                    ((ifa->ifa_flags & IFF_UP) == 0) ||
                    ((ifa->ifa_flags & IFF_BROADCAST) == 0) ||
                    ((ifa->ifa_flags & IFF_LOOPBACK) != 0) ||
                    (ifa->ifa_broadaddr == nullptr))
                        continue;

                sockaddr_in* broadAddr = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
                unique.insert(broadAddr->sin_addr.s_addr);
            }

            ::freeifaddrs(ifaddr);
        }
        #endif

        // We need at least 1 broadcast address
        if (unique.empty())
            unique.insert(htonl(INADDR_BROADCAST));

        // Build vector to return (instead of a set)
        for (IPv4Address addr : unique)
            addresses.push_back(addr);

        return addresses;
    }

    private:
    DeviceInfo_t m_DeviceInfo;
    std::thread m_Thread;
    std::atomic<bool> m_Running;
    std::atomic<SocketHandle> m_ListenSocket;
};
