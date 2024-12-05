/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Dec 2024
 *
 ****************************************************************************/

#include "mac_address.h"

#ifdef _WIN32
// Windows

#    include <iostream>
#    include <iphlpapi.h>
#    include <iomanip>
#    include <regex>

#    pragma comment(lib, "iphlpapi.lib")

namespace scorbit {
namespace detail {

bool isVirtualAdapter(const std::wstring &adapterName)
{
    // Check for known virtual interface patterns
    static const std::wregex virtualPatterns(LR"((^isatap|^teredo|^loopback|^vEthernet|^TAP|^tun))",
                                             std::regex_constants::icase);
    return std::regex_search(adapterName, virtualPatterns);
}

std::string getMacAddress()
{
    ULONG bufferSize = 0;
    GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &bufferSize);

    auto adapterAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(new char[bufferSize]);
    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapterAddresses, &bufferSize) != NO_ERROR) {
        delete[] adapterAddresses;
        return {};
    }

    std::string macAddress;
    for (IP_ADAPTER_ADDRESSES *adapter = adapterAddresses; adapter; adapter = adapter->Next) {
        // Exclude virtual adapters by name
        if (isVirtualAdapter(adapter->FriendlyName)) {
            continue;
        }

        if (adapter->PhysicalAddressLength == 6) { // MAC address length
            std::ostringstream mac;
            for (int i = 0; i < 6; ++i) {
                mac << std::hex << std::setw(2) << std::setfill('0')
                    << (int)adapter->PhysicalAddress[i];
                if (i != 5) {
                    mac << ":";
                }
            }
            macAddress = mac.str();
            break; // Use the first valid adapter's MAC address
        }
    }
    delete[] adapterAddresses;
    return macAddress;
}

} // namespace detail
} // namespace scorbit

#else
// Unix

#    include <iostream>
#    include <ifaddrs.h>
#    include <cstring>
#    include <iomanip>
#    include <sstream>
#    include <sys/socket.h>
#    include <net/if.h>
#    include <regex>

#    ifdef __linux__
#        include <netpacket/packet.h>
#    elif defined(__APPLE__)
#        include <net/if_dl.h> // For sockaddr_dl and LLADDR
#    endif

namespace scorbit {
namespace detail {

// Check if the interface name matches common virtual interface patterns
bool isVirtualInterface(const std::string &interfaceName)
{
    static const std::regex virtualPatterns(
            R"((^lo$|^docker|^veth|^virbr|^br-|^tap|^tun|^wl|^awdl|^dummy|^anpi|^bridge|^vnet))",
            std::regex_constants::icase);
    return std::regex_search(interfaceName, virtualPatterns);
}

std::string getMacAddress()
{
    struct ifaddrs *ifaddr = nullptr;
    struct ifaddrs *ifa = nullptr;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return {};
    }

    std::string macAddress;
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr)
            continue;

        // Skip virtual interfaces by name
        if (isVirtualInterface(ifa->ifa_name)) {
            continue;
        }

#    ifdef __linux__
        if (ifa->ifa_addr->sa_family != AF_PACKET)
            continue;

        struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
        if (s->sll_halen == 6) { // MAC address length
            std::ostringstream mac;
            for (int i = 0; i < 6; ++i) {
                mac << std::hex << std::setw(2) << std::setfill('0') << (int)s->sll_addr[i];
                if (i != 5) {
                    mac << ":";
                }
            }
            macAddress = mac.str();
            break;
        }
#    elif defined(__APPLE__)
        if (ifa->ifa_addr->sa_family != AF_LINK)
            continue;

        struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
        unsigned char *mac = (unsigned char *)LLADDR(sdl);
        if (sdl->sdl_alen == 6) { // MAC address length
            std::ostringstream macStream;
            for (int i = 0; i < 6; ++i) {
                macStream << std::hex << std::setw(2) << std::setfill('0') << (int)mac[i];
                if (i != 5) {
                    macStream << ":";
                }
            }
            macAddress = macStream.str();
            break;
        }
#    endif
    }

    freeifaddrs(ifaddr);
    return macAddress;
}

} // namespace detail
} // namespace scorbit

#endif
