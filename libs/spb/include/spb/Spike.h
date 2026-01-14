// --------------------------------------------------------------------
//  Project:           Scorbitron
//  Description:       Spike class
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2020-05-12	| Code from Pinball Browser
// --------------------------------------------------------------------

#pragma once

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <cstring>
#include "Util.h"

#ifdef __linux__
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <openssl/sha.h>
#endif // __linux__

class Spike
{
	private:
	// Vector Ids defined by Pinball Browser and used by pbspk2comm
	enum
	{
		VECTOR_XorValue								= 0,
		VECTOR_PbSpk2CommDatVersion					= 1,
		VECTOR_ZL13player_scores					= 19,
		VECTOR_ZL9player_up							= 20,
		VECTOR_ZL12ball_in_play						= 21,
		VECTOR_ZL13total_players					= 22,
		VECTOR_sys_gs_flags							= 23,
		VECTOR_Crc									= 24,
		VECTOR_XorScores							= 26,
		VECTOR_ScoreFlags							= 27,
		VECTOR_g_matrix_data						= 50,
		VECTOR_g_matrix_dedicated_data				= 51,
		VECTOR_SwitchStartIndex						= 52,
		VECTOR_SwitchCreditIndex					= 53,
		VECTOR_AdjustmentCount						= 54,
		VECTOR_AdjustmentSize						= 55,
		VECTOR_AdjustmentAddr						= 56,
		VECTOR_CreditTableAddr						= 57,
		Vector_Count								= 58
	};
	uint32_t Vectors[Vector_Count] = { 0 };
	int hPid = 0;

	public:
	Spike()
	{
	}
	bool Initialize(const char *Pbspk2commFilename = "pbspk2comm")
	{
		// Read vector file from pbspk2comm or pbspk2comm.dat
		int fn = open(Pbspk2commFilename, O_RDONLY);
		if (fn == -1) { ERR("Can't open pbspk2comm !\n"); return false; }

		// Go to the beginning of vectors
		if (lseek(fn, -1024, SEEK_END) == -1) { ERR("Can't find vectors in pbspk2comm !\n"); close(fn); return false; }

		// Read the vectors
		if (read(fn, Vectors, sizeof(Vectors)) != sizeof(Vectors)) { ERR("pbspk2comm is invalid !\n"); close(fn); return false; }
		close(fn);

		// Compute Crc
		uint32_t Crc = 1;
		for (int i = 1; i < sizeof(Vectors) / sizeof(Vectors[0]); i++) { Vectors[i] ^= Vectors[VECTOR_XorValue]; Crc += Vectors[i]; }
		if (Crc) { ERR("pbspk2comm is invalid !\n"); return false; }

		// Get the game Pid
		if (!UpdatePid()) { ERR("game process not found !\n"); return false; }

		return true;
	}

