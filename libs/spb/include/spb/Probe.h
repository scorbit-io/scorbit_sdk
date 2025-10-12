// --------------------------------------------------------------------
//  Project:           Scorbitron SLB
//  Description:       Probe class
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2024-12-21	| Initial Release
// --------------------------------------------------------------------

#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <cstring>
#include <memory>
#include "Debug.h"
#include "Cable.h"
#include "Util.h"
#include "Serial.h"
#include <sys/stat.h>  // for mkdir

#ifdef USE_FMT_LIBRARY
#include <fmt/format.h>
#    include <fmt/chrono.h>
#else
#    include <format>
namespace fmt = std;
#endif

#ifdef USE_DATE_LIBRARY
#    include <date/date.h>
#else
namespace date = std::chrono;
#endif

#ifdef USE_OPENCV2
#include <opencv2/opencv.hpp>
#else
#include "stb_image/stb_image.h"
#include "stb_image/stb_image_resize2.h"
#endif

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#ifdef __APPLE__
    #include <sys/mount.h>
    // MacOS mount flags equivalents
    #define MS_NOEXEC MNT_NOEXEC
    #define MS_SYNCHRONOUS MNT_SYNCHRONOUS
    // MacOS uses unmount instead of umount
    #define umount(dir) unmount(dir, 0)
    // MacOS mount function has different signature
    #define mount(dev, dir, type, flags, data) mount(type, dir, flags, data)
#else
    #include <sys/mount.h>
#endif
#endif

typedef enum
{
    ReadProbeInfos = 0x02,
    ReadProbeStatus = 0x06,
    ReadUserFlash = 0x0a,
    WriteUserFlash = 0x0b,
    WriteLedConfig = 0x13,
    ReadSwitch = 0x16,
    
    ReadDmdFrame = 0x22,
    WriteDmdOverlay = 0x23,
    ReadDmdInfos = 0x26,
    WriteDmdOverlayControlReg = 0x27,
    WriteDmdType = 0x2b,

    ReadCpuMemory = 0x32,
    WriteCpuMemory = 0x33,
    ReadCpuInfos = 0x36,
    WriteCpuControlReg = 0x37,
    WriteCpuType = 0x3b,
    EmulateCpu = 0x3f,

    WriteNfcLedsAnimation = 0x43,
    ReadNfcInfos = 0x46,
    WriteNfcType = 0x4b,

    WritePinMAMEControl = 0xd3,
    ReadPinMAMEInfos = 0xd6,

    ReadGpio = 0xe2,
    WriteGpio = 0xe3,
    ReadTesterInfos = 0xe6,
    WriteTesterInfos = 0xe7,

    ReadTest = 0xf2,
    Reboot = 0xf3,
    Bootloader = 0xf7
} ProbeCommand_t;

class ProbeBase
{
    public:
    SerialCable Cable;

    public:
    ProbeBase() {}
    virtual ~ProbeBase() { Cable.Close(); }

    protected:
    virtual bool OnInitialized() { return true; }
    
