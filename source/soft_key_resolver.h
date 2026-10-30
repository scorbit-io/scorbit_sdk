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

#include "hardware_key_marker.h"
#include "key_resolver.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {

class SoftKeyResolver : public IKeyResolver
{
public:
    /// While @p marker holds a hardware key, no soft key is loaded or provisioned (SB-6133).
    explicit SoftKeyResolver(std::shared_ptr<const HardwareKeyMarker> marker = {})
        : m_marker(std::move(marker))
    {
    }

    bool tryResolve(DeviceInfo &info, const std::string &serverTimestamp) override;
    /// True when the last tryResolve() refused because the device's key is a hardware key.
    bool shouldRetryLater() const override { return m_retryLater; }
    SignerCallback createSigner() const override;

private:
    bool tryLoadKey(DeviceInfo &info);
    bool provisionNewKey(DeviceInfo &info, const std::vector<uint8_t> &providerKey,
                         const std::string &serverTimestamp);

    std::shared_ptr<const HardwareKeyMarker> m_marker;
    bool m_retryLater {false};
    bool m_markerWarned {false};
    std::string m_encryptedDeviceKey;
    std::string m_deviceKeyPassword;
};

} // namespace detail
} // namespace scorbit
