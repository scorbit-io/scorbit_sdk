// --------------------------------------------------------------------
//  Project:           Scorbitron SLB
//  Description:       Miscellaneous functions
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2024-12-21	| Initial Release
// --------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdlib>
#include <iterator>
#include <cstdarg>
#include <cstring>
#include <limits>
#ifdef __linux__
#include <unistd.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#ifndef DBG
#define DBG(...) std::cerr << Util::Format(__VA_ARGS__)
#endif

#ifndef INF
#define INF(...) std::cerr << Util::Format(__VA_ARGS__)
#endif

#ifndef WRN
#define WRN(...) std::cerr << Util::Format(__VA_ARGS__)
#endif

#ifndef ERR
#define ERR(...) std::cerr << Util::Format(__VA_ARGS__)
#endif

class Util
{
    public:
    static bool FromString(const std::string& s, int32_t& value)
    {
        try
        {
            std::string parse = s;
            // Process sign
            bool negative = false;
            if (!parse.empty() && (parse[0] == '+' || parse[0] == '-')) { negative = (parse[0] == '-'); parse.erase(parse.begin()); }
			// Reject anothers sign after the first one
            if (!parse.empty() && (parse[0] == '+' || parse[0] == '-')) { value = 0; return false; }
			// Process hexadecimal prefix
            int base = 10;
            if (parse.size() > 2 && parse[0] == '0' && (parse[1] == 'x' || parse[1] == 'X')) { base = 16; parse = parse.substr(2); }
			// Reject empty string
            if (parse.empty()) { value = 0; return false; }
			// Convert the number
            size_t idx = 0;
            long long raw = std::stoll(parse, &idx, base);
            if (idx != parse.size()) { value = 0; return false; }
			// Apply sign
            if (negative) raw = -raw;
			// Check range
            if (raw < std::numeric_limits<int32_t>::min() || raw > std::numeric_limits<int32_t>::max())
            {
                value = 0;
                return false;
            }
			// Store the result
            value = static_cast<int32_t>(raw);
            return true;
        }
        catch (...)
        {
            value = 0;
            return false;
        }
    }

    static bool FromString(const std::string& s, uint32_t& value)
    {
        try
        {

            std::string parse = s;
			// Reject negative numbers
            if (!parse.empty() && s[0] == '-') { value = 0; return false; }
			// Process sign
            if (!parse.empty() && parse[0] == '+') parse.erase(parse.begin());
            // Reject anothers sign after the first one
            if (!parse.empty() && (parse[0] == '+' || parse[0] == '-')) { value = 0; return false; }
            // Process hexadecimal prefix
            int base = 10;
            if (parse.size() > 2 && parse[0] == '0' && (parse[1] == 'x' || parse[1] == 'X')) { base = 16; parse = parse.substr(2); }
            // Reject empty string
            if (parse.empty()) { value = 0; return false; }
			// Convert the number
            size_t idx = 0;
            unsigned long long raw = std::stoull(parse, &idx, base);
            if (idx != parse.size()) { value = 0; return false; }
			// Check range
            if (raw > std::numeric_limits<uint32_t>::max()) { value = 0; return false; }
			// Store the result
            value = static_cast<uint32_t>(raw);
            return true;
        }
        catch (...)
        {
            value = 0;
            return false;
        }
    }