    public:
    virtual bool Initialize(int DeviceIndex, const std::string& DeviceName)
    {
        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe))
        {
            if (DeviceName.empty())
                std::cerr << "Initializing cable with DeviceIndex=" << DeviceIndex << std::endl;
            else
                std::cerr << "Initializing cable with DeviceName=" << DeviceName << std::endl;
        }
        Cable.Close();

        if (!DeviceName.empty() && !Cable.Initialize(DeviceName))
        {
            if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe))
                std::cerr << "Device \"" << DeviceName << "\" can't be initialized !\n";
            return false;
        }
        if (DeviceName.empty() && !Cable.Initialize(DeviceIndex))
        {
            if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe))
            {
                if (DeviceIndex >= 0)
                    std::cerr << "Device #" << DeviceIndex << " can't be initialized !\n";
                else
                    std::cerr << "Default device can't be initialized !\n";
            }
            return false;
        }

        // Perform any initialization needed after the probe is connected
        return OnInitialized();
    }

    typedef struct
    { 
        int DeviceIndex;

        uint16_t Magic;
        std::string Id, Name; 
        int VersionMajor, VersionMinor, VersionRevision; 
        uint32_t TimestampHeader;
        uint64_t TimestampUTC;
        std::string Timestamp;  
        long TimeMs; 
        uint32_t SysClockFrequency; 
        uint8_t UsbFlags;
        uint8_t ReservedForAlignment1;
        uint32_t UID;
        uint8_t TpmSerial[9];
        uint8_t Reserved[51];
    } ProbeInformations_t;

    static std::vector<ProbeInformations_t> FindAllProbes(const std::string& sType = "")
    {
        std::vector<ProbeBase::ProbeInformations_t> ProbeInfos;

        // Browse the devices
        // (we should find a better alternative that works on all platform)
        for (int iDevice = SerialCable::iFirstDevice; iDevice <= SerialCable::iLastDevice; iDevice++)
        {
            ProbeBase probe;
            // Try to open the cable
            if (!probe.Initialize(iDevice, "")) continue;
            // Is it a probe ?
            ProbeBase::ProbeInformations_t pbi;
            if (!probe.GetInformations(&pbi)) continue;
            // Does the type matches ?
            if (!sType.empty() && sType != pbi.Id) continue;
            // Found a probe !
            ProbeInfos.push_back(pbi);
        }
        return ProbeInfos;
    }

    bool FindProbe(const std::string &sType, uint32_t UID = 0)
    {
        // Browse the devices
        // (we should find a better alternative that works on all platform)
        for (int iDevice = SerialCable::iFirstDevice; iDevice <= SerialCable::iLastDevice; iDevice++)
        {
            // Try to open the cable
            if (!Initialize(iDevice, "")) continue;
            // Is it a probe ?
            ProbeBase::ProbeInformations_t pbi;
            if (!this->GetInformations(&pbi)) continue;
            // Found a good one ?
            if ((sType.empty() || pbi.Id == sType) && (UID == 0 || pbi.UID == UID))
                return true;
        }
        return false;
    }

    virtual bool GetInformations(ProbeInformations_t *pbi)
    { 
        pbi->Id = "";
        pbi->DeviceIndex = Cable.DeviceIndex;

        // Read probe memory
        auto data = Cable.CommandRead(ProbeCommand_t::ReadProbeInfos, 0, 128);
        if (data.empty()) return false;
        pbi->Magic = Util::UInt16FromBuffer(data, 0);
        if (pbi->Magic != 0xCAFE) return false;
        // Extract the probe name
        pbi->Id = Util::StringFromBuffer(data, 2, 4);
        pbi->Name = Util::StringFromBuffer(data, 6, 12);
        pbi->VersionMajor = data[18];
        pbi->VersionMinor = data[19];
        pbi->VersionRevision = Util::UInt16FromBuffer(data, 20);
        pbi->TimestampHeader = Util::UInt16FromBuffer(data, 22);
        if (pbi->TimestampHeader != 0)
        {
            // Obsolete : Timestamp is a string
            pbi->Timestamp = Util::StringFromBuffer(data, 22, 32);
            pbi->TimestampUTC = 0; // Unknown timestamp 64
        }
        else
        {
            // New version : timestamp is a uint64_t (ex: 20250812122400)
            pbi->TimestampUTC = Util::UInt64FromBuffer(data, 24);
            uint64_t ymdhms = pbi->TimestampUTC;
            unsigned sec = ymdhms % 100; ymdhms /= 100;
            unsigned min = ymdhms % 100; ymdhms /= 100;
            unsigned hour = ymdhms % 100; ymdhms /= 100;
            unsigned day = ymdhms % 100; ymdhms /= 100;
            unsigned mon = ymdhms % 100; ymdhms /= 100;
            int      year = (int)ymdhms;

            // Create the timestamp string
            std::ostringstream oss;
            oss << std::setfill('0')
                << std::setw(4) << year << "-"
                << std::setw(2) << mon << "-"
                << std::setw(2) << day << " "
                << std::setw(2) << hour << ":"
                << std::setw(2) << min << ":"
                << std::setw(2) << sec;
            pbi->Timestamp = oss.str();
        }
        pbi->TimeMs = Util::Util::UInt32FromBuffer(data, 54);
        pbi->SysClockFrequency = Util::UInt32FromBuffer(data, 58);
        pbi->UsbFlags = data[62];
        pbi->ReservedForAlignment1 = data[63];
        pbi->UID = Util::UInt32FromBuffer(data, 64);
        std::memcpy(pbi->TpmSerial, data.data() + 68, sizeof(pbi->TpmSerial));
        std::memcpy(pbi->Reserved, data.data() + 77, sizeof(pbi->Reserved));
        return true;
    }

    virtual bool ReadStatus(uint8_t& status) 
    {
        status = 0;
        auto data = Cable.CommandRead(ProbeCommand_t::ReadProbeStatus, 0, 1);
        if (data.empty()) return false;
        status = data[0];
        return true;
    }

    virtual bool SetLed(uint8_t iLed, uint8_t duration, uint32_t pattern, uint8_t nCycles)
    {
        if (Cable.GetName() == "FT4222")
        {
            // Pattern is 16-bit
            return Cable.CommandWrite(ProbeCommand_t::WriteLedConfig, 0, { duration, static_cast<uint8_t>(pattern >> 8), static_cast<uint8_t>(pattern), nCycles });
        }
        else 
        {
            // Pattern is 32-bit
            return Cable.CommandWrite(ProbeCommand_t::WriteLedConfig, 0, {
                iLed,
                duration,
                static_cast<uint8_t>(pattern >> 0),
                static_cast<uint8_t>(pattern >> 8),
                static_cast<uint8_t>(pattern >> 16),
                static_cast<uint8_t>(pattern >> 24),
                nCycles
                });
        }
    }

    virtual bool ReadSwitch(uint8_t& pushCount)
    {
        pushCount = 0;
        auto data = Cable.CommandRead(ProbeCommand_t::ReadSwitch, 0, 1);
        if (data.empty()) return false;
        pushCount = data[0];
        return true;
    }

    typedef struct { uint32_t Magic, Mask, Directions, Values, Pullups, Pulldowns; } Gpio;
    virtual bool ReadGpio(Gpio* gpio)
    {
        auto data = Cable.CommandRead(ProbeCommand_t::ReadGpio, 0, sizeof(Gpio));
        if (data.size() < sizeof(Gpio)) return false;
        std::memcpy(gpio, data.data(), sizeof(Gpio));
        // Check Magic
        return (gpio->Magic == 0x4f495047);
    }
    virtual bool WriteGpio(uint32_t Mask, uint32_t Directions, uint32_t Values, uint32_t Pullups, uint32_t Pulldowns)
    {
        Gpio gpio;
        gpio.Magic = 0x4f495047;
        gpio.Mask = Mask;
        gpio.Directions = Directions; // For each bit, 0 for output, 1 for input
        gpio.Values = Values;
        gpio.Pullups = Pullups;
        gpio.Pulldowns = Pulldowns;
        std::vector<uint8_t> data(sizeof(gpio));
        std::memcpy(data.data(), &gpio, sizeof(Gpio));  // Copier la structure dans le vecteur
        return Cable.CommandWrite(ProbeCommand_t::WriteGpio, 0, data);
    }

    virtual std::vector<uint8_t> ReadFlashMemory(uint32_t Address, int Len)
    {
        return Cable.CommandRead(ProbeCommand_t::ReadUserFlash, Address, Len);
    }
    virtual bool WriteFlashMemory(uint32_t Address, const std::vector<uint8_t>& Buffer)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteUserFlash, Address, Buffer);
    }

    virtual bool Reboot(bool bReconnect = true)
    {
        // Get the firmware timestamp
        ProbeBase::ProbeInformations_t pbi;
        bool bPbiValid = bReconnect && GetInformations(&pbi);

        // Reboot the probe
        Cable.CommandWrite(ProbeCommand_t::Reboot, 0, { 0x05, 0xC0, 0x4B, 0x17 });

        // Try to reconnect it
        if (bPbiValid)
        {
            // Try a few times to find the same probe
            for (int i = 0; i < 10; i++)
            {
                if (this->FindProbe(pbi.Id, pbi.UID)) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            return false;
        }

        return true;
    }

    virtual bool UploadFirmware(const std::string &FirmwareFilename, bool bForceUpgrade, bool bVerbose)
    {
        bool bPbiValid = false;
        ProbeBase::ProbeInformations_t pbi;

        // Is a file specified ?
        bool bUploadFile = !FirmwareFilename.empty();
        if (bUploadFile)
        {
            #ifndef _WIN32
            if (!Util::IsRoot()) { std::cerr << "Need to be root !" << std::endl; return false; }
            #endif

            // Does the firmware file exists ?
            if (!Util::FileExists(FirmwareFilename.c_str()))
                { std::cerr << "Firmware file (" << FirmwareFilename << ") not found !" << std::endl; return false; }

            // Get the firmware timestamp
            bPbiValid = Cable.IsOpen() && GetInformations(&pbi);
            if (!bPbiValid)
            {
                if (bVerbose) std::cerr << "Warning: Can't read Target firmware timestamp !" << std::endl; 
                // Force the upgrade as we won't be able to compare the timestamps
                bForceUpgrade = true;
            }

            // Check if the upgrade is necessary ?
            if (!bForceUpgrade)
            {
                // Get firmware file last modification date
                std::error_code ec;
                auto ft = std::filesystem::last_write_time(FirmwareFilename, ec);
                if (ec) { if (bVerbose) std::cerr << "Can't read Target modification date !"; return false; }
                // Convert timestamp to uint64_t (same format as probe Timestamp64)
                const auto now_sys = std::chrono::system_clock::now();
                const auto now_file = std::filesystem::file_time_type::clock::now();
                auto tp = std::chrono::floor<std::chrono::seconds>(std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - now_file + now_sys));
                std::time_t tt = std::chrono::system_clock::to_time_t(tp);
                std::tm tm{};
                #if defined(_WIN32)
                gmtime_s(&tm, &tt);
                #else
                gmtime_r(&tt, &tm);
                #endif
                uint64_t FileTimestampUTC = static_cast<uint64_t>(1900+tm.tm_year) * 10000000000ULL
                    + static_cast<uint64_t>(tm.tm_mon+1) * 100000000ULL
                    + static_cast<uint64_t>(tm.tm_mday) * 1000000ULL
                    + static_cast<uint64_t>(tm.tm_hour) * 10000ULL
                    + static_cast<uint64_t>(tm.tm_min) * 100ULL
                    + static_cast<uint64_t>(tm.tm_sec);

                // Need an upgrade  (with 30s tolerance) ?
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "Timestamps - File:" << FileTimestampUTC << " Probe:" << pbi.TimestampUTC << std::endl;
                if (((int64_t)FileTimestampUTC - (int64_t)pbi.TimestampUTC) < 30 && !bForceUpgrade)
                    { if (bVerbose) std::cerr << "Probe does not need to be upgraded !" << std::endl; return true; }
            }
        }

        // Switch to bootloader mode
        if (Cable.IsOpen()) // Only when a cable is open
        {
            if (bVerbose) std::cerr << "Switching to bootloader mode..." << std::endl;
            bool bOk = Cable.CommandWrite(ProbeCommand_t::Bootloader, 0, { 0x05, 0xC0, 0x4B, 0x17 });
            if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "CommandWrite done !" << std::endl;
            if (!bOk && HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "Warning: RP2040 did not respond to Bootloader command... trying to upgrade firmware anyway" << std::endl;
            // Continue even in case of an error, just in case the RP2040 is already in bootloader mode

            // Give time to the bootloader
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        else
        {
            std::cerr << "Probe is not initialized... trying to upgrade firmware anyway" << std::endl;
        }

        // If no filename is specified, stop here
        if (!bUploadFile) return true;

        #ifdef _WIN32
        bool bOk = false;
        // Enumerate all volumes
        HANDLE hFind;
        wchar_t volName[MAX_PATH] = L"";
        for (hFind = FindFirstVolumeW(volName, MAX_PATH);
            hFind != INVALID_HANDLE_VALUE;
            hFind = (FindNextVolumeW(hFind, volName, MAX_PATH)) ? hFind : INVALID_HANDLE_VALUE)
        {
            std::wstring volumeGuid = volName; // se termine par '\'

            // Get mount point letter
            DWORD needed = 0;
            GetVolumePathNamesForVolumeNameW(volumeGuid.c_str(), nullptr, 0, &needed);
            std::vector<wchar_t> buf(needed + 1, L'\0');
            std::wstring mountPoint;
            // There can be several mounting point; we take the first one
            if (needed > 0 && GetVolumePathNamesForVolumeNameW(volumeGuid.c_str(), buf.data(), (DWORD)buf.size(), &needed))
                for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1)
                    if (*p) { mountPoint = p; break; }

            // Get volume Label with root path
            std::wstring rootForInfo = mountPoint.empty() ? volumeGuid : mountPoint;
            if (!rootForInfo.empty() && rootForInfo.back() != L'\\') rootForInfo.push_back(L'\\');
            wchar_t Label[MAX_PATH + 1] = L"", fsName[MAX_PATH + 1] = L"";
            DWORD serial = 0, maxCompLen = 0, fsFlags = 0;
            if (!GetVolumeInformationW(rootForInfo.c_str(), Label, MAX_PATH, &serial, &maxCompLen, &fsFlags, fsName, MAX_PATH))
            {
                if (bVerbose) std::cerr << "Can't get volume label !" << std::endl; return false;
            }

            // Check the volume label and the presence of INFO_UF2.TXT
            std::string BootloaderDir; BootloaderDir.reserve(rootForInfo.size());
            for (wchar_t wc : rootForInfo) BootloaderDir.push_back(static_cast<char>(wc));
            if (std::wstring(Label) == L"RPI-RP2" && Util::FileExists((BootloaderDir + "INFO_UF2.TXT").c_str()))
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "INFO_UF2.TXT OK !" << std::endl;
                // Copy the new firmware to the bootloader
                std::ifstream  src(FirmwareFilename, std::ios::binary);
                std::ofstream  dst(BootloaderDir + "/firmware.uf2", std::ios::binary);
                dst << src.rdbuf();
                if (bVerbose) std::cerr << "Firmware copy DONE !" << std::endl;
                // Done !
                bOk = true;
                break;
            }
        }
        FindVolumeClose(hFind);

        #else // _WIN32

        #define RP2040_BOOTLOADER_DIR "./bootloader"

        // Create a bootloader mount point
        if (mkdir(RP2040_BOOTLOADER_DIR, 0755) == -1 && errno != EEXIST)
            { std::cerr << "Can't create " RP2040_BOOTLOADER_DIR " !" << std::endl; return false; }

        // Wait so that the RP2040 has time to start the bootloader
        // Find the corresponding device
        #ifdef __APPLE__
        const char* sDevices[] = { "/dev/disk0s1", "/dev/disk1s1", "/dev/disk2s1", "/dev/disk3s1", "/dev/disk4s1", "/dev/disk5s1", "/dev/disk6s1", "/dev/disk7s1", "/dev/disk8s1", "/dev/disk9s1" };
        const char* mountfs = "msdos";
        #endif
        bool bOk = false;
        for (int iTry = 0; !bOk && iTry < 10; iTry++)
        {
            #ifdef __APPLE__
            // On Mac, we browser all possible devices
            const char* sDevices[] = { "/dev/disk0s1", "/dev/disk1s1", "/dev/disk2s1", "/dev/disk3s1", "/dev/disk4s1", "/dev/disk5s1", "/dev/disk6s1", "/dev/disk7s1", "/dev/disk8s1", "/dev/disk9s1" };
            const char* mountfs = "msdos";
            for (int iDevice = 0; !bOk && iDevice < sizeof(sDevices) / sizeof(sDevices[0]); iDevice++)
            {
                std::string sDevice = sDevices[iDevice];
            #else
            // On Linux, we immediately identify the device with /dev/disk/by-label
            const char* mountfs = "vfat";
            for (int i = 0; i<1; i++)
            {
                std::string sDevice;
                try { sDevice = "/dev/" + std::filesystem::read_symlink("/dev/disk/by-label/RPI-RP2").filename().string(); } catch (...) { break; }
            #endif
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "Trying " << sDevice << "..." << std::endl;
                // Does the device exists ?
                if (access(sDevice.c_str(), F_OK) != 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "Access OK !" << std::endl;
                // Mount this device in bootloader directory (ignore errors in case it's already automounted)
                mount(sDevice.c_str(), RP2040_BOOTLOADER_DIR, mountfs, MS_NOEXEC | MS_SYNCHRONOUS, nullptr);

                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "mount OK !" << std::endl;
                // Verify that this directory is correct
                if (access(RP2040_BOOTLOADER_DIR "/INFO_UF2.TXT", F_OK) == 0)
                {
                    if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "INFO_UF2.TXT OK !" << std::endl;
                    // Copy the new firmware to the bootloader
                    std::ifstream  src(FirmwareFilename, std::ios::binary);
                    std::ofstream  dst(RP2040_BOOTLOADER_DIR "/firmware.uf2", std::ios::binary);
                    dst << src.rdbuf();
                    if (bVerbose) std::cerr << "Firmware copy DONE !" << std::endl;
                    // Done !
                    bOk = true;
                }

                // Unount the device
                bool bOk = false;
                for (int j = 0; j < 100; j++)
                {
                    if (umount(RP2040_BOOTLOADER_DIR) == 0) { bOk = true; break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe))
                {
                    if (bOk) std::cerr << "Device unmounted !" << std::endl;
                    else std::cerr << "Can't unmount device !" << std::endl;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (!bOk)
            std::cerr << "Couldn't find the RP2040 bootloader device !" << std::endl;
        else
            // Give time for the upgrade
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Delete bootloader directory
        remove(RP2040_BOOTLOADER_DIR);
        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "Mountpoint deleted !" << std::endl;
        #endif

        // Re-initialze the cable (that has been disconnect during the upgrade process)
        if (bUploadFile && bPbiValid)
        {
            // Try a few times to find the same probe
            for (int i = 0; i < 10; i++)
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "Trying to reconnect probe " << pbi.Id << "/0x" << std::hex << pbi.UID << std::dec << "..." << std::endl;
                if (this->FindProbe(pbi.Id, pbi.UID)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }

        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) std::cerr << "Exiting UploadFirmware !" << std::endl;
        return bOk;
    }
};


class ProbeTST : public ProbeBase
{
    public:
    typedef struct
    {
        char WifiSSID[32];
        char WifiKey[64];
        uint8_t Keys[4][32];
        uint8_t PublicKey[64];
        uint8_t TOKEN_PRODUCTION[128];
        uint8_t TOKEN_STAGING[128];
        uint32_t TestFlags;
        uint8_t Unused[218];
        uint16_t SecurityInfosCrc;
    } TesterInformations_t;
    typedef enum : uint32_t
    {
        None                = 0x0000,
        IgnoreUSBSpeedTest  = 0x0001,
        IgnoreEthernetTest  = 0x0002,
        IgnoreWifiTest      = 0x0004,
        IgnoreRS232Test     = 0x0008,
        IgnoreTPMTest       = 0x0010,
        IgnoreSwithTest     = 0x0020
    } TestFlags;
    static_assert(sizeof(TesterInformations_t) == 0x300, "Probekeys_t struct has wrong size !");
    
    public:
    bool GetTesterInformations(TesterInformations_t *psi, bool bNeedSecurityInfos)
    {
        std::vector<uint8_t> data = Cable.CommandRead(ProbeCommand_t::ReadTesterInfos, 0, sizeof(TesterInformations_t));
        if (data.size() != sizeof(TesterInformations_t)) return false;
        std::memcpy(psi, data.data(), sizeof(TesterInformations_t));        
        return (bNeedSecurityInfos) ? HasSecurityInfos(psi) : true;
    }
    bool WriteTesterInformations(TesterInformations_t *psi, bool bHasSecurityInfos)
    {
        // Compute CRC
        if (bHasSecurityInfos) psi->SecurityInfosCrc = ComputeSecurityInfosCrc(psi);
        // Store infos
        return Cable.CommandWrite(ProbeCommand_t::WriteTesterInfos, 0, std::vector<uint8_t>((uint8_t*)psi, ((uint8_t*)psi) + sizeof(TesterInformations_t)));
    }
    static bool HasSecurityInfos(TesterInformations_t *Infos)
    {
        uint16_t ExpectedCrc = ProbeTST::ComputeSecurityInfosCrc(Infos);
        return (Infos->SecurityInfosCrc == ExpectedCrc);
    }

    private:
    static uint16_t ComputeSecurityInfosCrc(TesterInformations_t *psi)
    {
        return Util::Crc16((uint8_t*)psi, sizeof(TesterInformations_t) - sizeof(psi->SecurityInfosCrc));
    }
};

class ProbeDMD : public ProbeBase
{
    public:
    typedef enum : uint8_t { AUTO = 0, WPC = 1, SAM = 2, SPIKE = 3, WHITESTAR = 4, DE = 5, SEGA = 6, GOTTLIEB = 7, CAPCOM = 8, ALVING = 9, DE16 = 10 } DmdType_t;
    typedef struct { DmdType_t Type; uint8_t Width, Height, Bpp, Colors; DmdType_t FlashType; uint8_t Flags, FrameFrequency; uint32_t PixelCount, RowdatDuration; } DmdInformations_t;

    private:
    DmdInformations_t Infos;

    protected:
    virtual bool OnInitialized() { return GetDmdInformations(&Infos); }

    public:
    bool IsFrameReady() 
    {
        uint8_t status;
        if (!ReadStatus(status)) return false;
        return (status & 1) != 0;
    }

    bool WaitForFrame(int timeoutMs, int LatencyMs = 20)
    {
        auto now = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now) < std::chrono::milliseconds(timeoutMs))
        {
            if (IsFrameReady()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(LatencyMs));
        }
        return false;
    }

    std::vector<uint8_t> GetFrame(bool bUnpack = false)
    {
        auto PackedFrame = Cable.CommandRead(ProbeCommand_t::ReadDmdFrame, 0, Infos.Width*Infos.Height/2);
        if (!bUnpack) return PackedFrame;
        // Unpack the frame
        std::vector<uint8_t> UnpackedFrame(PackedFrame.size() * 2);
        for (int i = 0; i < PackedFrame.size(); i++)
        {
            UnpackedFrame[i * 2] = (PackedFrame[i] & 0x0f);
            UnpackedFrame[i * 2 + 1] = ((PackedFrame[i] >> 4) & 0x0f);
        }
        return UnpackedFrame;
    }

    static void MinMaxRgb(uint8_t &min, uint8_t &max, uint8_t r, uint8_t g, uint8_t b)
    {
        if (r > g) { max = r; min = g; } else { max = g; min = r; }
        if (b > max) max = b; else if (b < min) min = b;
    }
    static float GetBrightness(uint8_t r, uint8_t g, uint8_t b)
    {
        uint8_t min, max; MinMaxRgb(min, max, r, g, b);
        return ((int)max + (int)min) / (255 * 2.0f);
    }
    static std::vector<uint8_t> ReadOverlay(const std::string& fn)
    {
        // Return an array containing Width, Height and 8-bit Pixel data

        // Erase background ? (filename starting with '-')
        std::string filename = fn;
        bool bEraseBackground = (filename[0] == '-');
        if (bEraseBackground) filename.erase(0, 1);

        #ifdef USE_OPENCV2

        // Open bitmap file
        cv::Mat bmp = cv::imread(filename, cv::IMREAD_UNCHANGED);
        if (bmp.empty()) { std::cerr << "Cannot open bitmap \"" << filename << "\"!" << std::endl; return {}; }
        
        // Resize the bitmap if needed
        //if (Width >= 0 && Width <= 192 && Height >= 0 && Height <= 32)
        //    cv::resize(bmp, bmp, cv::Size(Width, Height));

        // Check the geometry
        int Width = bmp.cols, Height = bmp.rows;
        if (Width > 192 || Height > 64)
            { std::cerr << "Overlay is too large (" << Width << "x" << Height << ") !" << std::endl; return {}; }

        // Convert it to overlay format
        std::vector<uint8_t> overlay; // Unpacked overlay: 1 pixel per byte + transparency bit in MSB

        // Add a header : Width and Height
        overlay.push_back(Width);
        overlay.push_back(Height);

        // Add all pixels
        for (int y = 0; y < Height; ++y)
        {
            for (int x = 0; x < Width; ++x)
            {
                uint8_t brightness = 0;
                bool transparent = false;

                if (bmp.channels() == 4)
                {
                    // BGR+A format
                    cv::Vec4b color = bmp.at<cv::Vec4b>(cv::Point(x, y));
                    brightness = static_cast<uint8_t>(cv::saturate_cast<uint8_t>(color[2] * 0.3 + color[1] * 0.59 + color[0] * 0.11) * 15.0);
                    transparent = (color[3] == 0); // Alpha channel for transparency
                }
                else if (bmp.channels() == 3)
                {
                    // BGR format
                    cv::Vec3b color = bmp.at<cv::Vec3b>(cv::Point(x, y));
                    brightness = static_cast<uint8_t>(cv::saturate_cast<uint8_t>(color[2] * 0.3 + color[1] * 0.59 + color[0] * 0.11) * 15.0);
                    transparent = false; // Assume fully opaque for BGR images
                }
                else if (bmp.channels() == 1)
                {
                    // Grayscale format
                    uint8_t gray = bmp.at<uint8_t>(cv::Point(x, y));
                    brightness = static_cast<uint8_t>(cv::saturate_cast<uint8_t>(gray * 15.0));
                    transparent = false; // Assume fully opaque for grayscale images
                }

                // Add this pixel to the overlay
                overlay.push_back(brightness | (transparent ? 0x80 : 0)); // MSB as transparency bit
            }
        }
        return overlay;

        #else

        // Open bitmap file
        int Width, Height, Channels;
        unsigned char* data = stbi_load(filename.c_str(), &Width, &Height, &Channels, 0);
        if (!data) return {};

        // Check the geometry
        if (Width > 192 || Height > 64)
            { std::cerr << "Overlay is too large (" << Width << "x" << Height << ") !" << std::endl; return {}; }

        // Resize the bitmap if needed
        //std::vector<uint8_t> resizedImage(Width * Height * Channels);
        //if (!stbir_resize_uint8_linear(data, OriginalWidth, OriginalHeight, 0, resizedImage.data(), Width, Height, 0, static_cast<stbir_pixel_layout>(Channels)))
        //    { stbi_image_free(data); return {}; }

        // Convert it to overlay format
        std::vector<uint8_t> overlay; // Unpacked overlay: 1 pixel per byte + transparency bit in MSB

        // Do we need to erase the whole background ?
        int BackgroundWidth, BackgroundHeight;
        if (bEraseBackground)
            { BackgroundWidth = 128; BackgroundHeight = 32; } // Largest possible frame
        else
            { BackgroundWidth = Width; BackgroundHeight = Height; }
        int x0 = (BackgroundWidth - Width) / 2, y0 = (BackgroundHeight - Height) / 2;


        // Add a header : Width and Height
        overlay.push_back((uint8_t)BackgroundWidth);
        overlay.push_back((uint8_t)BackgroundHeight);

        // Add all pixels
        for (int y = 0; y < BackgroundHeight; ++y)
        {
            for (int x = 0; x < BackgroundWidth; ++x)
            {
                uint8_t brightness = 0;
                bool transparent = false;

                // All pixels out of the bitmap are set to black
                if (x < x0 || x >= x0 + Width || y < y0 || y >= y0 + Height) { overlay.push_back(0); continue; }

                if (Channels == 4)
                {
                    // RGBA format
                    uint8_t* color = &data[((y-y0) * Width + (x-x0)) * 4];
                    brightness = (uint8_t)(GetBrightness(color[0], color[1], color[2]) * 15.0);
                    transparent = (color[3] == 0); // Alpha channel for transparency
                }
                else if (Channels == 3)
                {
                    // RGB format
                    uint8_t* color = &data[((y-y0) * Width + (x-x0)) * 3];
                    brightness = (uint8_t)(GetBrightness(color[0], color[1], color[2]) * 15.0);
                    transparent = false; // Assume fully opaque for RGB images
                }
                else if (Channels == 1)
                {
                    // Grayscale format
                    uint8_t gray = data[(y-y0) * Width + (x-x0)];
                    brightness = static_cast<uint8_t>(gray / 17.0);
                    transparent = false; // Assume fully opaque for grayscale images
                }

                // When we want a black background, transparent pixels are replaced with a black one
                if (bEraseBackground && transparent) { brightness = 0; transparent = false; }

                // Add this pixel to the overlay
                overlay.push_back(brightness | (transparent ? 0x80 : 0)); // MSB as transparency bit
            }
        }

        // Free image
        stbi_image_free(data);

        return overlay;

        #endif
    }

    bool SetOverlay(bool bOnOff, uint16_t durationMs = 0, const std::string& filename = "")
    {
        if (!filename.empty())
        {
            // Load overlay (header + data)
            auto overlay = ReadOverlay(filename);
            if (overlay.size() == 0) { std::cerr << "Cannot open bitmap \"" << filename << "\"! (can't open file or wrong geometry)" << std::endl; return false; }

            // Send data to Cable
            Cable.CommandWrite(ProbeCommand_t::WriteDmdOverlay, 0, overlay);

            // If Duration is 0, the overlay is only loaded into the probe
            if (durationMs == 0) return true;

            // Give time for the probe to process this overlay
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Display the overlay
        return Cable.CommandWrite(ProbeCommand_t::WriteDmdOverlayControlReg, 0,
            {
                static_cast<uint8_t>(bOnOff ? 0x01 : 0x00),
                static_cast<uint8_t>(durationMs >> 8),
                static_cast<uint8_t>(durationMs)
            });
    }

    bool SetType(DmdType_t type)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteDmdType, 0, { (uint8_t)type });
        // Warning: Infos should be refreshed after this
    }

    bool GetDmdInformations(DmdInformations_t *Infos, bool bExtended = false)
    {
        int Len = (bExtended) ? 16 : 8;
        auto DmdInfos = Cable.CommandRead(ProbeCommand_t::ReadDmdInfos, 0, Len);
        if (DmdInfos.size() != Len) return false;
        Infos->Type = (DmdType_t)DmdInfos[0];
        Infos->Width = DmdInfos[1];
        Infos->Height = DmdInfos[2];
        Infos->Bpp = DmdInfos[3];
        Infos->Colors = DmdInfos[4];
        Infos->Flags = DmdInfos[5];
        Infos->FlashType = (DmdType_t)DmdInfos[6];
        Infos->FrameFrequency = DmdInfos[7];
        if (bExtended)
        {
            Infos->PixelCount = Util::UInt32FromBuffer(DmdInfos, 8);
            Infos->RowdatDuration = Util::UInt32FromBuffer(DmdInfos, 12);
        }
        else
        {
            Infos->PixelCount = 0;
            Infos->RowdatDuration = 0;
        }
        return true;
    }
};

class ProbeCPU : public ProbeBase
{
    public:

    typedef enum : uint8_t { CPU_NONE = 0xff, CPU6502 = 0, CPU6800, CPU6802, CPU6803, CPU6808, CPU6809, CPU6809E } CpuType_t;
    bool SetType(CpuType_t type, bool bMITM)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteCpuType, 0, { (uint8_t)type, (uint8_t)((bMITM) ? 1 : 0) });
    }
    bool SetTimings(uint32_t delayAfterOE, uint32_t delayAfterHalt, uint32_t delayBusSettle, uint32_t delayAfterClockLow, uint32_t delayAfterClockHigh, bool bWaitClockHigh)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteCpuType, 0,
            {
                0xfe, // Invalid CPU
                0xff, // Invalid bMITM
                (uint8_t)delayAfterOE, (uint8_t)(delayAfterOE >> 8), (uint8_t)(delayAfterOE >> 16), (uint8_t)(delayAfterOE >> 24),
                (uint8_t)delayAfterHalt, (uint8_t)(delayAfterHalt >> 8), (uint8_t)(delayAfterHalt >> 16), (uint8_t)(delayAfterHalt >> 24),
                (uint8_t)delayBusSettle, (uint8_t)(delayBusSettle >> 8), (uint8_t)(delayBusSettle >> 16), (uint8_t)(delayBusSettle >> 24),
                (uint8_t)delayAfterClockLow, (uint8_t)(delayAfterClockLow >> 8), (uint8_t)(delayAfterClockLow >> 16), (uint8_t)(delayAfterClockLow >> 24),
                (uint8_t)delayAfterClockHigh, (uint8_t)(delayAfterClockHigh >> 8), (uint8_t)(delayAfterClockHigh >> 16), (uint8_t)(delayAfterClockHigh >> 24),
                (uint8_t)((bWaitClockHigh) ? 1 : 0),
                0xff, // Invalid NvRamUnlockValue
                0xff, 0xff, // Invalid NvRamLockAddr
                0xff, 0xff, // Invalid NvRamAddr
                0xff, 0xff, // Invalid NvRamLength
                0xff, // Invalid NvRamLockValue
            });
    }    
    bool SetNvRamParameters(uint16_t NvRamAddr, uint16_t NvRamLength, uint16_t NvRamLockAddr, uint8_t NvRamUnlockValue, uint8_t NvRamLockValue)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteCpuType, 0,
            {
                0xfe, // Invalid CPU
                0xff, // Invalid bMITM
                0xff, 0xff, 0xff, 0xff, // Invalid delayAfterOE
                0xff, 0xff, 0xff, 0xff, // Invalid delayAfterHalt
                0xff, 0xff, 0xff, 0xff, // Invalid delayBusSettle
                0xff, 0xff, 0xff, 0xff, // Invalid delayAfterClockLow 
                0xff, 0xff, 0xff, 0xff, // Invalid delayAfterClockHigh
                0xff, //  Invalid bWaitClockHigh
                NvRamUnlockValue, 
                (uint8_t)NvRamLockAddr, (uint8_t)(NvRamLockAddr >> 8),
                (uint8_t)NvRamAddr, (uint8_t)(NvRamAddr >> 8),
                (uint8_t)NvRamLength, (uint8_t)(NvRamLength >> 8),
                NvRamLockValue,
            });
        return false;
    }
    typedef struct
    {
        CpuType_t Type;
        uint8_t Unused1, Unused2, Unused3;
        uint32_t ClockFrequency;
        uint32_t delayAfterOE, delayAfterHalt, delayBusSettle, delayAfterClockLow, delayAfterClockHigh;
        bool bWaitClockHigh, bMITM;
        uint8_t NvRamUnlockValue, NvRamLockValue;
        uint16_t NvRamLockAddr;
        uint16_t NvRamAddr;
        uint16_t NvRamLength;
    } CpuInformations_t;
    bool GetCpuInformations(CpuInformations_t* Infos, bool bExtended = false)
    {
        int Len = (bExtended) ? 35 : 2;
        auto CpuInfos = Cable.CommandRead(ProbeCommand_t::ReadCpuInfos, 0, Len);
        if (CpuInfos.size() != Len) return false;
        Infos->Type = (CpuType_t)CpuInfos[0];
        Infos->bMITM = (CpuInfos[1] != 0);
        if (bExtended)
        {
            Infos->ClockFrequency = Util::UInt32FromBuffer(CpuInfos, 2);
            Infos->delayAfterOE = Util::UInt32FromBuffer(CpuInfos, 6);
            Infos->delayAfterHalt = Util::UInt32FromBuffer(CpuInfos, 10);
            Infos->delayBusSettle = Util::UInt32FromBuffer(CpuInfos, 14);
            Infos->delayAfterClockLow = Util::UInt32FromBuffer(CpuInfos, 18);
            Infos->delayAfterClockHigh = Util::UInt32FromBuffer(CpuInfos, 22);
            Infos->bWaitClockHigh = (CpuInfos[26] != 0);
            Infos->NvRamUnlockValue = CpuInfos[27];
            Infos->NvRamLockAddr = Util::UInt16FromBuffer(CpuInfos, 28);
            Infos->NvRamAddr = Util::UInt16FromBuffer(CpuInfos, 30);
            Infos->NvRamLength = Util::UInt16FromBuffer(CpuInfos, 32);
            Infos->NvRamLockValue = CpuInfos[34];
        }
        else
            Infos->ClockFrequency = 0;
        return true;
    }
    bool Halt(bool bHalt)
    {
        // Halt or UnHalt the CPU
        return Cable.CommandWrite(ProbeCommand_t::WriteCpuControlReg, 0,
            {
                static_cast<uint8_t>(bHalt ? 0x01 : 0x00)
            });
    }
    bool Emulate(bool bEmulate)
    {
        // Halt or UnHalt the CPU
        return Cable.CommandWrite(ProbeCommand_t::EmulateCpu, 0,
            {
                static_cast<uint8_t>(bEmulate ? 0x01 : 0x00)
            });
    }
    std::vector<uint8_t> ReadMemory(uint32_t Address, int Len)
    {
        return Cable.CommandRead(ProbeCommand_t::ReadCpuMemory, Address, Len);
    }
    bool WriteMemory(uint32_t Address, const std::vector<uint8_t>& Buffer)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteCpuMemory, Address, Buffer);
    }
};

