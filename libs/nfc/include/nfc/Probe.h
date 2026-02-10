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
#include "Util.h"
#include "Cable.h"

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

    WriteNfcLedsAnimation = 0x43,
    ReadNfcInfos = 0x46,
    WriteNfcInfos = 0x4b,

    WritePinMAMEControl = 0xd3,
    ReadPinMAMEInfos = 0xd6,


    Reboot = 0xf3,
    Bootloader = 0xf7,
    TriggerWatchdog = 0xfb
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
                ERR("Initializing cable with DeviceIndex=%d\n", DeviceIndex);
            else
                ERR("Initializing cable with DeviceName=%s\n", DeviceName.c_str());
        }
        Cable.Close();

        if (!DeviceName.empty() && !Cable.Initialize(DeviceName))
        {
            if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe))
                ERR("Device \"%s\" can't be initialized !\n", DeviceName.c_str());
            return false;
        }
        if (DeviceName.empty() && !Cable.Initialize(DeviceIndex))
        {
            if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe))
            {
                if (DeviceIndex >= 0)
                    ERR("Device #%d can't be initialized !\n", DeviceIndex);
                else
                    ERR("Default device can't be initialized !\n");
            }
            return false;
        }

        // Perform any initialization needed after the probe is connected
        return OnInitialized();
    }

    typedef enum { Unknown = 0, PowerOnReset, BootloaderRestart, SoftRestart, WatchdogTimer, WatchdogTimerAcknoledged } BootReason_t;
    typedef struct
    { 
        int DeviceIndex;

        uint16_t Magic;
        std::string Id, Name; 
        int VersionMajor, VersionMinor, VersionRevision; 
        uint32_t TimestampHeader;
        uint64_t TimestampUTC;
        std::string Timestamp;  
        BootReason_t BootReason;
        uint8_t WatchdogCount;
        long TimeMs;
        uint32_t SysClockFrequency;
        uint8_t UsbFlags;
        uint8_t Reserved3ForAlignment;
        uint32_t UID;
        uint8_t TpmSerial[9];
        uint8_t TpmType; // 0:None, 1:Hard, 2:Soft
        uint8_t TpmUUID[16];
        uint16_t Reserved4ForAlignment;
        uint64_t TpmScorbitSerial;
        uint8_t Reserved5[24];
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
            pbi->BootReason = BootReason_t::Unknown; // Unknown
            pbi->WatchdogCount = 0;                  // Unknown
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

            // Get probe flags
            pbi->BootReason = (BootReason_t)data[32];
            pbi->WatchdogCount = data[33];
        }
        pbi->TimeMs = Util::UInt32FromBuffer(data, 54);
        pbi->SysClockFrequency = Util::UInt32FromBuffer(data, 58);
        pbi->UsbFlags = data[62];
        pbi->Reserved3ForAlignment = data[63];
        pbi->UID = Util::UInt32FromBuffer(data, 64);
        std::memcpy(pbi->TpmSerial, data.data() + 68, sizeof(pbi->TpmSerial));
        pbi->TpmType = data[77];
        std::memcpy(pbi->TpmUUID, data.data() + 78, sizeof(pbi->TpmUUID));
        pbi->Reserved4ForAlignment = Util::UInt16FromBuffer(data, 94);
        pbi->TpmScorbitSerial = Util::UInt64FromBuffer(data, 96);
        std::memcpy(pbi->Reserved5, data.data() + 104, sizeof(pbi->Reserved5));

        return true;
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

    virtual bool TriggerWatchdog()
    {
        // Reboot the probe
        return Cable.CommandWrite(ProbeCommand_t::TriggerWatchdog, 0, { 0x05, 0xC0, 0x4B, 0x17 });
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
            if (!Util::IsRoot()) { ERR("Need to be root !\n"); return false; }
            #endif

            // Does the firmware file exists ?
            if (!Util::FileExists(FirmwareFilename.c_str()))
                { ERR("Firmware file \"%s\" not found !\n", FirmwareFilename.c_str()); return false; }

            // Get the firmware timestamp
            bPbiValid = Cable.IsOpen() && GetInformations(&pbi);
            if (!bPbiValid)
            {
                if (bVerbose) ERR("Warning: Can't read Target firmware timestamp !\n");
                // Force the upgrade as we won't be able to compare the timestamps
                bForceUpgrade = true;
            }

            // Check if the upgrade is necessary ?
            if (!bForceUpgrade)
            {
                // Get firmware file last modification date
                std::error_code ec;
                auto ft = std::filesystem::last_write_time(FirmwareFilename, ec);
                if (ec) { if (bVerbose) ERR("Can't read Target modification date !\n"); return false; }
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
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("Timestamps - File:%lld Probe:%lld\n", FileTimestampUTC, pbi.TimestampUTC);
                if (((int64_t)FileTimestampUTC - (int64_t)pbi.TimestampUTC) < 30 && !bForceUpgrade)
                    { if (bVerbose) INF("Probe does not need to be upgraded !\n"); return true; }
            }
        }

        // Switch to bootloader mode
        if (Cable.IsOpen()) // Only when a cable is open
        {
            if (bVerbose) INF("Switching to bootloader mode...\n");
            bool bOk = Cable.CommandWrite(ProbeCommand_t::Bootloader, 0, { 0x05, 0xC0, 0x4B, 0x17 });
            if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("CommandWrite done !\n");
            if (!bOk && HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) WRN("Warning: RP2040 did not respond to Bootloader command... trying to upgrade firmware anyway\n");
            // Continue even in case of an error, just in case the RP2040 is already in bootloader mode

            // Give time to the bootloader
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        else
        {
            WRN("Probe is not initialized... trying to upgrade firmware anyway\n");
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
                if (bVerbose) ERR("Can't get volume label !\n"); return false;
            }

            // Check the volume label and the presence of INFO_UF2.TXT
            std::string BootloaderDir; BootloaderDir.reserve(rootForInfo.size());
            for (wchar_t wc : rootForInfo) BootloaderDir.push_back(static_cast<char>(wc));
            if (std::wstring(Label) == L"RPI-RP2" && Util::FileExists((BootloaderDir + "INFO_UF2.TXT").c_str()))
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("INFO_UF2.TXT OK !\n");
                // Copy the new firmware to the bootloader
                std::ifstream  src(FirmwareFilename, std::ios::binary);
                std::ofstream  dst(BootloaderDir + "/firmware.uf2", std::ios::binary);
                dst << src.rdbuf();
                if (bVerbose) INF("Firmware copy DONE !\n");
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
            { ERR("Can't create %s !\n", RP2040_BOOTLOADER_DIR); return false; }

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
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("Trying %s...", sDevice.c_str());
                // Does the device exists ?
                if (access(sDevice.c_str(), F_OK) != 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("Access OK !\n");
                // Mount this device in bootloader directory (ignore errors in case it's already automounted)
                mount(sDevice.c_str(), RP2040_BOOTLOADER_DIR, mountfs, MS_NOEXEC | MS_SYNCHRONOUS, nullptr);

                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("mount OK !\n");
                // Verify that this directory is correct
                if (access(RP2040_BOOTLOADER_DIR "/INFO_UF2.TXT", F_OK) == 0)
                {
                    if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("INFO_UF2.TXT OK !\n");
                    // Copy the new firmware to the bootloader
                    std::ifstream  src(FirmwareFilename, std::ios::binary);
                    std::ofstream  dst(RP2040_BOOTLOADER_DIR "/firmware.uf2", std::ios::binary);
                    dst << src.rdbuf();
                    if (bVerbose) INF("Firmware copy DONE !\n");
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
                    if (bOk) INF("Device unmounted !\n");
                    else ERR("Can't unmount device !\n");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (!bOk)
            ERR("Couldn't find the RP2040 bootloader device !\n");
        else
            // Give time for the upgrade
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Delete bootloader directory
        remove(RP2040_BOOTLOADER_DIR);
        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("Mountpoint deleted !\n");
        #endif
        // Re-initialze the cable (that has been disconnect during the upgrade process)
        if (bUploadFile && bPbiValid)
        {
            // Try a few times to find the same probe
            for (int i = 0; i < 10; i++)
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("Trying to reconnect probe %s/0x%X...\n", pbi.Id.c_str(), pbi.UID);
                if (this->FindProbe(pbi.Id, pbi.UID))
                    { INF("Successfully reconnected probe %s/0x%X...\n", pbi.Id.c_str(), pbi.UID); break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }

        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugProbe)) INF("Exiting UploadFirmware !\n");
        return bOk;
    }
};