    static std::string ReadString(const uint8_t *data, size_t dataLen, size_t Pos, size_t MaxLen)
    {
        if (Pos >= dataLen) return {};
        size_t len = std::min(MaxLen, dataLen - Pos);
        const uint8_t* start = data + Pos;
        const void* zero = std::memchr(start, 0, len);
        size_t realLen = (zero != nullptr) ? static_cast<size_t>(static_cast<const uint8_t*>(zero) - start) : len;
        return std::string(reinterpret_cast<const char*>(start), realLen);
    }
	static std::string ReadString(const std::vector<uint8_t>& data, size_t Pos, size_t MaxLen) { return ReadString(data.data(), data.size(), Pos, MaxLen); }
    static uint16_t ReadUInt16LE(const uint8_t *data, size_t dataLen, size_t Pos)
    {
        if (Pos + 2 > dataLen) return 0;
        return static_cast<uint16_t>(data[Pos + 0]) | static_cast<uint16_t>(data[Pos + 1] << 8);
    }
    static uint16_t ReadUInt16LE(const std::vector<uint8_t>& data, size_t Pos) { return ReadUInt16LE(data.data(), data.size(), Pos); }
    static uint32_t ReadUInt32LE(const uint8_t* data, size_t dataLen, size_t Pos)
    {
        if (Pos + 4 > dataLen) return 0;
        return static_cast<uint32_t>(ReadUInt16LE(data, dataLen, Pos + 0)) |
            (static_cast<uint32_t>(ReadUInt16LE(data, dataLen, Pos + 2)) << 16);
    }
    static uint32_t ReadUInt32LE(const std::vector<uint8_t>& data, size_t Pos) { return ReadUInt32LE(data.data(), data.size(), Pos); }
    static uint64_t ReadUInt64LE(const uint8_t* data, size_t dataLen, size_t Pos)
    {
        if (Pos + 8 > dataLen) return 0;
        return static_cast<uint64_t>(ReadUInt32LE(data, dataLen, Pos + 0)) |
            (static_cast<uint64_t>(ReadUInt32LE(data, dataLen, Pos + 4)) << 32);
    }
    static uint64_t ReadUInt64LE(const std::vector<uint8_t>& data, size_t Pos) { return ReadUInt64LE(data.data(), data.size(), Pos); }
    static bool WriteLE(uint8_t* data, size_t dataLen, size_t Pos, uint16_t value)
    {
		if (Pos + 2 > dataLen) return false;
        data[Pos + 0] = static_cast<uint8_t>(value);
        data[Pos + 1] = static_cast<uint8_t>(value >> 8);
        return true;
    }
    static bool WriteLE(uint8_t* data, size_t dataLen, size_t Pos, uint32_t value)
    {
        if (Pos + 4 > dataLen) return false;
        WriteLE(data, dataLen, Pos + 0, static_cast<uint16_t>(value));
        WriteLE(data, dataLen, Pos + 2, static_cast<uint16_t>(value >> 16));
        return true;
    }
    static bool WriteLE(uint8_t* data, size_t dataLen, size_t Pos, uint64_t value)
    {
        if (Pos + 8 > dataLen) return false;
        WriteLE(data, dataLen, Pos + 0, static_cast<uint32_t>(value));
        WriteLE(data, dataLen, Pos + 4, static_cast<uint32_t>(value >> 32));
        return true;
    }
    static std::string ToHexString(const std::uint8_t* Data, std::size_t Size)
    {
        if (Data == nullptr || Size == 0) return std::string();

        static const char kHex[] = "0123456789abcdef";
        std::string out;
        out.resize(Size * 2);
        for (std::size_t i = 0; i < Size; i++)
        {
            const std::uint8_t b = Data[i];
            out[2 * i + 0] = kHex[(b >> 4) & 0x0F];
            out[2 * i + 1] = kHex[b & 0x0F];
        }
        return out;
    }
    static std::vector<uint8_t> ReadAllBytes(const std::string& Filename)
    {
        // Allocate a vector
        std::vector<uint8_t> Data;

        // Open the file
        std::ifstream file(Filename, std::ios::binary);
        if (!file) { throw std::runtime_error("Could not open file " + Filename); return Data; }

        // Stop eating new lines in binary mode
        file.unsetf(std::ios::skipws);

        // Get its size
        std::streampos fileSize;
        file.seekg(0, std::ios::end);
        fileSize = file.tellg();
        // Check if the file size is valid
        if (fileSize <= 0) return Data;
        file.seekg(0, std::ios::beg);

        // Reserve capacity
        Data.reserve(fileSize);

        // Read the data
        Data.insert(Data.begin(),
            std::istream_iterator<uint8_t>(file),
            std::istream_iterator<uint8_t>());

        return Data;
    }


