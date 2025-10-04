// --------------------------------------------------------------------
//  Project:           Scorbitron SLB
//  Description:       Debug helper
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2025-09-14	| Initial Release
// --------------------------------------------------------------------

#pragma once

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
