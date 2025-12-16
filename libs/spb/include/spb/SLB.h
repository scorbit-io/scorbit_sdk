// --------------------------------------------------------------------
//  Project:           Scorbitron SLB
//  Description:       LED helper
// --------------------------------------------------------------------
// Code Revision History :
// --------------------------------------------------------------------
// Ver:	| Author			| Mod. Date		| Changes Made:
// V1.0	| Olivier Galliez	| 2024-12-21	| Initial Release
// --------------------------------------------------------------------

#pragma once

#include "Util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <atomic>
#include <thread>
#include <iostream>
#include <algorithm>
#include <cstring>
#include "Debug.h"

#ifdef _WIN32
#pragma warning(disable:4996)
#include <io.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define be32toh(x) OSSwapBigToHostInt32(x)
#endif

// --------------------------------------------------------------------
// Raspberry Pi IO management
// --------------------------------------------------------------------

// GPIO definitions
#define GPIO_BLOCK_SIZE     4096 // GPIO memory block

// GPIO Offsets
#define GPFSEL_OFFSET       0 // GPIO Function Select
#define GPSET_OFFSET        7 // GPIO Pin Output Set
#define GPCLR_OFFSET        10 // GPIO Pin Output Clear
#define GPLEV_OFFSET        13 // GPIO Pin Level
#define GPPUD_OFFSET        37 // GPIO Pull-up/down Enable
#define GPPUDCLK_OFFSET     38 // GPIO Pull-up/down Clock

class SLB_IO
{
	friend class SLB_Jtag;

	protected:
	// GPIO, memory mapped
	volatile uint32_t *gpio;
	// PWM
	typedef struct
	{
		int pin;
		std::thread Thread;
		int MicroSecondsHigh, MicroSecondsLow;
		std::atomic<bool> bAbort, bEnabled;
	} Pwm_t;
	static const int MaxPwmPins = 27;
	Pwm_t PwmPins[MaxPwmPins];

