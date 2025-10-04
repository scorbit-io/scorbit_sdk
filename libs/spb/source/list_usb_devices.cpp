/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#include "list_usb_devices.h"
// #include <logger/logger.h>

// FIXME: add logger
#define INF(...)                                                                                     \
    while (false) { }
#define ERR(...) INF()

#if defined(_WIN32)

#    include <windows.h>
#    include <initguid.h>
#    include <devguid.h>
#    include <setupapi.h>
#    include <tchar.h>
#    include <regex>
#    pragma comment(lib, "setupapi.lib")
#else
#    include <filesystem>

namespace fs = std::filesystem;
#endif

namespace spb {

std::vector<std::string> listUsbDevices()
{
#if defined(_WIN32)
    std::vector<std::string> devices;

    HDEVINFO deviceInfoSet =
            SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);

    if (deviceInfoSet == INVALID_HANDLE_VALUE)
        return devices;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    static const std::regex comRegex(R"(COM\d+)", std::regex_constants::icase);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &devInfoData); ++i) {
        char buffer[256];
        if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &devInfoData, SPDRP_FRIENDLYNAME,
                                              nullptr, reinterpret_cast<PBYTE>(buffer),
                                              sizeof(buffer), nullptr)) {
            std::string name(buffer);
            std::smatch match;
            if (std::regex_search(name, match, comRegex)) {
                devices.push_back(match.str());
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return devices;
#else
    std::vector<std::string> devices;

#    if defined(__APPLE__)
    // Apple
    const std::string prefix = "cu.usbmodem";
#    else
    // Linux
    const std::string prefix = "ttyACM";
#    endif
    const std::string path = "/dev";
    try {
        for (const auto &entry : fs::directory_iterator(path)) {
            if (entry.is_character_file()) {
                const std::string filename = entry.path().filename().string();
                if (filename.find(prefix, 0) == 0) {
                    devices.push_back(entry.path().string());
                }
            }
        }
    } catch (const fs::filesystem_error &e) {
        ERR("Filesystem error: {}", e.what());
    }

    return devices;
#endif
}

} // namespace spb
