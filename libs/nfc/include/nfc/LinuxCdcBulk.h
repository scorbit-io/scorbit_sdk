// --------------------------------------------------------------------
//  Project:           Scorbitron SLB
//  Description:       USB CDC ACM bulk I/O via libusb (Linux, no ttyACM)
// --------------------------------------------------------------------

#pragma once

#if defined(__linux__)

#include "ListUsbDevices.h"
#include "Util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// One CDC ACM pair (comm + data) via libusb. Path: "usb:<bus>:<addr>:<dataIface>"
// e.g. probe data If 1 → usb:1:3:1 ; TPM data If 3 → usb:1:3:3
class LinuxCdcBulk
{
    libusb_device_handle* m_handle = nullptr;
    int m_ifData = -1;
    int m_ifComm = -1;
    unsigned char m_epIn = 0;
    unsigned char m_epOut = 0;
    int m_maxPacket = 64;
    std::vector<uint8_t> m_rxBuf;
    size_t m_rxPos = 0;
    uint32_t m_baud = 921600;
    std::string m_path;

public:
    LinuxCdcBulk() = default;
    LinuxCdcBulk(const LinuxCdcBulk&) = delete;
    LinuxCdcBulk& operator=(const LinuxCdcBulk&) = delete;
    ~LinuxCdcBulk() { close(); }

    bool isOpen() const { return m_handle != nullptr; }

    bool open(const std::string& path, uint32_t baud = 921600)
    {
        close();
        m_baud = baud;
        m_path = path;

        unsigned bus = 0, addr = 0, iface = 0;
        if (sscanf(path.c_str(), "usb:%u:%u:%u", &bus, &addr, &iface) != 3)
            return false;

        libusb_context* ctx = probeLibusbContext();
        if (!ctx)
            return false;

        libusb_device** list = nullptr;
        ssize_t n = libusb_get_device_list(ctx, &list);
        if (n < 0)
            return false;

        libusb_device* found = nullptr;
        for (ssize_t i = 0; i < n; i++)
        {
            if (libusb_get_bus_number(list[i]) != bus || libusb_get_device_address(list[i]) != addr)
                continue;
            libusb_device_descriptor desc{};
            if (libusb_get_device_descriptor(list[i], &desc) != 0)
                continue;
            if (desc.idVendor != kProbeUsbVid || !isProbeUsbPid(desc.idProduct))
                continue;
            found = list[i];
            break;
        }
        if (!found)
        {
            libusb_free_device_list(list, 1);
            return false;
        }

        libusb_config_descriptor* cfg = nullptr;
        if (libusb_get_active_config_descriptor(found, &cfg) != 0)
        {
            libusb_free_device_list(list, 1);
            return false;
        }

        int commPrefer = -1, commAny = -1;
        unsigned char epIn = 0, epOut = 0;
        int maxPacket = 64;
        bool dataFound = false;
        for (int i = 0; i < cfg->bNumInterfaces; i++)
        {
            if (cfg->interface[i].num_altsetting < 1)
                continue;
            const libusb_interface_descriptor& alt = cfg->interface[i].altsetting[0];
            if (alt.bInterfaceClass == LIBUSB_CLASS_COMM && alt.bInterfaceSubClass == 2)
            {
                commAny = alt.bInterfaceNumber;
                if (alt.bInterfaceNumber + 1 == (int)iface)
                    commPrefer = alt.bInterfaceNumber;
            }
            if (alt.bInterfaceNumber != (int)iface || alt.bInterfaceClass != LIBUSB_CLASS_DATA)
                continue;
            dataFound = true;
            for (int e = 0; e < alt.bNumEndpoints; e++)
            {
                const libusb_endpoint_descriptor& ep = alt.endpoint[e];
                if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
                    continue;
                if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN)
                {
                    epIn = ep.bEndpointAddress;
                    maxPacket = ep.wMaxPacketSize & 0x7FF;
                }
                else
                    epOut = ep.bEndpointAddress;
            }
        }
        const int commIface = (commPrefer >= 0) ? commPrefer : commAny;
        libusb_free_config_descriptor(cfg);

        if (!dataFound || !epIn || !epOut)
        {
            libusb_free_device_list(list, 1);
            return false;
        }

        libusb_device_handle* handle = nullptr;
        int err = libusb_open(found, &handle);
        libusb_free_device_list(list, 1);
        if (err != 0)
        {
            if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
                ERR("Can't open USB device %s (%s)\n", path.c_str(), libusb_error_name(err));
            return false;
        }

        // Do not detach cdc-acm: this path is only used when ttyACM is absent
        int claimedComm = -1;
        if (commIface >= 0 && commIface != (int)iface)
        {
            if (libusb_claim_interface(handle, commIface) == 0)
                claimedComm = commIface;
        }

        err = libusb_claim_interface(handle, (int)iface);
        if (err != 0)
        {
            if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
                ERR("Can't claim CDC interface %u (%s)\n", iface, libusb_error_name(err));
            if (claimedComm >= 0)
                libusb_release_interface(handle, claimedComm);
            libusb_close(handle);
            return false;
        }

