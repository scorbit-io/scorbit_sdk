// --------------------------------------------------------------------
//  Project:           Scorbitron SLB
//  Description:       Serial cable class
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2024-12-21	| Initial Release
// --------------------------------------------------------------------

#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "Debug.h"
#include "Cable.h"
#include "Util.h"

#if defined(__linux__)
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
// Define IOSSIOSPEED ourselves instead of including <serial/ioss.h>
#ifndef IOSSIOSPEED
#define IOSSIOSPEED _IOW('T', 2, speed_t)
#endif
#endif

class SerialCable : public ProbeCable 
{
    private:
    #ifdef _WIN32
        HANDLE hSerial = INVALID_HANDLE_VALUE;
    #else
    int hSerial = -1;
    #endif

    public:
    #ifdef _WIN32
    static const int iFirstDevice = 1, iLastDevice = 30;
    #else
    static const int iFirstDevice = 0, iLastDevice = 6;
    #endif

	virtual std::string GetName() { return "Serial"; }
    virtual bool Initialize(const std::string &sDevice) 
    {
        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) INF("Using device %s\n", sDevice.c_str());

        // Call base function
        ProbeCable::Initialize(sDevice);

        #if defined(_WIN32)
            // Close serial port if necessary
            if (hSerial != INVALID_HANDLE_VALUE) { CloseHandle(hSerial); hSerial = INVALID_HANDLE_VALUE; }
            // Open serial port (with exclusivity)
            hSerial = CreateFileA(("\\\\.\\" + sDevice).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hSerial == INVALID_HANDLE_VALUE) return false;

		    // Configure DCB structure
		    DCB dcbSerialParams = {0};
		    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
		    if (!GetCommState(hSerial, &dcbSerialParams)) { ERR("Can't read serial port configuration !\n"); return false; }
		    dcbSerialParams.BaudRate = 921600 * 4;  
		    dcbSerialParams.ByteSize = 8;           
		    dcbSerialParams.Parity = NOPARITY;      
		    dcbSerialParams.StopBits = ONESTOPBIT;
		    dcbSerialParams.fParity = FALSE;
		    dcbSerialParams.fOutxCtsFlow = FALSE;   
		    dcbSerialParams.fOutxDsrFlow = FALSE;   
		    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
		    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
		    dcbSerialParams.fOutX = FALSE;          
		    dcbSerialParams.fInX = FALSE;           

		    // Apply configuration
		    if (!SetCommState(hSerial, &dcbSerialParams)) { ERR("Can't configure serial port !\n"); return false; }

		    // Configure timeouts
		    COMMTIMEOUTS timeouts = {0};
		    timeouts.ReadIntervalTimeout = 50;       
		    timeouts.ReadTotalTimeoutConstant = 1000;  
		    timeouts.ReadTotalTimeoutMultiplier = 10;
		    timeouts.WriteTotalTimeoutConstant = 1000; 
		    timeouts.WriteTotalTimeoutMultiplier = 10;
		    if (!SetCommTimeouts(hSerial, &timeouts)) { ERR("Can't configure seria timeouts !\n"); return false; }

            // Flush buffers
            PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
        #elif defined(__APPLE__)
            // MacOS initialization code
            // Add the /dev/ prefix if necessary
            std::string devicePath = sDevice;
            if (devicePath.find("/dev/") != 0) devicePath = "/dev/" + sDevice;

            // Open the serial port
            if (hSerial >= 0) Close();
            hSerial = open(devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (hSerial < 0) return false;

            // Ensure exclusivity
            if (ioctl(hSerial, TIOCEXCL) != 0) { close(hSerial); hSerial = -1; return false; }

            // Get the current port settings
            struct termios options;
            if (tcgetattr(hSerial, &options) < 0) { close(hSerial); hSerial = -1;  return false; }

            // Set custom baud rate
            speed_t baudRate = 921600 * 4;
            if (ioctl(hSerial, IOSSIOSPEED, &baudRate) < 0) { close(hSerial); hSerial = -1; return false; }

            // Configure port settings
            options.c_cflag &= ~(CSIZE | PARODD | PARENB | CSTOPB);
            options.c_cflag |= (CLOCAL | CREAD | CS8);
            options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
            options.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
            options.c_iflag &= ~(INPCK | ISTRIP);
            options.c_oflag &= ~OPOST;
            options.c_cflag &= ~HUPCL;

            // Apply the settings
            if (tcsetattr(hSerial, TCSANOW, &options) < 0) { close(hSerial); hSerial = -1; return false; }

            // Flush the port
            if (tcflush(hSerial, TCIOFLUSH) < 0) { close(hSerial); hSerial = -1; return false; }

        #elif defined(__linux__)
            // Open serial port
            if (hSerial >= 0) Close();
            hSerial = open(sDevice.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (hSerial < 0)
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) ERR("Can't open port %s - Err:%d\n", sDevice.c_str(), (int)hSerial);
                return false;
            }

            // Ensure exclusivity
            if (ioctl(hSerial, TIOCEXCL) != 0) { close(hSerial); hSerial = -1; return false; }