	static uint32_t GetFirmwareId()
	{
		uint32_t FirmwareId = 0;

		#if defined(__linux__) 
		// Open the file
		int fd = ::open("/games/game", O_RDONLY);
		if (fd < 0) { ERR("/games/game not found !\n"); return 0; }

		// mmap the game file
		struct stat st;
		if (fstat(fd, &st) < 0) { close(fd); return 0; }
		auto* data = static_cast<const unsigned char*>(mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
		if (data == MAP_FAILED) { close(fd); return 0; }

		// check ELF magic & class
		if (std::memcmp(data, ELFMAG, SELFMAG) != 0) { munmap((void*)data, st.st_size); close(fd); return 0; }

		// Is it a 32 or 64-bit binary ?
		bool found = false;
		const unsigned char *text_ptr = nullptr;
		size_t text_size = 0;
		if (data[EI_CLASS] == ELFCLASS32)
		{
			// Find the .text section
			auto *ehdr = reinterpret_cast<const Elf32_Ehdr*>(data);
			auto *shdrs = reinterpret_cast<const Elf32_Shdr*>(data + ehdr->e_shoff);
			const char* shstrtab = reinterpret_cast<const char*>(data + shdrs[ehdr->e_shstrndx].sh_offset);
			for (int i = 0; i < ehdr->e_shnum; ++i)
			{
				const auto &sh = shdrs[i];
				const char *name = shstrtab + sh.sh_name;
				if (std::strcmp(name, ".text") == 0)
				{
					// Section .text has been found
					found = true;
					text_ptr = data + sh.sh_offset;
					text_size = sh.sh_size;
					break;
				}
			}
		}
		else if (data[EI_CLASS] == ELFCLASS64)
		{
			// Find the .text section
			auto *ehdr = reinterpret_cast<const Elf64_Ehdr*>(data);
			auto *shdrs = reinterpret_cast<const Elf64_Shdr*>(data + ehdr->e_shoff);
			const char* shstrtab = reinterpret_cast<const char*>(data + shdrs[ehdr->e_shstrndx].sh_offset);
			for (int i = 0; i < ehdr->e_shnum; ++i)
			{
				const auto &sh = shdrs[i];
				const char *name = shstrtab + sh.sh_name;
				if (std::strcmp(name, ".text") == 0)
				{
					// Section .text has been found
					found = true;
					text_ptr = data + sh.sh_offset;
					text_size = sh.sh_size;
					break;
				}
			}
		}

		if (found)
		{
			uint8_t hash[SHA256_DIGEST_LENGTH];
			SHA256(text_ptr, (text_size / 1024) * 1024, hash);
			FirmwareId = (uint32_t(hash[0]) << 24) | (uint32_t(hash[1]) << 16) | (uint32_t(hash[2]) << 8) | (uint32_t(hash[3]));
		}

		// Cleanup
		munmap((void*)data, st.st_size);
		close(fd);

		#else // __linux__
		ERR("This function only works on a Spike game !\n");
		#endif // __linux__

		return FirmwareId;
	}
	typedef struct
	{
		uint8_t PlayerUp, BallInPlay, TotalPlayers;
		long long Scores[4];
		bool bGameOver;
		int Credits;
	} SpikeInformations_t;
	bool GetInformations(SpikeInformations_t *spi)
	{
		// Update the game Pid
		if (!UpdatePid()) return false;

		// Get data
		bool b1 = (Vectors[VECTOR_PbSpk2CommDatVersion] >= 3) && ((Vectors[VECTOR_ScoreFlags] & 1) != 0);
		bool b2 = (Vectors[VECTOR_PbSpk2CommDatVersion] >= 3) && ((Vectors[VECTOR_ScoreFlags] & 2) != 0);
		char Mem[0x40]; if (!ReadProcessMemory(hPid, Vectors[VECTOR_ZL13player_scores], sizeof(Mem), Mem)) return false;
		if (!ReadProcessMemory(hPid, Vectors[VECTOR_ZL9player_up], sizeof(SpikeInformations_t::PlayerUp), &spi->PlayerUp)) return false;
		if (!ReadProcessMemory(hPid, Vectors[VECTOR_ZL12ball_in_play], sizeof(SpikeInformations_t::BallInPlay), &spi->BallInPlay)) return false;
		if (!ReadProcessMemory(hPid, Vectors[VECTOR_ZL13total_players], sizeof(SpikeInformations_t::TotalPlayers), &spi->TotalPlayers)) return false;
		unsigned short SysFlags; if (!ReadProcessMemory(hPid, Vectors[VECTOR_sys_gs_flags], sizeof(SysFlags), &SysFlags)) return false;
		spi->bGameOver = (SysFlags & 0x10) ? true : false;

		// Extract the scores
		if (b1)
		{
			uint32_t ScoreAddr = ((uint32_t*)Mem)[0];
			ReadProcessMemory(hPid, ScoreAddr, sizeof(Mem), Mem);
		}
		for (int iScore = 0; iScore < sizeof(SpikeInformations_t::Scores) / sizeof(SpikeInformations_t::Scores[0]); iScore++)
		{
			if (iScore >= spi->TotalPlayers)
				spi->Scores[iScore] = -1;
			else
			{
				if (b2)
				{
					if (Vectors[VECTOR_XorScores] != 0)
						spi->Scores[iScore] = ((long long*)Mem)[iScore] ^ ((long long*)Mem)[iScore + 4] ^ (long long)Vectors[VECTOR_XorScores];
					else
						spi->Scores[iScore] = ((long long*)Mem)[iScore];
				}
				else
				{
					if (Vectors[VECTOR_XorScores] != 0)
						spi->Scores[iScore] = ((long*)Mem)[iScore] ^ ((long*)Mem)[iScore + 4] ^ (long)Vectors[VECTOR_XorScores];
					else
						spi->Scores[iScore] = ((long*)Mem)[iScore];
				}
			}
		}

		// Extract the credits
		spi->Credits = -1;
		if (Vectors[VECTOR_CreditTableAddr] != 0)
		{
			uint32_t CreditRecordAddr = 0;
			uint8_t CreditRecord[6] = { 0 };
			if (ReadProcessMemory(hPid, Vectors[VECTOR_CreditTableAddr]+0x2c, sizeof(CreditRecordAddr), &CreditRecordAddr) &&
				ReadProcessMemory(hPid, CreditRecordAddr, sizeof(CreditRecord), CreditRecord))
			{
				// Check the record CRC
				uint8_t Crc = 0; for (int i = 0; i < sizeof(CreditRecord); i++) Crc += CreditRecord[i];
				// Get the credits
				if (Crc == 0xff) spi->Credits = CreditRecord[4];
			}
		}

		return true;
	}
	// Switch simulation when we know its index on the game
	bool SimulateSwitch(uint8_t iSwitch, bool bActive)
	{
		return SetSwitchBit(Vectors[VECTOR_g_matrix_data], iSwitch, bActive);
	}
	bool SimulateSwitch(uint8_t sw)
	{
		INF("Pressing switch %d\n", (int)sw);
		if (!SimulateSwitch(sw, true)) return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		INF("Releasing switch %d\n", (int)sw);
		return SimulateSwitch(sw, false);
	}
	// Switch simulation by function
	typedef enum : int { START=0, CREDIT=1 } Switch_t;
	bool SimulateSwitch(Switch_t sw, bool bActive)
	{
		if (sw == START)
			return SimulateSwitch(Vectors[VECTOR_SwitchStartIndex], bActive);
		else if (sw == CREDIT)
			return SimulateSwitch(Vectors[VECTOR_SwitchCreditIndex]+0x80, bActive); // +0x80 because CREDIT is a dedicated switch
		return false;
	}
	bool SimulateSwitch(Switch_t sw)
	{
		if (!SimulateSwitch(sw, true)) return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		return SimulateSwitch(sw, false);
	}
	bool GetAdjustmentValue(int iAdj, uint32_t& Value)
	{
		// Update the game Pid
		if (!UpdatePid()) return false;

		if (iAdj >= (int)Vectors[VECTOR_AdjustmentCount]) return false;
		if (!ReadProcessMemory(hPid, Vectors[VECTOR_AdjustmentAddr]+Vectors[VECTOR_AdjustmentSize]*iAdj, sizeof(Value), &Value)) return false;
		if (!ReadProcessMemory(hPid, Value, sizeof(Value), &Value)) return false;
		return true;
	}
	bool SetAdjustmentValue(int iAdj, uint32_t Value)
	{
		// Update the game Pid
		if (!UpdatePid()) return false;

		if (iAdj >= (int)Vectors[VECTOR_AdjustmentCount]) return false;
		uint32_t addr = 0;
		if (!ReadProcessMemory(hPid, Vectors[VECTOR_AdjustmentAddr] + Vectors[VECTOR_AdjustmentSize] * iAdj, sizeof(addr), &addr)) return false;
		if (!WriteProcessMemory(hPid, addr, sizeof(Value), &Value)) return false;
		return true;
	}

	std::vector<uint8_t> ReadMemory(uint32_t Address, int Len)
	{
		// Update the game Pid
		if (!UpdatePid()) return {};

		// Allocate a vector
		std::vector<std::uint8_t> Mem(Len);

		// Read memory
		if (!ReadProcessMemory(hPid, Address, Len, Mem.data())) return {};

		return Mem;
	}
	bool WriteMemory(uint32_t Address, const std::vector<uint8_t>& Buffer)
	{
		// Update the game Pid
		if (!UpdatePid()) return false;

		// Write memory
		return WriteProcessMemory(hPid, Address, (int)Buffer.size(), (void *)Buffer.data());
	}

	private:
	bool UpdatePid()
	{
		if (hPid <= 0 || !Util::PidExists(hPid))
		{
			// No, get the game Pid
			hPid = Util::GetPid("game");
		}
		return hPid > 0;
	}

	bool ReadProcessMemory(int Pid, int Addr, int Size, void* Mem)
	{
		#ifdef __linux__
		struct iovec liov[1];
		struct iovec remote[1];
		liov[0].iov_base = Mem;
		liov[0].iov_len = Size;
		remote[0].iov_base = (void *)((intptr_t)Addr);
		remote[0].iov_len = Size;
		return process_vm_readv((pid_t)Pid, liov, 1, remote, 1, 0) == Size;
		#else
		return false;
		#endif // __linux__
	}
	bool WriteProcessMemory(int Pid, int Addr, int Size, void* Mem)
	{
		#ifdef __linux__
		struct iovec liov[1];
		struct iovec remote[1];
		liov[0].iov_base = Mem;
		liov[0].iov_len = Size;
		remote[0].iov_base = (void *)((intptr_t)Addr);
		remote[0].iov_len = Size;
		return process_vm_writev((pid_t)Pid, liov, 1, remote, 1, 0) == Size;
		#else
		return false;
		#endif // __linux__
	}

	bool SetSwitchBit(uint32_t Addr, int iBit, bool bSet)
	{
		// Update the game Pid
		if (!UpdatePid()) return false;

		// Check that the address is available
		if (Addr == 0) { ERR("No valid address for switch !\n"); return false; }
		// Read the switch byte
		uint8_t Data;
		if (!ReadProcessMemory(hPid, Addr + iBit / 8, 1, &Data)) return false;
		// Set the bit
		uint8_t Mask = 1 << (iBit % 8);
		Data &= ~Mask;
		if (bSet) Data |= Mask;
		INF("0x%02X at @0x%04X\n", Data, (Addr + iBit / 8));
		// Write it back
		return WriteProcessMemory(hPid, Addr + iBit / 8, 1, &Data);
	}
};
