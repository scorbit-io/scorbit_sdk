/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace nfc {

/** Probe identity and status, as reported by the firmware's ReadProbeInfos command. */
struct ProbeInformation {
    std::string id;
    std::string name;
    uint8_t versionMajor {0};
    uint8_t versionMinor {0};
    uint16_t versionRevision {0};
    /** Non-zero on legacy firmware: timestamp is a string and boot reason/watchdog are absent. */
    uint32_t timestampHeader {0};
    uint64_t timestampUtc {0};
    std::string timestamp;
    uint8_t bootReason {0};
    uint8_t watchdogCount {0};
    uint32_t timeMs {0};
    uint32_t sysClockFrequency {0};
    uint8_t usbFlags {0};
    uint32_t uid {0};
    std::array<uint8_t, 9> tpmSerial {};
    uint8_t tpmType {0};
    std::array<uint8_t, 16> tpmUuid {};
    uint64_t tpmScorbitSerial {0};
};

/** NFC state, as reported by the firmware's ReadNfcInfos command. */
struct NfcInformation {
    uint8_t flags {0};
    uint8_t brightness {0};
    uint8_t capacitance {0};
    uint32_t lastId {0};
    std::string tagUri;
    uint16_t rssi {0};
};

/** Bits of @ref NfcInformation::flags. */
enum NfcFlag : uint8_t {
    NfcFlagTag = 0x01,
    NfcFlagFactoryMode = 0x02,
    NfcFlagExtLm = 0x04,
    /** Tag mode: RSSI holds the RF amplitude measured at a tap. */
    NfcFlagMeasureAmplitude = 0x08,
    /** Tag mode: RSSI holds the antenna amplitude, updated continuously. */
    NfcFlagFieldDetector = 0x10,
};

/** Number of boot reason values the firmware defines (0 through WatchdogTimerAcknowledged). */
constexpr std::size_t NFC_BOOT_REASON_COUNT {6};

enum class NfcType : uint8_t {
    Reader = 0,
    Tag = 1,
};

/**
 * I/O seam over a connected NFC probe, so ProbesManager can be tested without USB.
 *
 * Implementations are not required to be thread-safe; ProbesManager serializes all calls.
 * @warning getNfcInformations() clears the probe's tap indicator (lastId) on real hardware.
 */
class NfcProbeDevice
{
public:
    virtual ~NfcProbeDevice() = default;

    virtual bool getInformations(ProbeInformation &info) = 0;
    virtual bool getNfcInformations(NfcInformation &info) = 0;
    virtual bool setUri(const std::string &uri) = 0;
    virtual bool setLedsBrightness(uint8_t brightness) = 0;
    virtual bool setType(NfcType type) = 0;

    /**
     * Sends the bootloader command. The probe reboots into its RP2040 boot ROM (BOOTSEL) without
     * replying, so only a BOOTSEL device appearing shows that it worked.
     */
    virtual void enterBootloader() {}

    /** What the probe was opened on: "/dev/ttyACM<n>" or "usb:<bus>:<addr>:<iface>". */
    virtual std::string devicePath() const { return {}; }
};

} // namespace nfc
