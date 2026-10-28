/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "nfc_tpm_key_resolver.h"
#include "soft_key_resolver.h"

#include <memory>
#include <vector>

namespace scorbit {
namespace detail {

/// The key resolvers for a device with no signer: a tap pad TPM first, then the soft key, unless
/// @p softKeyOnly asks for the soft key alone.
inline auto makeUnsignedKeyResolvers(bool softKeyOnly)
        -> std::vector<std::unique_ptr<IKeyResolver>>
{
    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    if (!softKeyOnly) {
        resolvers.push_back(std::make_unique<NfcTpmKeyResolver>());
    }
    resolvers.push_back(std::make_unique<SoftKeyResolver>());
    return resolvers;
}

} // namespace detail
} // namespace scorbit