	protected:
	SLB_IO() // Constructor is protected to prevent class instanciation 
	{ 
		gpio = NULL; 
		// Mark all PWM pins as invalid
		for (int i = 0; i < MaxPwmPins; i++)
			PwmPins[i].pin = -1;
	}
	~SLB_IO()
	{
		// Stop all running PWM
		for (int i = 0; i < MaxPwmPins; i++)
		{
			// Stop the PWM thread
			if (PwmPins[i].Thread.joinable())
			{
				// Disable the pin only if there's a PWM thread
				// If not, the pin is driven at a 0% or 100% ratio and we don't disable it (so that any solid LED stays active)
				EnablePWM(i, false);

				// Abort the thread
				PwmPins[i].bAbort = true;
				PwmPins[i].Thread.join();
			}
		}

		// Release GPIO
		#ifndef _WIN32
		munmap((void*)gpio, GPIO_BLOCK_SIZE);
		#endif
	}
	bool Initialize()
	{
		#ifdef __linux__

		// We need to be on an SLB
		if (!Util::IsSLB()) return false;

		// Determine GPIO base address
		uint32_t gpio_base = 0x3F200000; // GPIO base address; Default for older SoCs;
		FILE* fp = fopen("/proc/device-tree/soc/ranges", "rb");
		if (fp) 
		{
			uint32_t address;
			fseek(fp, 4, SEEK_SET); // Skip first 4 bytes
			fread(&address, sizeof(address), 1, fp);
			fclose(fp);
			address = be32toh(address);
			gpio_base = address + 0x200000;
		}

		// Open /dev/mem
		int mem_fd;
		if ((mem_fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) return false;
		// Map memory to GPIO
		gpio = (uint32_t*)mmap(NULL, GPIO_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, gpio_base);

		// Close /dev/mem
		close(mem_fd);

		return (gpio != MAP_FAILED);
		#else // __linux__
		return true;
		#endif
	}
	bool SetDirection(int pin, bool bOutput)
	{
		if (gpio == NULL) return false;

		int register_index = pin / 10;
		int bit = (pin % 10) * 3;

		if (bOutput)
		{
			gpio[GPFSEL_OFFSET + register_index] &= ~(7 << bit);  // Clear bits
			gpio[GPFSEL_OFFSET + register_index] |= (1 << bit);   // Configure as an output
		}
		else
			gpio[GPFSEL_OFFSET + register_index] &= ~(7 << bit);  // Clear bits

		return true;
	}
	bool SetPullUp(int pin)
	{
		if (gpio == NULL) return false;

		#ifdef __linux__
		// Activate pull-up for the given pin
		gpio[GPPUD_OFFSET] = 0b10; // Enable pull-up control
		std::this_thread::sleep_for(std::chrono::microseconds(5)); // Wait for control signal to stabilize

		gpio[GPPUDCLK_OFFSET + (pin / 32)] = (1 << (pin % 32)); // Apply pull-up to the pin
		std::this_thread::sleep_for(std::chrono::microseconds(5)); // Wait for changes to propagate

		gpio[GPPUD_OFFSET] = 0; // Clear GPPUD to remove control signal
		gpio[GPPUDCLK_OFFSET + (pin / 32)] = 0; // Remove clock signal
		#endif // __linux__

		return true;
	}
	bool SetPin(int pin, int state)
	{
		if (gpio == NULL) return false;
		if (state)
			gpio[GPSET_OFFSET] = (1 << pin);
		else
			gpio[GPCLR_OFFSET] = (1 << pin);
		return true;
	}
	int GetPin(int pin)
	{
		if (gpio == NULL) return -1;
		return (gpio[GPLEV_OFFSET] & (1 << pin)) ? 1 : 0;
	}
	// Software PWM
	bool SetPWM(int pin, int frequency, float dutyCycle, bool bEnabled = true)
	{

		if (gpio == NULL || pin < 0 || pin >= MaxPwmPins) return false;
		if (dutyCycle < 0) dutyCycle = 0.0f;
		if (dutyCycle > 1.0f) dutyCycle = 1.0f;

		// Stop previous PWM 
		Pwm_t &pwm = PwmPins[pin];
		pwm.bAbort = true;
		if (pwm.Thread.joinable())
			pwm.Thread.join();

		pwm.pin = pin;
		pwm.bEnabled = bEnabled;
		pwm.bAbort = false;

		// No need to start a thread if the led is off
		if (frequency <= 0 || dutyCycle <= 0.0f) { SetPin(pwm.pin, 0); return true; }

		// No need to start a thread if duty cycle is 100%
		if (dutyCycle == 1.0f) { SetPin(pwm.pin, 1); return true; }

		// Compute period in microseconds
		const int periodUs = 1000000 / frequency;
		pwm.MicroSecondsHigh = static_cast<int>(periodUs * dutyCycle);
		pwm.MicroSecondsLow = periodUs - pwm.MicroSecondsHigh;

		// Start a thread to handle PWM
		Pwm_t *pPwm = &pwm;
		pwm.Thread = std::thread([this, pPwm]()
		{
			#ifdef __linux__
			struct sched_param param;
			param.sched_priority = sched_get_priority_max(SCHED_RR);
			pthread_setschedparam(pthread_self(), SCHED_RR, &param);
			#endif // __linux__
			
			while (!pPwm->bAbort)
			{
				// Pin High
				SetPin(pPwm->pin, (pPwm->bEnabled) ? 1:0);
				std::this_thread::sleep_for(std::chrono::microseconds(pPwm->MicroSecondsHigh));

				// Pin Low
				SetPin(pPwm->pin, 0);
				std::this_thread::sleep_for(std::chrono::microseconds(pPwm->MicroSecondsLow));
			}
		});
		
		return true;
	}
	bool EnablePWM(int pin, bool bEnabled)
	{
		if (gpio == NULL || pin < 0 || pin >= MaxPwmPins) return false;
		// Save the state
		PwmPins[pin].bEnabled = bEnabled;
		// If a PWM thread is not running, the duty cycle is probably at 0% or 100%, so use the pin directly
		if (!PwmPins[pin].Thread.joinable())
			SetPin(pin, bEnabled ? 1 : 0);
		return true;
	}
};

// --------------------------------------------------------------------
// SLB serial port
// --------------------------------------------------------------------

class SLB_Serial
{
	protected:
	int hSerial = -1;