		    // Initialize the serial port
		    struct termios options;
		    tcgetattr(hSerial, &options);
		    cfsetispeed(&options, B921600);
		    cfsetospeed(&options, B921600);
		    options.c_cflag &= ~(CSIZE | PARODD | PARENB | CSTOPB); // 1 stop bit, no parity
		    options.c_cflag |= (CLOCAL | CREAD | CS8 );				// 8 bits
		    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
		    options.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
		    options.c_iflag &= ~(INPCK | ISTRIP);
		    options.c_oflag &= ~OPOST;
		    tcflush(hSerial, TCIFLUSH);
		    tcsetattr(hSerial, TCSANOW, &options);
        #else
            #error "Platform not supported !"
        #endif  

        // Extract the device index
        size_t pos = sDevice.find_last_not_of("0123456789");
        if (pos != std::string::npos && pos + 1 < sDevice.size())
            DeviceIndex = std::stoi(sDevice.substr(pos + 1));

        // To resync the receiver
        DataWrite({ 0x00, 0x00, 0x00, 0x00, 0x00 });

        return true;
    }

    virtual bool Initialize(int iDevice)
    {
        // Call base function
        ProbeCable::Initialize(iDevice);

        #if defined(_WIN32)
            // Windows device enumeration
            if (iDevice >= 0)
                return Initialize("COM" + std::to_string(iDevice));
            for (int i = iFirstDevice; i < iLastDevice; i++)
            {
                if (!Initialize("COM" + std::to_string(i))) 
                    continue;
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) INF("Autodetection : port COM%d\n", i);
                return true;
            }
            return false;
        #elif defined(__APPLE__)
            // MacOS device enumeration
            if (iDevice >= 0) return Initialize("cu.usbmodem" + std::to_string(iDevice));

            // Try the exact device we can see
            std::string cmd = "ls /dev/cu.usbmodem* 2>/dev/null";
            FILE *pipe = popen(cmd.c_str(), "r");
            if (pipe) 
            {
                char buffer[128];
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) 
                {
                    buffer[strcspn(buffer, "\n")] = 0;
                    std::string device = buffer;
                    if (device.find("/dev/") == 0)
                        device = device.substr(5);
                    if (Initialize(device)) { pclose(pipe); return true; }
                }
                pclose(pipe);
            }
            return false;
        #elif defined(__linux__)
            // Linux device enumeration
            if (iDevice >= 0)
                return Initialize("/dev/ttyACM" + std::to_string(iDevice));
            for (int i = iFirstDevice; i < iLastDevice; i++)
            {
                if (!Initialize("/dev/ttyACM" + std::to_string(i))) 
                    continue;
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) INF("Autodetection : port /dev/ttyACM%d\n", i);
                return true;
            }
            return false;
        #else
            #error "Platform not supported !"
        #endif
    }
    virtual bool IsOpen() 
    { 
        #if defined(_WIN32)
        return (hSerial != INVALID_HANDLE_VALUE);
        #elif defined(__linux__) || defined(__APPLE__)
        return (hSerial >= 0);
        #else
            #error "Platform not supported !"
        #endif
    }

    virtual void Close()
    {
        #if defined(_WIN32)
            if (hSerial != INVALID_HANDLE_VALUE)
            {
                CloseHandle(hSerial);
                hSerial = INVALID_HANDLE_VALUE;
            }
        #elif defined(__linux__) || defined(__APPLE__)
            if (hSerial >= 0)
            {
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) { INF("Closing port\n"); }
                tcflush(hSerial, TCIOFLUSH); // To prevent delays if some charaters have not been transmitted                                
                close(hSerial);
                if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) { INF("Port closed\n"); }
                hSerial = -1;
            }
        #else
            #error "Platform not supported !"
        #endif
    }

    virtual bool DataWrite(const std::vector<uint8_t>& buffer, bool bEndTransaction = true)
    {
        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) { INF("Sending:\n"); Util::Dump(buffer, 0, "", Util::DumpFlags::NoMD5); }

        #ifdef _WIN32
        if (hSerial == INVALID_HANDLE_VALUE) return false;
        DWORD bytesWritten;
        WriteFile(hSerial, buffer.data(), (DWORD)buffer.size(), &bytesWritten, nullptr);
        return bytesWritten == buffer.size();
        #else
        return hSerial >= 0 && write(hSerial, buffer.data(), (int)buffer.size()) == buffer.size();
        #endif
    }

    virtual std::vector<uint8_t> DataRead(int count, bool bEndTransaction = true, int TimeoutMs = 2000)
    {
        #ifdef _WIN32
        if (hSerial == INVALID_HANDLE_VALUE) return {};
        std::vector<uint8_t> buffer(count);
        DWORD bytesRead = 0, offset = 0;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TimeoutMs);
        while (count > 0) 
        {
            // Check timeout
            if (std::chrono::steady_clock::now() >= deadline) break;
            // Receive chars
            DWORD n;
            if (!ReadFile(hSerial, buffer.data() + offset, count, &n, nullptr))
            {
                PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
                return {};
            }
            count -= n;
            offset += n;
        }

        #else

        if (hSerial < 0) return {};
        std::vector<uint8_t> buffer(count);
        int offset = 0;
        int time = 0;
        while (count > 0)
        {
            int n = read(hSerial, buffer.data() + offset, count);
            if (n <= 0)
            { 
                #ifndef _WIN32
                usleep(10 * 1000); // Wait 10ms to prevent 100% cpu usage
                time += 10;
                if (time >= TimeoutMs)
                {
                    if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) ERR("Timeout waiting for data...\n");
                    return {};
                }
                #endif
                continue; 
            } 
            count -= n;
            offset += n;
            time = 0;
        }
        #endif

        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable)) { INF("Received:\n"); Util::Dump(buffer, 0, "", Util::DumpFlags::NoMD5); }
        return buffer;
    }

    // Serial USB Protocol
    // Frames sent to the probes :
    //   Header16 Cmd8 Address16 Count16 Data* Crc16
    //      Header16  : 0x4503 (Little Endian)
    //      Cmd8      : This byte is defined as follow :
    //		    {ID[5:0], HASPARAMS, WR}
    //		    - ID: Message id
    // 		    - WR: Read(0) or Write(1) message
    //		    - HASPARAMS : 0 when the message has no address and data, 1 otherwise
    //      Address16 : 16-bit address for the data (Little Endian)
    //      Count16   : 16-bit count for the data (Little Endian)
    //      Data*     : 1 or multiple bytes to read/write
    //      Crc16     : 16-bit (Little Endian) 1-complement sum of all bytes in the frame (Header included, Crc excluded)
    //
    // The reply from the probe is a frame with the following format :
    //  Header16 Data* Crc16
    //      Header16  : 0x4604 (Little Endian)
    //      Data*     : 1 or multiple bytes (the Count is specfied in the request)
    //      Crc16     : 16-bit (Little Endian) 1-complement sum of all bytes in the frame (Header included, Crc excluded)

    virtual bool Command(uint8_t cmd)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!DataWrite(AddCrc({ 0x03, 0x45, cmd }))) return false;
        std::vector<uint8_t> data;
        return WaitForReply(data, 0);
    }

    virtual bool CommandWrite(uint8_t cmd, uint32_t address, const std::vector<uint8_t>& data)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::vector <uint8_t> buffer = { 0x03, 0x45, cmd, (uint8_t)address, (uint8_t)(address >> 8), (uint8_t)data.size(), (uint8_t)(data.size() >> 8) };
        buffer.insert(buffer.end(), data.begin(), data.end());
        if (!DataWrite(AddCrc({ buffer }))) return false;
        return WaitForReply(buffer, 0);
    }

    virtual std::vector<uint8_t> CommandRead(uint8_t cmd, uint32_t address, int Count)
    {
        #ifndef _WIN32
        // Flush any pending char
        // Due to a bug in the cdc driver, the whole system crashes when closing a non-empty channel from a probe with a composite USB device
        // This can only happen during a "--spb detect" with probes compiles with a debug log (2nd cdc)
        char c; while (read(hSerial, &c, 1) > 0) /* Do nothing */;
        #endif

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!DataWrite(AddCrc({ 0x03, 0x45, cmd, (uint8_t)address, (uint8_t)(address >> 8), (uint8_t)Count, (uint8_t)(Count >> 8) }), false)) 
            return {};
        std::vector<uint8_t> Data;
        if (!WaitForReply(Data, Count)) return {};
        return Data;
    }

    private:
    std::vector<uint8_t> AddCrc(std::vector<uint8_t> data)
    {
        uint16_t Crc = 0;
        for (int i = 0; i < data.size(); i++)
            Crc += data[i];
        Crc = ~Crc;
        data.push_back((uint8_t)(Crc & 0xff));
        data.push_back((uint8_t)(Crc >> 8));
        return data;
    }
    bool WaitForReply(std::vector<uint8_t>& Data, int Count)
    {
        // Receive and verify header
        auto Header = DataRead(2);
        if (Header.size() != 2) return false;
        if (Header[0] != 0x04 && Header[1] != 0x46) return false;
        // Receive data
        if (Count)
            Data = DataRead(Count);
        else
            Data = {};
        // Receive Crc
        auto Crc = DataRead(2);
        if (Crc.size() != 2) return false;
        // Verify Crc
        uint16_t Sum = 0;
        for (int i = 0; i < Header.size(); i++) Sum += Header[i];
        for (int i = 0; i < Data.size(); i++) Sum += Data[i];
        Sum = ~Sum;
        bool bCrcOk = (uint8_t)Sum == Crc[0] && (uint8_t)(Sum >> 8) == Crc[1];
        if (HardwareDebug::IsFlagSet(HardwareDebug::DebugCable) && !bCrcOk)
        {
            ERR("Wrong CRC :\n- Received = 0x%04X\n- Expected = 0x%02X%02X\n", (int)Crc[1], (int)Crc[0], (int)Sum);
        }
        return bCrcOk;
    }
};
