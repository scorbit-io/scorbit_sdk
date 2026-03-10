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

#include "key_resolver.h"
#include "device_info.h"
#include "net_util.h"
#include <logger/logger.h>

namespace scorbit {
namespace detail {

/**
 * Key resolver for the scorbitd signer callback path.
 * Wraps an externally-provided SignerCallback. Requires a valid UUID to be
 * already set in DeviceInfo; fails otherwise so the next resolver can try.
 */
class SignerKeyResolver : public IKeyResolver
{
public:
    explicit SignerKeyResolver(SignerCallback signer)
        : m_signer(std::move(signer))
    {
    }

    bool tryResolve(DeviceInfo &info, const std::string & /*serverTimestamp*/) override
    {
        if (!m_signer) {
            return false;
        }

        if (info.uuid.empty()) {
            INF("SignerKeyResolver: no UUID provided, skipping");
            return false;
        }

        const auto originalUuid = info.uuid;
        info.uuid = parseUuid(originalUuid);
        if (info.uuid.empty()) {
            ERR("SignerKeyResolver: UUID is not in correct format: {}", originalUuid);
            return false;
        }

        return true;
    }

    SignerCallback createSigner() const override { return m_signer; }

private:
    SignerCallback m_signer;
};

} // namespace detail
} // namespace scorbit
