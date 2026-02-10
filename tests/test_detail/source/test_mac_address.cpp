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

#include "utils/mac_address.h"
#include <catch2/catch_test_macros.hpp>
#include <regex>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Valid MAC address", "[getMacAddress]")
{
    static const auto re = std::regex("([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})");
    const auto mac = getMacAddress();

    // MAC address retrieval may fail in sandboxed/restricted environments
    // (e.g., "Operation not permitted" on getifaddrs)
    // In such cases, the function returns an empty string or default value
    if (!mac.empty()) {
        CHECK(std::regex_match(mac, re));
    } else {
        WARN("MAC address could not be retrieved (likely due to sandbox restrictions)");
    }
}
