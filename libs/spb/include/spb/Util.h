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
#ifdef __linux__
#include <unistd.h>
#include <dirent.h>
#endif
#ifdef __APPLE__
#include <unistd.h>
#endif
#include "md5.h"

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

    static void WriteAllBytes(const std::string& Filename, const std::vector<uint8_t> &Data, bool bAppend = false)
    {
        // Open the file
        std::ios_base::openmode mode = std::ios::binary;
        if (bAppend) mode |= std::ios::app;

        std::ofstream file;
        std::ostream *out = nullptr;
        // Open the file, or use cout when "-" is specified
        if (Filename == "-")
            out = &std::cout;
        else 
        {
            file.open(Filename, mode);
            if (!file) throw std::runtime_error("Could not open file " + Filename);
            out = &file;
        }

        // Write the data
        out->write(reinterpret_cast<const char*>(Data.data()), Data.size());
        // Flush the data
        out->flush();
    }

    enum DumpFlags : uint8_t { None = 0x00, Append = 0x01, NoAddresses = 0x02, NoText = 0x04, NoMD5 = 0x08, Compact = 0x10 };
    static void Dump(const std::vector<uint8_t>& data, const std::string& filename = "", uint8_t Flags = DumpFlags::None)
    {
        // Output to file ?
        if (!filename.empty())
        {
            try { WriteAllBytes(filename, data, ((Flags & DumpFlags::Append) != 0)); }
            catch (...) { std::cerr << "Can't write to file \"" << filename << "\"!" << std::endl; }
        }
        else
        {
            // Dump to screen
            for (size_t i = 0; i < data.size(); i += 16)
            {
                if (!(Flags & DumpFlags::NoAddresses))
                    std::cout << std::setw(4) << std::setfill('0') << std::hex << i << ": ";
                for (size_t j = i; j < i + 16 && j < data.size(); ++j)
                {
                    std::cout << std::setw(2) << std::setfill('0') << std::hex << (int)data[j];
                    if (!(Flags & DumpFlags::Compact)) std::cout << " ";

                }
                if (!(Flags & DumpFlags::NoText))
                {
                    std::cout << "  ";
                    for (size_t j = i; j < i + 16 && j < data.size(); ++j)
                        std::cout << (data[j] <= 0x20 || data[j] > 0x7f ? '.' : (char)data[j]);
                }
                std::cout << std::endl;
            }

            // Display MD5
            if (!(Flags && DumpFlags::NoMD5))
            {
                MD5_CTX ctx; MD5Init(&ctx);
                MD5Update(& ctx, (unsigned char *)data.data(), (unsigned)data.size());
                unsigned char hash[16]; MD5Final(hash, &ctx);
                std::cout << "MD5:";
                for (int i = 0; i < 16; ++i) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
                std::cout << std::endl;
            }

            std::cout << std::dec;
        }
    }
    static void Dump(void *data, int Len, const std::string& filename = "", uint8_t Flags = DumpFlags::None)
    {
        Dump(std::vector<uint8_t>((uint8_t*)data, (uint8_t *)data + Len), filename, Flags);
    }

    static void Vt100_GotoXY(int x, int y) { std::cout << "\x1b[" << y << ";" << x << "H"; }
    static void Vt100_Cls() { std::cout << "\x1b[2J"; }
    static void Vt100_ShowCursor() { std::cout << "\x1b[?25h"; }
    static void Vt100_HideCursor() { std::cout << "\x1b[?25l"; }
    static void Vt100_ForegroundColor(uint8_t r, uint8_t g, uint8_t b)
    {
        std::cout << "\x1b[38;2;" << static_cast<int>(r) << ";" << static_cast<int>(g) << ";" << static_cast<int>(b) << "m";
    }

    static bool IsRoot()
    {
        #ifdef _WIN32
        return false;
        #else   
        return geteuid() == 0;
        #endif
    }
    static bool IsSLB()
    {
        std::ifstream file("/proc/device-tree/model");
        if (!file) return false;
        std::string model; std::getline(file, model);
        return model.find("Raspberry Pi Zero 2 W") != std::string::npos;
    }
    static bool FileExists(const char* fn) 
    { 
        #ifdef __linux__
        return access(fn, F_OK) == 0;
        #else
        return std::filesystem::exists(fn);
        #endif
    }
    static bool PidExists(int pid)
    { 
        #ifdef __linux__
        char PidFilename[20]; sprintf(PidFilename, "/proc/%d", pid);
        return FileExists(PidFilename); 
        #else
        return 0;
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
};
