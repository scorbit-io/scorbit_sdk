// --------------------------------------------------------------------
//  Project:           Scorbitron
//  Description:       TPM through USB CDC (TPM on NFC probe)
//                     Discovers the TPM CDC port by sending
//                     "board:device(00)" and checking for "ECC508".
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2026-01-29	| Initial version (HID)
// V2.0	|                   | 2026-04-02	| CDC serial transport
// V2.1	|                   | 2026-04-05	| Use board:device for discovery
// --------------------------------------------------------------------

#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#endif

#include "Util.h"
#include "ListUsbDevices.h"

class CdcTpm
{
    private:

    #if defined(_WIN32)
    HANDLE hSerial = INVALID_HANDLE_VALUE;
    #else
    int fd_serial = -1;
    #endif

    std::string m_devicePath;

    uint16_t Crc16(const uint8_t* Data, size_t Len)
    {
        const uint16_t polynom = 0x8005;
        uint16_t crc = 0;
        for (size_t i = 0; i < Len; i++)
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

    int ParseTpmResponse(const char* rx, uint8_t* outData, size_t outDataCapacity)
    {
        if (!rx || !outData || outDataCapacity == 0) return 0;

        const char* start = strchr(rx, '(');
        if (!start) return 0;
        start++;

        int count = 0;
        while (*start != ')' && *start != '\0')
        {
            if (count >= static_cast<int>(outDataCapacity)) return 0;
            if (sscanf(start, "%02hhX", &outData[count++]) != 1) return 0;
            start += 2;
        }
        return count;
    }

    bool SerialWrite(const void *data, size_t len)
    {
        #ifdef _WIN32
        if (hSerial == INVALID_HANDLE_VALUE) return false;
        DWORD written;
        return WriteFile(hSerial, data, (DWORD)len, &written, NULL) && written == len;
        #else
        if (fd_serial < 0) return false;
        return write(fd_serial, data, len) == (ssize_t)len;
        #endif
    }

    int SerialRead(void *buf, size_t maxLen, int timeoutMs)
    {
        #ifdef _WIN32
        if (hSerial == INVALID_HANDLE_VALUE) return -1;
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadTotalTimeoutConstant = timeoutMs;
        timeouts.ReadIntervalTimeout = 10;
        SetCommTimeouts(hSerial, &timeouts);
        DWORD bytesRead = 0;
        if (!ReadFile(hSerial, buf, (DWORD)maxLen, &bytesRead, NULL)) return -1;
        return (int)bytesRead;
        #else
        if (fd_serial < 0) return -1;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_serial, &fds);
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int ret = select(fd_serial + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) return ret;
        return (int)read(fd_serial, buf, maxLen);
        #endif
    }

    void SerialFlush()
    {
        uint8_t tmp[64];
        while (SerialRead(tmp, sizeof(tmp), 1) > 0) {}
    }

    void SerialClose()
    {
        #ifdef _WIN32
        if (hSerial != INVALID_HANDLE_VALUE) { CloseHandle(hSerial); hSerial = INVALID_HANDLE_VALUE; }
        #else
        if (fd_serial >= 0) { close(fd_serial); fd_serial = -1; }
        #endif
    }

    bool SerialOpen(const std::string &path)
    {
        #ifdef _WIN32
        std::string winPath = "\\\\.\\" + path;
        hSerial = CreateFileA(winPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
        if (hSerial == INVALID_HANDLE_VALUE) return false;
        DCB dcb = {};
        dcb.DCBlength = sizeof(dcb);
        GetCommState(hSerial, &dcb);
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        SetCommState(hSerial, &dcb);
        return true;
        #else
        fd_serial = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_serial < 0) return false;
        int flags = fcntl(fd_serial, F_GETFL, 0);
        fcntl(fd_serial, F_SETFL, flags & ~O_NONBLOCK);
        struct termios tty;
        memset(&tty, 0, sizeof(tty));
        tcgetattr(fd_serial, &tty);
        cfsetispeed(&tty, B115200);
        cfsetospeed(&tty, B115200);
        tty.c_cflag = CS8 | CLOCAL | CREAD;
        tty.c_iflag = 0;
        tty.c_oflag = 0;
        tty.c_lflag = 0;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;
        tcsetattr(fd_serial, TCSANOW, &tty);
        tcflush(fd_serial, TCIOFLUSH);
        return true;
        #endif
    }