	public:
	SLB_Serial() 
	{
	}
	~SLB_Serial()
	{
		UnInitialize();
	}
	bool Initialize(const char *sPort, int BaudRate=115200, int nBits=8, int nStops=1, char Parity='n')
	{
		#if defined(__APPLE__) || defined(_MSC_VER) || defined(__x86_64__)
		return false;
		#else
		// Open serial port
		hSerial = open(sPort, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
		if (hSerial < 0)
		{
			if (HardwareDebug::IsFlagSet(HardwareDebug::DebugSLB)) ERR("Can't open %d !\n", sPort);
			return false;
		}
		if (HardwareDebug::IsFlagSet(HardwareDebug::DebugSLB)) INF("Using port %s\n", sPort);

		// Initialize the serial port
		struct termios options;
		tcgetattr(hSerial, &options);
		speed_t br;
		switch (BaudRate)
		{
			case 57600: br = B57600; break;
			case 115200: br = B115200; break;
			default: return false;
		}
		cfsetispeed(&options, br);
		cfsetospeed(&options, br);
		options.c_cflag &= ~(CSIZE | PARODD | PARENB | CSTOPB); // 1 stop bit, no parity
		if (nStops == 2) options.c_cflag |= CSTOPB;				// 2 stops
		options.c_cflag |= (nBits == 7) ? CS7:CS8;				// 7 or 8 bits
		if (Parity == 'o') options.c_cflag |= PARENB | PARODD;	// Odd parity
		else if (Parity == 'e') options.c_cflag |= PARENB;		// Even parity
		options.c_cflag |= (CLOCAL | CREAD);
		options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
		options.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
		options.c_iflag &= ~(INPCK | ISTRIP);
		options.c_oflag &= ~OPOST;
		options.c_cc[VMIN] = 0;		// Don't wait for char
		options.c_cc[VTIME] = 0;	// No timeout
		tcflush(hSerial, TCIFLUSH);
		tcsetattr(hSerial, TCSANOW, &options);

		return true;
		#endif // defined(__APPLE__) || defined(_MSV_VER)
	}
	void UnInitialize()
	{
		if (hSerial >= 0)
		{
			#ifdef _MSC_VER
			_close(hSerial);
			#else
			close(hSerial);
			#endif
			hSerial = -1;
		}
	}
	void Send(const void *Buffer, size_t BufLen)
	{
		// Serial port has to be open
		if (hSerial < 0) return;

		#ifdef _MSC_VER
		_write(hSerial, Buffer, (unsigned int)BufLen);
		#else
		write(hSerial, Buffer, BufLen);
		#endif
	}
	bool Receive(uint8_t *c)
	{
		// Serial port has to be open
		if (hSerial < 0) return false;

		#ifdef _MSC_VER
		return _read(hSerial, c, 1) == 1;
		#else
		return read(hSerial, c, 1) == 1;
		#endif
	}
	bool ReceiveLine(std::string *Line, std::atomic<bool> *ptrAbort=NULL, int TimeoutMs=1000)
	{
		// Serial port has to be open
		if (hSerial < 0) return false;

		*Line = "";
		int timeoutCounter = 0;
		while (ptrAbort==NULL || !*ptrAbort)
		{
			// Inter-character Timeout ?
			if (timeoutCounter > TimeoutMs) return false;
			timeoutCounter += 10;

			// Read 1 char
			char c; 
			if (!Receive((uint8_t *)&c))
			{ 
				#if  !defined(_MSC_VER) && !defined(__x86_64__)
				usleep(10 * 1000); // Wait 10ms to prevent 100% cpu usage
				#endif
				continue; 
			}

			// Reset timeout counter
			timeoutCounter = 0;

			// Ignore \r
			if (c == '\r') continue;

			// End of the line ?
			if (c == '\n') return true;

			// Save the received character
			(*Line) += c;
		}
		return false;
	}

	bool SendAndReceiveLines(const char* Cmd=NULL, std::string* Response= NULL, std::atomic<bool> *ptrAbort =NULL, int TimeoutFirsLineMs=1000, int TimeoutOtherLinesMs = 500, int nLinesExpected = 10000)
	{
		// Serial port has to be open
		if (hSerial < 0) return false;
		if (HardwareDebug::IsFlagSet(HardwareDebug::DebugSLB)) INF("Sending \"%s\"\n", Cmd);

		// Send command
		if (Cmd)
		{
			// Make sure the command ends with a "\r\n"
			std::string s = Cmd; if (s.size() < 2 || s.substr(s.size() - 2) != "\r\n") s.append("\r\n");
			// Send the command
			Send(s.c_str(), s.size());
		}

		// Receive response
		if (Response)
		{
			*Response = "";
			// Wait for a line
			if (!ReceiveLine(Response, ptrAbort, TimeoutFirsLineMs)) return false;
			if (HardwareDebug::IsFlagSet(HardwareDebug::DebugSLB)) INF("Received \"%s\"\n", Response->c_str());

			// Is it the echo to the command we have sent ?
			if (Cmd && std::search(Cmd, Cmd + strlen(Cmd), Response->cbegin(), Response->cend()) == Cmd)
			{
				if (HardwareDebug::IsFlagSet(HardwareDebug::DebugSLB)) INF("Echo received, waiting for real answer...\n");
				*Response = "";
				// Waiting for the response
				int nLines = 0;
				while (nLines < nLinesExpected)
				{
					// Wait for 1 line
					std::string s = ""; if (!ReceiveLine(&s, ptrAbort, TimeoutOtherLinesMs)) break;
					if (HardwareDebug::IsFlagSet(HardwareDebug::DebugSLB)) INF("Received also \"%s\"\n", s.c_str());
					// Separate this new line with a \n if any line has already been received
					if (Response->size() > 0) *Response += "\n";
					// Add this new line to the response
					*Response += s;
					nLines++;
				}
				// Return an error if nothing has been received
				return (Response->size() > 0);
			}
			return true;
		}
		return false;
	}
};

// --------------------------------------------------------------------
// SLB SAM interface
// --------------------------------------------------------------------

class SLB_Sam : public SLB_Serial
{
	public:
	SLB_Sam() {}
	bool Initialize() 
	{ 
		if (!SLB_Serial::Initialize("/dev/serial0", 57600, 8, 1, 'n')) 
			return false;
		return true;
	}
	void Reboot() { SendCommand("zc run 0"); } // Don't expect an answer from this function !
	std::string GetPatchVersion() { return SendCommand("zc ver"); }
	std::string GetGameInfos() { return SendCommand("zc mod 0x8e4d6900"); }
	std::string GetScores() { return SendCommand("zc mod 0x8e4d6901"); }
	std::string AddCredit() { return SendCommand("zc mod 0x8e4d6902"); }
	std::string AddPlayer() { return SendCommand("zc mod 0x8e4d6903"); }
	bool GetAdjustmentValue(int iAdj, uint32_t &Value)
	{ 
		char s[40]; snprintf(s, sizeof(s), "zc mod 0x8e4d6904 %u", iAdj); 
		std::string r = SendCommand(s);
		if (r.size() == 0) return false;
		return Util::FromString(r.substr(1), Value);
	}
	bool SetAdjustmentValue(int iAdj, uint32_t value) 
	{ 
		char s[40]; snprintf(s, sizeof(s), "zc mod 0x8e4d6904 %u %u", iAdj, value); 
		std::string r = SendCommand(s); 
		return (r.size() != 0);
	}
	std::string SendCommand(const char* Cmd)
	{
		std::string Response;
		if (!SendAndReceiveLines(Cmd, &Response, NULL, 500, 1000, 1)) return "";
		return Response;
	}
};

// --------------------------------------------------------------------
// SLB IO management
// --------------------------------------------------------------------

class SLB : public SLB_IO
{
	friend class SLB_Jtag;

