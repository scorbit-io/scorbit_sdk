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

#include "signer_types.h"
#include <functional>
#include <string>

namespace scorbit {

struct DeviceInfo;

namespace detail {

/// Outcome of trying the resolver chain.
enum class KeyResolution {
    Resolved, ///< a key was found
    Deferred, ///< a resolver's hardware is busy; ask the chain again later
    Failed,   ///< no resolver can provide a key
};

class IKeyResolver
{
public:
    virtual ~IKeyResolver() = default;

    IKeyResolver() = default;
    IKeyResolver(const IKeyResolver &) = delete;
    IKeyResolver &operator=(const IKeyResolver &) = delete;
    IKeyResolver(IKeyResolver &&) = default;
    IKeyResolver &operator=(IKeyResolver &&) = default;

    /**
     * Attempt to resolve authentication for this device.
     * On success: populates info.uuid and info.serialNumber, returns true.
     * @param serverTimestamp Unix timestamp string obtained from server for accurate time.
     */
    virtual bool tryResolve(DeviceInfo &info, const std::string &serverTimestamp) = 0;

    /**
     * After tryResolve() returned false: true if this resolver could not decide yet (its
     * hardware was busy) and should be asked again later, before any resolver after it is tried.
     */
    virtual bool shouldRetryLater() const { return false; }

    /// After tryResolve() returned true: whether the key lives in the tap pad's TPM (SB-6133).
    virtual bool isHardwareKey() const { return false; }

    /**
     * Create a signer callback from the resolved key material.
     * Must only be called after tryResolve() returns true.
     */
    virtual SignerCallback createSigner() const = 0;

    /**
     * Optional, alongside createSigner(): after the signer returned an empty signature, whether
     * that was because the key's hardware was busy (not broken). Empty when it cannot be.
     */
    virtual std::function<bool()> createBusyCheck() const { return {}; }
};

} // namespace detail
} // namespace scorbit
