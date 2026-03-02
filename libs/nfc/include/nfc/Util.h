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
            value = (s.substr(0, 2) == "0x") ? std::stoul(s, nullptr, 16) : std::stoul(s);
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
            value = (s.substr(0, 2) == "0x") ? std::stoul(s, nullptr, 16) : std::stoul(s);
            return true;
        }
        catch (...)
        {
            value = 0;
            return false;
        }
    }

    static std::string StringFromBuffer(std::vector<uint8_t>& data, int Pos, int MaxLen)
    {
        // Get string from buffer
        std::string str = std::string(&data[Pos], &data[Pos + MaxLen]);
        // Strip ending '\0'
        size_t pos = str.find('\0');
        if (pos != std::string::npos)
            str.erase(pos);
        return str;
    }
    static uint16_t UInt16FromBuffer(std::vector<uint8_t>& data, int Pos)
    {
        if (Pos + 2 > data.size()) return 0;
        return data[Pos] | (data[Pos + 1] << 8);
    }
    static uint32_t UInt32FromBuffer(std::vector<uint8_t>& data, int Pos)
    {
        if (Pos + 4 > data.size()) return 0;
        return data[Pos] | (data[Pos + 1] << 8) | (data[Pos + 2] << 16) | (data[Pos + 3] << 24);
    }
    static uint64_t UInt64FromBuffer(std::vector<uint8_t>& data, int Pos)
    {
        if (Pos + 8 > data.size()) return 0;
        return (uint64_t)UInt32FromBuffer(data, Pos) | ((uint64_t)UInt32FromBuffer(data, Pos + 4) << 32);
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
    static int GetPid(const char* pName)
    {
        int pid = -1;

        #ifdef __linux__
        DIR* dir = opendir("/proc");
        if (dir)
        {
            struct dirent* de = 0;
            while ((de = readdir(dir)) != 0)
            {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                if (sscanf(de->d_name, "%d", &pid) == 1)
                {
                    // Open the /proc/pid/comm file to determine what's the name of the process
                    char comm_fn[256]; sprintf(comm_fn, "/proc/%d/comm", pid);
                    FILE* hComm = fopen(comm_fn, "r");
                    if (hComm == NULL) continue;
                    char* ProcessName = NULL;
                    size_t ProcessNameSize = 0;
                    bool bOk = (getline(&ProcessName, &ProcessNameSize, hComm) > 0);
                    fclose(hComm);
                    // Remove the ending \n
                    char* p = strchr(ProcessName, '\n'); if (p) *p = 0;
                    // Process has been found ?
                    //printf("%d: -%s- -%s- -%s-\n", pid, pName, comm_fn, ProcessName);
                    if (bOk && !strcmp(ProcessName, pName)) break;
                }
                pid = -1;
            }
            closedir(dir);
        }
        #endif // __linux__

        return pid;
    }

    static uint16_t Crc16(const uint8_t* Data, int Len)
    {
        const uint16_t polynom = 0x8005; // Polynome used
        uint16_t crc = 0;
        for (int i = 0; i < Len; i++)
            for (uint8_t shift_register = 0x01; shift_register > 0x00; shift_register <<= 1)
            {
                uint8_t data_bit = (Data[i] & shift_register) ? 1 : 0;
                uint8_t crc_bit = crc >> 15;
                crc <<= 1;
                if (data_bit != crc_bit)
                    crc ^= polynom;
            }
        return crc;
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
        out.resize(needed); 

        // Format the string
        std::vsnprintf(out.data(), needed + 1, sFormat, ap);
        va_end(ap);

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
        
        std::string path = "/var/lock/" + std::filesystem::path(LockName).filename().string() + ".lock";
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