	protected:
	typedef enum
	{
		SDA = 2,		// I2C
		SCL = 3,

		TCK = 27,		// JTag
		TMS = 17,
		TDI = 4,
		TDO = 22,
		nSRST = 23,

		LEDB_R = 5,		// LED B
		LEDB_G = 6,
		LEDB_B = 26,

		LEDA_R = 12,	// LED A
		LEDA_G = 13,
		LEDA_B = 18,

		SW = 24,		// Switch

		TXD0 = 14,		// Serial
		RXD0 = 15,
	} Pins;

	// Led variables
	public: 
	static const int LedCount = 2;
	protected:
	const int LedPinsR[LedCount] = { LEDA_R, LEDB_R };
	const int LedPinsG[LedCount] = { LEDA_G, LEDB_G };
	const int LedPinsB[LedCount] = { LEDA_B, LEDB_B };

	// Led pattern variables
	std::thread LedPatternThread;
	std::atomic<bool> bLedPatternThreadAbort = false;
	typedef struct
	{
		// Configuration data
		uint8_t DurationMs;
		uint32_t PatternR, PatternG, PatternB;
		uint8_t Cycles;
		bool bHasPattern, bStartEvent;
		// Live data
		uint8_t DurationCount;
		uint8_t CycleCount;
		uint32_t PatternBit;
	} LedPattern_t;
	LedPattern_t LedPatterns[LedCount] = { 0 };

