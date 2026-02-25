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

#include "net.h"

namespace scorbit {

struct DeviceInfo;

namespace detail {

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
     */
    virtual bool tryResolve(DeviceInfo &info) = 0;

    /**
     * Create a signer callback from the resolved key material.
     * Must only be called after tryResolve() returns true.
     */
    virtual SignerCallback createSigner() const = 0;
};

} // namespace detail
} // namespace scorbit
