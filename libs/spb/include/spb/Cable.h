// --------------------------------------------------------------------
//  Project:           Scorbitron SLB
//  Description:       Base class for cables
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2024-12-21	| Initial Release
// --------------------------------------------------------------------

#pragma once
#include <string>
#include <vector>

class ProbeCable
{
    public:
    std::string DeviceName = "";
    int DeviceIndex = -1;
    virtual ~ProbeCable() { Close(); }
    virtual std::string GetName() { return "None"; }
    virtual bool Initialize(const std::string& sDevice) { DeviceName = sDevice; return false; }
    virtual bool Initialize(int iDevice) { DeviceIndex = iDevice; return false; }
    virtual bool IsOpen() { return false; }
    virtual void Close() {}
    virtual void ListDevices() {} // List sub-devices on this cable

    virtual bool DataWrite(const std::vector<uint8_t>& Buffer, bool bEndTransaction = true) { return false; }
    virtual std::vector<uint8_t> DataRead(int count, bool bEndTransaction = true, int TimeoutMs = 2000) { return {}; }

    virtual bool Command(uint8_t Cmd) { return false; }
    virtual bool CommandWrite(uint8_t Cmd, uint32_t Address, const std::vector<uint8_t>& Data) { return false; }
    virtual std::vector<uint8_t> CommandRead(uint8_t Cmd, uint32_t Address, int Count) { return {}; }
};