	// Switch variable
	std::thread SwitchThread;
	std::atomic<bool> bSwitchThreadAbort = false;
	int SwitchPushCount = 0;
	bool bSwitchPressed = false;

	public:
	SLB() {}
	~SLB()
	{
		// Stop led pattern thread
		if (LedPatternThread.joinable())
		{
			bLedPatternThreadAbort = true;
			LedPatternThread.join();
		}
		// Stop switch thread
		if (SwitchThread.joinable())
		{
			bSwitchThreadAbort = true;
			SwitchThread.join();
		}
	}
	bool Initialize(bool bPreserveIO = false, bool bRunSwitchThread = true)
	{
		if (!SLB_IO::Initialize()) return false;

		// Define LEDs pins
		// Preserve any existing LED state
		static bool bFirstTime = true;
		SetDirection(LEDA_R, true); if (bFirstTime && !bPreserveIO) SetPin(LEDA_R, 0);
		SetDirection(LEDA_G, true); if (bFirstTime && !bPreserveIO) SetPin(LEDA_G, 0);
		SetDirection(LEDA_B, true); if (bFirstTime && !bPreserveIO) SetPin(LEDA_B, 0);
		SetDirection(LEDB_R, true); if (bFirstTime && !bPreserveIO) SetPin(LEDB_R, 0);
		SetDirection(LEDB_G, true); if (bFirstTime && !bPreserveIO) SetPin(LEDB_G, 0);
		SetDirection(LEDB_B, true); if (bFirstTime && !bPreserveIO) SetPin(LEDB_B, 0);
		bFirstTime = false;

		// Define JTAG pins
		SetDirection(nSRST, true); SetPin(nSRST, 0);
		SetDirection(TCK, true); SetPin(TCK, 0);
		SetDirection(TMS, true); SetPin(TMS, 0);
		SetDirection(TDI, true); SetPin(TDI, 0);
		SetDirection(TDO, false);

		// Define Switch pin
		SetDirection(SW, false);
		SetPullUp(SW);

		// Start switch thread
		if (bRunSwitchThread && !SwitchThread.joinable())
		{
			bSwitchThreadAbort = false;
			SwitchThread = std::thread([this]()
			{
				while (!bSwitchThreadAbort)
				{
					// Wait 20ms
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					// Read switch state
					bool bPressed = IsSwitchPressed();
					// The switch has just been pressed ?
					if (!bSwitchPressed && bPressed)
						SwitchPushCount++;
					// Save state
					bSwitchPressed = bPressed;
				}
			});
		}

		return true;
	}

