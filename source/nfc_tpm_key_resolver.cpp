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

#include "nfc_tpm_key_resolver.h"
#include "device_info.h"
#include <logger/logger.h>
#include <utils/bytearray.h>
#include <tpm/hardwaretpm.h>
#include <tpm/tpm.h>
#include <tpm/tappad_lock.h>
#include <nfc/CdcTpm.h>

#include <atomic>
#include <chrono>

namespace scorbit {
namespace detail {

namespace {

// Discovery writes a board:device probe to every candidate tty, the TPM's included, so it holds
// the tap pad lock like any other chip sequence. Neither caller runs inside a chip hold.
// A chip sequence holds the lock for well under a second, so a longer wait than the per-attempt
// default only matters when another process is stuck holding it.
constexpr uint32_t DISCOVERY_LOCK_TIMEOUT_MS = 10000;

} // namespace

TpmLocation discoverTpmDeviceUnderLock()
{
    bool busy = false;
    auto path = scanUnderTappadLock([] { return CdcTpm().DiscoverTpmDevice(); },
                                    DISCOVERY_LOCK_TIMEOUT_MS, &busy);
    if (busy) {
        return TpmLocation::busyTapPad();
    }
    return path;
}

std::string scanUnderTappadLock(const std::function<std::string()> &scan, uint32_t timeoutMs,
                                bool *busy)
{
    const auto start = std::chrono::steady_clock::now();
    const TappadLock hold {timeoutMs};
    if (busy) {
        *busy = !hold.ok();
    }
    if (!hold.ok()) {
        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
        static std::atomic<bool> warned {false};
        if (!warned.exchange(true)) {
            WRN("TPM discovery: tap pad busy for {} ms, not scanning", waited.count());
        } else {
            DBG("TPM discovery: tap pad busy for {} ms, not scanning", waited.count());
        }
        return {};
    }
    return scan();
}

bool NfcTpmKeyResolver::tryResolve(DeviceInfo &info, const std::string &)
{
    m_retryLater = false;

    const TpmLocation located = m_discover ? m_discover() : TpmLocation {};
    if (located.busy) {
        // Not "no TPM": say so, so the chain does not fall through to a soft key.
        m_retryLater = true;
        return false;
    }
    const std::string &tpmDevicePath = located.path;

    if (!tpmDevicePath.empty()) {
        INF("TPM CDC device found at {}", tpmDevicePath);
    }

    auto tpm = std::make_shared<HardwareTpm>(TpmBus::USB, tpmDevicePath, m_discover);
    // The constructor's identity read was busy: opening again now would only wait again.
    if (tpm->busy()) {
        m_retryLater = true;
        INF("NFC TPM busy, will retry");
        return false;
    }
    if (!tpm->hasTpm() || !tpm->isValid()) {
        m_retryLater = tpm->busy();
        INF("NFC TPM {}", m_retryLater ? "busy, will retry" : "not found or not valid");
        tpm.reset();
        return false;
    }

    info.uuid = tpm->uuid();
    info.serialNumber = tpm->serial();
    m_tpm = std::move(tpm);

    INF("NFC TPM resolved: uuid={}, serial={}", info.uuid, info.serialNumber);
    return true;
}

std::function<bool()> NfcTpmKeyResolver::createBusyCheck() const
{
    auto tpm = m_tpm;
    return [tpm] { return tpm && tpm->busy(); };
}

SignerCallback NfcTpmKeyResolver::createSigner() const
{
    if (!m_tpm) {
        ERR("NFC TPM signer requested but TPM is not available");
        return {};
    }

    auto tpm = m_tpm;
    return [tpm](const Digest &digest) -> Signature {
        // .data() rather than .begin(): the intended overload is
        // ByteArray(const uint8_t[], size_t), and std::array::iterator is only a
        // raw pointer on libstdc++ and libc++. MSVC makes it a class type, so
        // begin() matches nothing and the compiler falls through to the
        // string-iterator overload:
        //
        //   error C2665: 'utils::ByteArray::ByteArray': no overloaded function
        //   could convert all the argument types
        //
        // data() is required to return a pointer on every implementation.
        utils::ByteArray digestArray(digest.data(), digest.size());
        const auto sig = tpm->signDigest(digestArray);
        if (sig.empty()) {
            ERR("NFC TPM signing failed");
            return {};
        }
        return Signature(sig.begin(), sig.end());
    };
}

} // namespace detail
} // namespace scorbit
