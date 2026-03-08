/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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

#include "utils/machine_fingerprint.h"
#include <cpr/cpr.h>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace scorbit {
namespace detail {

struct ProvisionResult {
    std::string uuid;
    uint64_t serialNumber {0};
};

class ProvisioningClient
{
public:
    explicit ProvisioningClient(std::string hostname);

    /**
     * Step 1: GET /api/v2/provision/?provider={id}
     * Returns uuid and serial_number assigned by the API.
     */
    std::optional<ProvisionResult> initiate(const std::string &providerId,
                                            const std::vector<uint8_t> &providerKey);

    /**
     * Step 2: POST /api/v2/provision/
     * Confirms provisioning with the device's public key and signature.
     */
    bool confirm(const ProvisionResult &result, const std::string &publicKeyHex,
                 const std::string &deviceSignatureHex, const std::string &timestamp,
                 const std::string &providerId, const std::vector<uint8_t> &providerKey,
                 const MachineFingerprint &fingerprints);

private:
    cpr::Header buildProviderAuthHeaders(const std::string &providerId,
                                         const std::vector<uint8_t> &providerKey,
                                         const std::string &body = "");
    cpr::SslOptions sslOptions() const;

    std::string m_hostname;
};

} // namespace detail
} // namespace scorbit