    static bool IsRoot()
    {
        #ifdef _WIN32
        return false;
        #else   
        return geteuid() == 0;
        #endif
    }
    static bool FileExists(const char* fn)
    { 
        #ifdef __linux__
        return access(fn, F_OK) == 0;
        #else
        return std::filesystem::exists(fn);
        #endif
    }

    static inline const std::string Format(const char *sFormat, ...)
    {
        if (!sFormat) return {};

        va_list ap;
        va_start(ap, sFormat);

        // Determine the string length
        va_list apCopy;
        va_copy(apCopy, ap);
        #if defined(_MSC_VER)
        int needed = _vscprintf(sFormat, apCopy);
        #else
        int needed = std::vsnprintf(nullptr, 0, sFormat, apCopy);
        #endif
        va_end(apCopy);

        // Anything to output ?
        if (needed < 0) { va_end(ap); return {}; }

        // Dimension the output string
        std::string out;
        out.resize(static_cast<size_t>(needed) + 1); 

        // Format the string
        std::vsnprintf(out.data(), out.size(), sFormat, ap);
        va_end(ap);
        out.resize(static_cast<size_t>(needed));

        return out;
    }
};

class HardwareDebug
{
	public:
	enum : uint32_t
	{
		DebugNone = 0x0,
		DebugCable = 0x1,
		DebugProbe = 0x2,
		DebugSLB = 0x04,
		DebugAll = 0xffffffff
	};

	static uint32_t DebugFlags;
	static void SetFlags(uint32_t f) { DebugFlags = f; }
	static bool IsFlagSet(uint32_t f) { return (DebugFlags & f) != 0; }
};

inline uint32_t HardwareDebug::DebugFlags = HardwareDebug::DebugNone;

class InterprocessLock
{
    public:
    explicit InterprocessLock(const std::string& LockName)
    {
        #ifdef _WIN32
        std::string mutexName = "Local\\MyDeviceLock_" + LockName;

        m_handle = ::CreateMutexA(nullptr, FALSE, mutexName.c_str());
        if (m_handle == nullptr)
        {
            ERR("Can't create/open mutex !\n");
            return;
        }

        DWORD waitResult = ::WaitForSingleObject(m_handle, INFINITE);
        if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED)
            m_locked = true;
        else
        {
            ::CloseHandle(m_handle);
            m_handle = nullptr;
            ERR("Can't lock mutex !\n");
            return;
        }

        #else
        
        #ifdef __linux__
        std::string path = "/var/lock/" + std::filesystem::path(LockName).filename().string() + ".lock";
        #else
        std::string path = "/tmp/" + std::filesystem::path(LockName).filename().string() + ".lock";
        #endif // __linux__

        // Try to create the lock file
        m_fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
        // 2nd try as Read Only (if root has already created the file with a restrictive umask)
        if (m_fd < 0 && errno == EACCES)
            m_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (m_fd < 0)
        {
            ERR("Can't create lock file %s: %s\n", path.c_str(), std::strerror(errno));
            return;
        }

        // Widen the permissions for others
        ::fchmod(m_fd, 0666);

        // Lock the file (and wait if it's already locked)
        if (::flock(m_fd, LOCK_EX) == 0)
            m_locked = true;
        else
        {
            ::close(m_fd);
            m_fd = -1;
            ERR("Can't flock(%s): %s\n", path.c_str(), std::strerror(errno));
            return;
        }
        #endif
    }

    ~InterprocessLock()
    {
        #ifdef _WIN32
        if (m_handle != nullptr)
        {
            ::ReleaseMutex(m_handle);
            ::CloseHandle(m_handle);
        }
        #else
        if (m_fd >= 0)
        {
            ::flock(m_fd, LOCK_UN);
            ::close(m_fd);
        }
        #endif
    }

    InterprocessLock(const InterprocessLock&) = delete;
    InterprocessLock& operator=(const InterprocessLock&) = delete;
    bool IsLocked() const { return m_locked; }

    private:
    bool m_locked = false;
    #ifdef _WIN32
    HANDLE m_handle = nullptr;
    #else
    int m_fd = -1;
    #endif
};
