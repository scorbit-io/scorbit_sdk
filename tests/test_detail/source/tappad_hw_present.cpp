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

// Its own translation unit: ListUsbDevices.h logs through Util.h's ERR, which must not meet the
// logger's macros of the same name.
#include "tappad_lock_test_path.h"

#include <nfc/Util.h>
#include <nfc/ListUsbDevices.h>

// cryptoauthlib builds hidapi in; only these two calls are needed, so no hidapi header.
extern "C" {
struct hid_device_info;
hid_device_info *hid_enumerate(unsigned short vendor_id, unsigned short product_id);
void hid_free_enumeration(hid_device_info *devs);
}

namespace tappad_lock_test {

bool tapPadMayBePresent()
{
    if (!nfc::listUsbDevices().empty()) {
        return true;
    }
    // The probe's HID interface, as Tpm's HID fallback opens it (tpm.cpp).
    hid_device_info *devices = hid_enumerate(0xCAFE, 0x4005);
    const bool present = devices != nullptr;
    hid_free_enumeration(devices);
    return present;
}

} // namespace tappad_lock_test