#ifdef USE_PINMAME // Only used by the Spb tool
class ProbePinMAME : public ProbeCPU
{
    public:
    typedef struct
    {
        uint8_t Unused[0x80];
    } PinMAMEInformations_t;
    bool GetPinMAMEInformations(PinMAMEInformations_t* Infos)
    {
        int Len = sizeof(PinMAMEInformations_t);
        auto PinMAMEInfos = Cable.CommandRead(ProbeCommand_t::ReadPinMAMEInfos, 0, Len);
        if (PinMAMEInfos.size() != Len) return false;
        std::memcpy(Infos->Unused, PinMAMEInfos.data(), Len);
        return true;
    }
};
#endif

class ProbeNFC : public ProbeBase
{
    public:
    typedef enum : uint8_t { READER = 0, TAG = 1 } NfcType_t;

    typedef struct
    {
        NfcType_t Type;
        uint8_t Brightness, Capacitance, bFactoryMode;
        uint32_t LastId;
        std::string TagUri;
        uint16_t RSSI;
    } NfcInformations_t;
    bool GetNfcInformations(NfcInformations_t* Infos)
    {
        int Len = 8+256+2;
        auto NfcInfos = Cable.CommandRead(ProbeCommand_t::ReadNfcInfos, 0, Len);
        if (NfcInfos.size() != Len) return false;
        Infos->Type = (NfcType_t)NfcInfos[0];
        Infos->Brightness = NfcInfos[1];
        Infos->Capacitance = NfcInfos[2];
        Infos->bFactoryMode = NfcInfos[3];
        Infos->LastId = Util::UInt32FromBuffer(NfcInfos, 4);

        int UriLen = (int)strnlen((const char*)&NfcInfos[8], 255);
        Infos->TagUri = std::string((const char *)&NfcInfos[8], UriLen);
        Infos->RSSI = Util::UInt16FromBuffer(NfcInfos, 264);
        return true;
    }