        if (claimedComm >= 0)
        {
            uint8_t lineCoding[7] = {
                (uint8_t)m_baud, (uint8_t)(m_baud >> 8), (uint8_t)(m_baud >> 16), (uint8_t)(m_baud >> 24),
                0, 0, 8
            };
            int cr = libusb_control_transfer(handle, 0x21, 0x20, 0, (uint16_t)claimedComm, lineCoding, 7, 1000);
            if (cr < 0 && HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
                ERR("SET_LINE_CODING failed (%s)\n", libusb_error_name(cr));
            cr = libusb_control_transfer(handle, 0x21, 0x22, 0x0003, (uint16_t)claimedComm, nullptr, 0, 1000);
            if (cr < 0 && HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
                ERR("SET_CONTROL_LINE_STATE failed (%s)\n", libusb_error_name(cr));
        }

        m_handle = handle;
        m_ifData = (int)iface;
        m_ifComm = claimedComm;
        m_epIn = epIn;
        m_epOut = epOut;
        m_maxPacket = (maxPacket > 0) ? maxPacket : 64;
        m_rxBuf.clear();
        m_rxPos = 0;
        return true;
    }

    void close()
    {
        if (!m_handle)
            return;
        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
            INF("Closing USB CDC %s\n", m_path.c_str());
        if (m_ifData >= 0)
            libusb_release_interface(m_handle, m_ifData);
        if (m_ifComm >= 0 && m_ifComm != m_ifData)
            libusb_release_interface(m_handle, m_ifComm);
        libusb_close(m_handle);
        m_handle = nullptr;
        m_ifData = -1;
        m_ifComm = -1;
        m_epIn = 0;
        m_epOut = 0;
        m_maxPacket = 64;
        m_rxBuf.clear();
        m_rxPos = 0;
        m_path.clear();
    }

    void flush()
    {
        m_rxBuf.clear();
        m_rxPos = 0;
        if (!m_handle || !m_epIn)
            return;
        const int xferSize = m_maxPacket > 0 ? m_maxPacket : 64;
        std::vector<uint8_t> tmp(xferSize);
        for (int i = 0; i < 64; i++)
        {
            int transferred = 0;
            int r = libusb_bulk_transfer(m_handle, m_epIn, tmp.data(), xferSize, &transferred, 1);
            if (r < 0 || transferred <= 0)
                break;
        }
    }

    bool write(const void* data, int len)
    {
        if (!m_handle || !data || len < 0)
            return false;
        auto* p = static_cast<uint8_t*>(const_cast<void*>(data));
        int remaining = len;
        while (remaining > 0)
        {
            int transferred = 0;
            int r = libusb_bulk_transfer(m_handle, m_epOut, p, remaining, &transferred, 1000);
            if (r < 0 || transferred <= 0)
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
                    ERR("USB write error (%s)\n", r < 0 ? libusb_error_name(r) : "short");
                return false;
            }
            p += transferred;
            remaining -= transferred;
        }
        return true;
    }

    bool write(const std::vector<uint8_t>& buffer)
    {
        return write(buffer.data(), (int)buffer.size());
    }

    // At most maxLen bytes, wait up to timeoutMs (like select+read)
    int readSome(void* buf, int maxLen, int timeoutMs)
    {
        if (!m_handle || !buf || maxLen <= 0)
            return -1;
        uint8_t* out = static_cast<uint8_t*>(buf);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        const int xferSize = (m_maxPacket > 0) ? std::max(m_maxPacket, 512 - (512 % m_maxPacket)) : 64;
        while (true)
        {
            if (m_rxPos < m_rxBuf.size())
            {
                const int n = std::min(maxLen, (int)(m_rxBuf.size() - m_rxPos));
                memcpy(out, m_rxBuf.data() + m_rxPos, (size_t)n);
                m_rxPos += (size_t)n;
                return n;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                return 0;
            const int remainingMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

            m_rxBuf.resize((size_t)xferSize);
            m_rxPos = 0;
            int transferred = 0;
            int r = libusb_bulk_transfer(m_handle, m_epIn, m_rxBuf.data(), xferSize, &transferred, std::max(1, remainingMs));
            if (transferred > 0)
            {
                m_rxBuf.resize((size_t)transferred);
                continue;
            }
            m_rxBuf.clear();
            if (r < 0 && r != LIBUSB_ERROR_TIMEOUT)
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
                    ERR("USB read error (%s)\n", libusb_error_name(r));
                return -1;
            }
            return 0;
        }
    }

    // Exactly `count` bytes or {}
    std::vector<uint8_t> readExact(int count, int timeoutMs)
    {
        if (!m_handle || count <= 0)
            return {};
        std::vector<uint8_t> buffer((size_t)count);
        int offset = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        // Always IN a multiple of wMaxPacketSize. A short host buffer truncates the
        // USB packet (ttyACM would have kept the leftover bytes).
        const int xferSize = (m_maxPacket > 0) ? std::max(m_maxPacket, 512 - (512 % m_maxPacket)) : 64;
        while (count > 0)
        {
            if (m_rxPos < m_rxBuf.size())
            {
                const int n = std::min(count, (int)(m_rxBuf.size() - m_rxPos));
                memcpy(buffer.data() + offset, m_rxBuf.data() + m_rxPos, n);
                m_rxPos += (size_t)n;
                offset += n;
                count -= n;
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
                    ERR("Timeout waiting for data...\n");
                return {};
            }
            const int remainingMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            const int chunkTimeout = std::min(10, std::max(1, remainingMs));

            m_rxBuf.resize((size_t)xferSize);
            m_rxPos = 0;
            int transferred = 0;
            int r = libusb_bulk_transfer(m_handle, m_epIn, m_rxBuf.data(), xferSize, &transferred, chunkTimeout);
            if (transferred > 0)
            {
                m_rxBuf.resize((size_t)transferred);
                continue;
            }
            m_rxBuf.clear();
            if (r < 0 && r != LIBUSB_ERROR_TIMEOUT)
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable))
                    ERR("USB read error (%s)\n", libusb_error_name(r));
                return {};
            }
        }
        return buffer;
    }
};

#endif
