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

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__)

// ListUsbDevices.h logs through Util.h's ERR, as its other includers arrange.
#    include <nfc/Util.h>
#    include <nfc/ListUsbDevices.h>

#    include <thread>
#    include <vector>

// SB-5237: concurrent first calls must share one context, not each run libusb_init.
TEST_CASE("probeLibusbContext: concurrent callers share one context", "[nfc][libusb]")
{
    std::vector<libusb_context *> seen(8);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < seen.size(); ++i) {
        threads.emplace_back([&seen, i] { seen[i] = probeLibusbContext(); });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    for (auto *ctx : seen) {
        CHECK(ctx == seen.front());
    }
}

#endif