	bool IsSwitchPressed()
	{
		return (GetPin(SW) == 0);
	}

	bool ReadSwitch(uint8_t &pushCount)
	{
		pushCount = SwitchPushCount;
		SwitchPushCount = 0;
		return true;
	}

	bool SetLed(int iLed, uint32_t rgb)
	{
		if (iLed < 0 || iLed >= LedCount) return false;

		// Stop any running pattern
		if (LedPatterns[iLed].bHasPattern)
		{
			LedPatterns[iLed].bHasPattern = false;
			// Wait for the pattern to be over
			// In the future, we'll have to sync the thread instead of waiting...
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		// Set the led
		SetPWM(LedPinsR[iLed], 50, ((rgb >> 16) & 0xff) / 255.0f);
		SetPWM(LedPinsG[iLed], 50, ((rgb >> 8) & 0xff) / 255.0f);
		SetPWM(LedPinsB[iLed], 50, ((rgb >> 0) & 0xff) / 255.0f);
		return true;
	}

	bool SetLedPattern(int iLed, uint32_t rgb, uint8_t DurationMs, uint32_t PatternR, uint32_t PatternG, uint32_t PatternB, uint8_t Cycles = -1)
	{
		if (iLed < 0 || iLed >= LedCount) return false;
		LedPattern_t& Pattern = LedPatterns[iLed];

		Pattern.DurationMs = DurationMs;
		Pattern.PatternR = PatternR;
		Pattern.PatternG = PatternG;
		Pattern.PatternB = PatternB;
		Pattern.Cycles = Cycles;
		Pattern.bStartEvent = true;
		Pattern.bHasPattern = true;

		SetPWM(LedPinsR[iLed], 50, ((rgb >> 16) & 0xff) / 255.0f, false);
		SetPWM(LedPinsG[iLed], 50, ((rgb >> 8) & 0xff) / 255.0f, false);
		SetPWM(LedPinsB[iLed], 50, ((rgb >> 0) & 0xff) / 255.0f, false);

		if (!LedPatternThread.joinable())
		{
			LedPatternThread = std::thread([this]()
			{
				while (!bLedPatternThreadAbort)
				{
					for (int iLed = 0; iLed < LedCount; iLed++)
					{
						LedPattern_t& Pattern = LedPatterns[iLed];

						// Does this led have a pattern ?
						if (!Pattern.bHasPattern) continue;

						// Is it a new pattern ?
						if (Pattern.bStartEvent)
						{
							Pattern.bStartEvent = false;  // Reset event
							Pattern.DurationCount = 0;
							Pattern.PatternBit = 0;
							Pattern.CycleCount = Pattern.Cycles;
						}

						// Increment the duration count until it reaches the desired Led duration
						if (Pattern.DurationCount >= Pattern.DurationMs)
						{
							Pattern.DurationCount = 0;

							if (Pattern.CycleCount == 0)
							{
								// Disable the led
								EnablePWM(LedPinsR[iLed], false);
								EnablePWM(LedPinsG[iLed], false);
								EnablePWM(LedPinsB[iLed], false);
							}
							else
							{
								// Update the led state
								EnablePWM(LedPinsR[iLed], ((Pattern.PatternR >> Pattern.PatternBit) & 1) != 0);
								EnablePWM(LedPinsG[iLed], ((Pattern.PatternG >> Pattern.PatternBit) & 1) != 0);
								EnablePWM(LedPinsB[iLed], ((Pattern.PatternB >> Pattern.PatternBit) & 1) != 0);

								// Next pattern bit
								if (Pattern.PatternBit >= 31)
								{
									Pattern.PatternBit = 0;

									// End of a cycle : decrement the cycle count
									if (Pattern.CycleCount != (uint8_t)-1)
										Pattern.CycleCount--;
								}
								else
									Pattern.PatternBit++;
							}
						}
						else
							Pattern.DurationCount++;
					}
					// Wait 1ms
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			});
		}
		return true;
	}
};
