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

// SB-6100: a host that links the SDK may carry its own global copies of the USB helpers, as
// scorbitd's spb does. Two different definitions of one inline function let the linker keep
// either one's statics, so the SDK's must live in nfc::. This file only compiles if they do:
// the host's global copies below would otherwise redefine them.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>
#include <vector>

// The host's own copies, in the global namespace, unlike the SDK's.
inline std::vector<std::string> listUsbDevices()
{
    return {"host"};
}

class CdcTpm
{
public:
    int host = 1;
};

#if defined(__linux__)
struct libusb_context;

inline libusb_context *probeLibusbContext()
{
    static libusb_context *hostContext = nullptr; // a different static from the SDK's
    return hostContext;
}

class LinuxCdcBulk
{
public:
    int host = 1;
};
#endif

#include <nfc/Util.h>
#include <nfc/CdcTpm.h>
#include <nfc/ListUsbDevices.h>

TEST_CASE("The SDK's USB helpers do not collide with a host's global copies", "[nfc][usb]")
{
    // The host's copies stay its own...
    CHECK(::listUsbDevices() == std::vector<std::string> {"host"});
    CHECK(::CdcTpm {}.host == 1);
    // ...and the SDK's are other entities, so neither can take the other's statics.
    STATIC_CHECK_FALSE(std::is_same_v<::CdcTpm, nfc::CdcTpm>);
    CHECK(reinterpret_cast<void *>(&::listUsbDevices)
          != reinterpret_cast<void *>(&nfc::listUsbDevices));
#if defined(__linux__)
    STATIC_CHECK_FALSE(std::is_same_v<::LinuxCdcBulk, nfc::LinuxCdcBulk>);
    (void)nfc::probeLibusbContext(); // the SDK's static, whatever libusb_init gives it here
    CHECK(::probeLibusbContext() == nullptr); // the host's own static is untouched
#endif
}