    bool IdentifyTpmDevice(const std::string &path)
    {
        if (!SerialOpen(path)) return false;

        SerialFlush();

        // Prepend \n to flush any stale data in the firmware's receive buffer.
        // The firmware processes <stale garbage>\n as a failed message (C5),
        // then processes "board:device(00)\n" cleanly on the next iteration.
        const char *cmd = "\nboard:device(00)\n";
        if (!SerialWrite(cmd, strlen(cmd))) { SerialClose(); return false; }

        char response[256] = {};
        int total = 0;
        for (int attempt = 0; attempt < 5; attempt++)
        {
            int n = SerialRead(response + total, sizeof(response) - 1 - total, 100);
            if (n > 0)
            {
                total += n;
                if (strstr(response, "ECC508")) break;
            }
            else if (total > 0) break;
        }

        SerialClose();
        return total > 0 && strstr(response, "ECC508") != nullptr;
    }

    bool SendReceive(const char *Cmd, char *Response, size_t SizeofResponse)
    {
        SerialFlush();

        size_t CmdLength = strlen(Cmd);

        if (!SerialWrite(Cmd, CmdLength)) return false;

        if (Response && SizeofResponse > 0) memset(Response, 0, SizeofResponse);
        size_t nReceived = 0;
        bool bCompleted = false;
        for (int attempt = 0; attempt < 50 && !bCompleted; attempt++)
        {
            uint8_t buf[128];
            int n = SerialRead(buf, sizeof(buf), 100);
            if (n <= 0) { if (nReceived > 0) break; continue; }

            for (int i = 0; i < n && !bCompleted; i++)
            {
                if (nReceived >= SizeofResponse - 1) { bCompleted = true; break; }
                if (Response) Response[nReceived] = buf[i];
                nReceived++;
                if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\0')
                    bCompleted = true;
            }
        }

        return bCompleted;
    }

    bool SendPacket(std::vector<uint8_t> packet, uint8_t* response, size_t responseCapacity, int* responseLength)
    {
        if (!response || !responseLength || responseCapacity == 0) return false;

        uint8_t len = packet[0];

        uint16_t crc = Crc16(packet.data(), len - 2);
        packet.push_back(crc & 0xFF);
        packet.push_back((crc >> 8) & 0xFF);

        std::string Cmd = "E:t(" + Util::ToHexString(packet.data(), packet.size()) + ")\n";

        Wakeup();
        char rx[512];
        bool success = false;
        if (SendReceive(Cmd.c_str(), rx, sizeof(rx)))
        {
            *responseLength = ParseTpmResponse(rx, response, responseCapacity);
            success = (*responseLength > 0);
        }
        Sleep();
        return success;
    }

    void Wakeup() { SendReceive("E:w()\n", nullptr, 0); }

    void Sleep() { SendReceive("E:i()\n", nullptr, 0); }

    public:
    CdcTpm() {}

    ~CdcTpm()
    {
        SerialClose();
    }

    std::string DiscoverTpmDevice()
    {
        for (const auto &path : listUsbDevices())
        {
            if (IdentifyTpmDevice(path))
                return path;
        }
        return {};
    }

    bool Initialize()
    {
        m_devicePath = DiscoverTpmDevice();
        if (m_devicePath.empty())
            return false;
        return SerialOpen(m_devicePath);
    }

    bool ReadSerial(uint8_t* sn)
    {
        uint8_t resp[64];
        int respLen = 0;
        if (SendPacket({ 7, 0x02, 0x80, 0x00, 0x00 }, resp, sizeof(resp), &respLen) && respLen >= 13) 
        {
            memcpy(sn + 0, resp + 1, 4);
            memcpy(sn + 4, resp + 9, 5);
            return true;
        }
        return false;
    }

    bool ReadUUID(uint64_t* serial_num, uint8_t uuid[16])
    {
        uint8_t resp[64];
        int respLen = 0;
        const uint16_t SLOT = 5;
        uint16_t addr = SLOT << 3;

        if (SendPacket({ 7, 0x02, 0x82, (uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8) }, resp, sizeof(resp), &respLen) && respLen >= 25) 
        {
            if (serial_num) memcpy(serial_num, resp + 1, 8);
            if (uuid) memcpy(uuid, resp + 9, 16);
            return true;
        }
        return false;
    }

    bool SignDigest(uint16_t iSlot, const uint8_t(&Digest32)[32], uint8_t(&Signature64)[64])
    {
        std::vector<uint8_t> pkt = { 0x27, 0x16, 0x03, 0x00, 0x00 };
        pkt.insert(pkt.end(), Digest32, Digest32 + 32);
        uint8_t resp[256];
        int respLen = 0;
        if (SendPacket(pkt, resp, sizeof(resp), &respLen) && respLen == 4)
        {
            pkt = { 0x07, 0x41, 0x80, (uint8_t)iSlot, 0x00 };
            if (SendPacket(pkt, resp, sizeof(resp), &respLen) && respLen == 64+3)
            {
                memcpy(Signature64, resp+1, 64);
                return true;
            }
        }
        return false;
    }
};