    bool SetLedsCode(uint32_t Code)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcLedsAnimation, 0, { (uint8_t)(Code >> 0), (uint8_t)(Code >> 8), (uint8_t)(Code >> 16), (uint8_t)(Code >> 24) });
    }
    bool SetLedsAnimation(const std::vector<uint8_t> &AnimationStruct)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcLedsAnimation, 0, AnimationStruct);
    }
    bool SetType(NfcType_t type)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcType, 0, { type });
    }
    bool SetLedsBrightness(uint8_t Brightness)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcType, 0, { 0xff /*Invalid Nfc type*/, Brightness, 0xff /* Invalid Factory test mode */, 0});
    }
    bool SetFactoryMode(bool bEnabled = true)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcType, 0, { 0xff /*Invalid Nfc type*/, 0xff /* Invalid brightness */, (uint8_t)(bEnabled ? 1:0), 0});
    }
    bool SetUri(std::string uri)
    {
        std::vector<uint8_t> data;
        data.push_back(0xff); // Invalid Nfc type
        data.push_back(0xff); // Invalid Leds brightness
        data.push_back(0xff); // Invalid Factory test mode
        data.push_back((uint8_t)uri.length()); // URI length
        data.insert(data.end(), uri.begin(), uri.end()); // URI content
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcType, 0, data);
    }
};
