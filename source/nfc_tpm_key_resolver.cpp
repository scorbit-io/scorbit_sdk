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

#include "nfc_tpm_key_resolver.h"
#include "device_info.h"
#include <logger/logger.h>
#include <utils/bytearray.h>
#include <tpm/hardwaretpm.h>
#include <tpm/tpm.h>

namespace scorbit {
namespace detail {

bool NfcTpmKeyResolver::tryResolve(DeviceInfo &info)
{
    auto tpm = std::make_shared<HardwareTpm>(TpmBus::USB);
    if (!tpm->hasTpm() || !tpm->isValid()) {
        INF("NFC TPM not found or not valid");
        tpm.reset();
        return false;
    }

    info.uuid = tpm->uuid();
    info.serialNumber = tpm->serial();
    m_tpm = std::move(tpm);

    INF("NFC TPM resolved: uuid={}, serial={}", info.uuid, info.serialNumber);
    return true;
}

SignerCallback NfcTpmKeyResolver::createSigner() const
{
    if (!m_tpm) {
        ERR("NFC TPM signer requested but TPM is not available");
        return {};
    }

    auto tpm = m_tpm;
    return [tpm](const Digest &digest) -> Signature {
        utils::ByteArray digestArray(digest.begin(), digest.size());
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