class ProbeNFC : public ProbeBase
{
    public:
    typedef enum : uint8_t { READER = 0, TAG = 1 } NfcType_t;
    typedef enum : uint8_t { None=0x00, Tag=0x01, FactoryMode = 0x02, Ext_LM = 0x04 } NfcFlags_t;

    typedef struct
    {
        NfcFlags_t Flags;
        uint8_t Brightness, Capacitance;
        uint8_t Unused;
        uint32_t LastId;
        std::string TagUri;
        uint16_t RSSI;
    } NfcInformations_t;
    bool GetNfcInformations(NfcInformations_t* Infos)
    {
        int Len = 8+256+2;
        auto NfcInfos = Cable.CommandRead(ProbeCommand_t::ReadNfcInfos, 0, Len);
        if (NfcInfos.size() != Len) return false;
        Infos->Flags = (NfcFlags_t)NfcInfos[0];
        Infos->Brightness = NfcInfos[1];
        Infos->Capacitance = NfcInfos[2];
        Infos->Unused = NfcInfos[3];
        Infos->LastId = Util::UInt32FromBuffer(NfcInfos, 4);

        int UriLen = (int)strnlen((const char*)&NfcInfos[8], 255);
        Infos->TagUri = std::string((const char *)&NfcInfos[8], UriLen);
        Infos->RSSI = Util::UInt16FromBuffer(NfcInfos, 264);
        return true;
    }
    bool SetLedsAnimation(const std::vector<uint8_t> &AnimationStruct)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcLedsAnimation, 0, AnimationStruct);
    }
    bool SetType(NfcType_t Type) { return SetFlags(NfcFlags_t::Tag, (Type==NfcType_t::TAG) ? NfcFlags_t::Tag : NfcFlags_t::None); }
    bool SetFlags(uint8_t Mask, NfcFlags_t Flags)
    {
        // Mask specifies which bits are significant in Flags
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcInfos, 0, { Mask, Flags, 0xff /* Invalid brighness */, 0 /* Uri Length */});
    }
    bool SetLedsBrightness(uint8_t Brightness)
    {
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcInfos, 0, { 0x00 /*No Flags*/, 0x00 /*Flags*/, Brightness, 0});
    }
    bool SetUri(std::string uri)
    {
        std::vector<uint8_t> data;
        data.push_back(0x00); // No Flags
        data.push_back(0x00); // Flags
        data.push_back(0xff); // Invalid Leds brightness
        data.push_back((uint8_t)uri.length()); // URI length
        data.insert(data.end(), uri.begin(), uri.end()); // URI content
        return Cable.CommandWrite(ProbeCommand_t::WriteNfcInfos, 0, data);
    }
};
