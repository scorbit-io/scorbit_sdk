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

#include "key_resolver.h"
#include <tpm/tpm.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class HardwareTpm;

namespace scorbit {
namespace detail {

/**
 * Runs the TPM discovery @p scan under the tap pad lock. If the lock is not free within
 * @p timeoutMs, returns empty WITHOUT scanning (the scan writes probes to the TPM's tty, which
 * would corrupt the holder's sequence) and sets @p busy.
 */
std::string scanUnderTappadLock(const std::function<std::string()> &scan, uint32_t timeoutMs,
                                bool *busy = nullptr);

/// The production TPM discovery: the CdcTpm scan under the tap pad lock, waiting up to 10 s.
TpmLocation discoverTpmDeviceUnderLock();

class NfcTpmKeyResolver : public IKeyResolver
{
public:
    /// Finds the TPM's CDC node, or reports the tap pad busy. Tests replace it so they never
    /// probe real hardware.
    using Discover = std::function<TpmLocation()>;

    explicit NfcTpmKeyResolver(Discover discover = discoverTpmDeviceUnderLock)
        : m_discover(std::move(discover))
    {
    }

    bool tryResolve(DeviceInfo &info, const std::string &serverTimestamp) override;
    /// True when the last tryResolve() failed only because the tap pad was held by another
    /// process, so falling through to a soft key would be wrong.
    bool shouldRetryLater() const override { return m_retryLater; }
    bool isHardwareKey() const override { return true; }
    SignerCallback createSigner() const override;
    /// Whether the TPM's last attempt gave up because the tap pad was held by another process.
    std::function<bool()> createBusyCheck() const override;

private:
    Discover m_discover;
    std::shared_ptr<HardwareTpm> m_tpm;
    bool m_retryLater {false};
};

} // namespace detail
} // namespace scorbit
