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
#include <cstdint>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {

class SoftKeyResolver : public IKeyResolver
{
public:
    bool tryResolve(DeviceInfo &info, const std::string &serverTimestamp) override;
    SignerCallback createSigner() const override;

private:
    bool tryLoadKey(DeviceInfo &info);
    bool provisionNewKey(DeviceInfo &info, const std::vector<uint8_t> &providerKey,
                         const std::string &serverTimestamp);

    std::string m_encryptedDeviceKey;
    std::string m_deviceKeyPassword;
};

} // namespace detail
} // namespace scorbit
