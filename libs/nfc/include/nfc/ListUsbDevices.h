/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#pragma once

#include <string>
#include <vector>
#include <algorithm>

/**
 * Lists USB devices based on the platform.
 * On Windows, returns vector of COM<N> ports
 * On MacOS, returns vector of devices starting with "/dev/cu.usbmodem".
 * On Linux, returns vector of devices starting with "/dev/ttyACM".
 * If no ttyACM is present (kernel without cdc-acm), Linux also returns
 * Probe CDC interfaces as "usb:<bus>:<addr>:<iface>"
 * (VID 0xCAFE, PID 0x4000 | CDC count: 0x4001 = 1 CDC, 0x4002 = 2 CDCs, …).
 *
 * @return A vector of strings containing the paths of the USB devices.
 */

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
#    include <cstdint>
#    if defined(__linux__)
#        include <cstdio>
#        if defined(__has_include)
#            if __has_include(<libusb-1.0/libusb.h>)
#                include <libusb-1.0/libusb.h>
#            else
#                include <libusb.h>
#            endif
#        else
#            include <libusb-1.0/libusb.h>
#        endif
#    endif
#endif

#if defined(__linux__)
constexpr uint16_t kProbeUsbVid = 0xCAFE;
// TinyUSB: USB_PID = 0x4000 | CFG_TUD_CDC
constexpr uint16_t kProbeUsbPidBase = 0x4000;

inline bool isProbeUsbPid(uint16_t pid)
{
    return (pid & 0xFFF0) == kProbeUsbPidBase && (pid & 0x000F) != 0;
}

inline libusb_context* probeLibusbContext()
{
    // Use RAII to free the context when the process exits
    struct LibusbContext
    {
        libusb_context* ctx = nullptr;

        LibusbContext() = default;

        ~LibusbContext()
        {
            if (ctx)
                libusb_exit(ctx);
        }

        libusb_context* get()
        {
            if (!ctx && libusb_init(&ctx) < 0)
                ctx = nullptr;
            return ctx;
        }

        LibusbContext(const LibusbContext&) = delete;
        LibusbContext& operator=(const LibusbContext&) = delete;
    };

    // Use a static context to prevent multiple context allocation after a successful one
    static LibusbContext context;
    return context.get();
}

// CDC Data interfaces of Probe devices, as "usb:BBB:AAA:I"
inline std::vector<std::string> listLibusbProbeCdcPorts()
{
    std::vector<std::string> devices;
    libusb_context* ctx = probeLibusbContext();
    if (!ctx)
        return devices;

    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(ctx, &list);
    if (n < 0)
        return devices;

    for (ssize_t i = 0; i < n; i++)
    {
        libusb_device* dev = list[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) != 0)
            continue;
        if (desc.idVendor != kProbeUsbVid || !isProbeUsbPid(desc.idProduct))
            continue;

        libusb_config_descriptor* cfg = nullptr;
        if (libusb_get_active_config_descriptor(dev, &cfg) != 0)
            continue;

        const uint8_t bus = libusb_get_bus_number(dev);
        const uint8_t addr = libusb_get_device_address(dev);

        for (int ifn = 0; ifn < cfg->bNumInterfaces; ifn++)
        {
            const libusb_interface& intf = cfg->interface[ifn];
            if (intf.num_altsetting < 1)
                continue;
            const libusb_interface_descriptor& alt = intf.altsetting[0];
            if (alt.bInterfaceClass != LIBUSB_CLASS_DATA)
                continue;

            bool hasIn = false, hasOut = false;
            for (int e = 0; e < alt.bNumEndpoints; e++)
            {
                const libusb_endpoint_descriptor& ep = alt.endpoint[e];
                if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
                    continue;
                if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN)
                    hasIn = true;
                else
                    hasOut = true;
            }
            if (!hasIn || !hasOut)
                continue;

            char path[32];
            snprintf(path, sizeof(path), "usb:%03u:%03u:%u", (unsigned)bus, (unsigned)addr,
                     (unsigned)alt.bInterfaceNumber);
            devices.push_back(path);
        }
        libusb_free_config_descriptor(cfg);
    }

    libusb_free_device_list(list, 1);
    return devices;
}
#endif

inline std::vector<std::string> listUsbDevices()
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
    std::sort(devices.begin(), devices.end());
    return devices;
#else // __APPLE__ || __LINUX__
    std::vector<std::string> devices;
    namespace fs = std::filesystem;
    
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

#    if defined(__linux__)
    // No cdc-acm: talk to Probe CDC bulk endpoints through libusb
    if (devices.empty())
    {
        auto usb = listLibusbProbeCdcPorts();
        devices.insert(devices.end(), usb.begin(), usb.end());
    }
#    endif

    std::sort(devices.begin(), devices.end());
    return devices;
#endif
}
