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

#include "utils/lan_ip.h"
#include <catch2/catch_test_macros.hpp>
#include <boost/asio/ip/address.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail;
using boost::asio::ip::make_address;

TEST_CASE("LAN IP filter rejects non-routable addresses", "[lan_ip]")
{
    CHECK_FALSE(isUsableLanAddress(make_address("0.0.0.0")));
    CHECK_FALSE(isUsableLanAddress(make_address("127.0.0.1")));
    CHECK_FALSE(isUsableLanAddress(make_address("169.254.1.2")));
    CHECK_FALSE(isUsableLanAddress(make_address("224.0.0.1")));

    CHECK_FALSE(isUsableLanAddress(make_address("::")));
    CHECK_FALSE(isUsableLanAddress(make_address("::1")));
    CHECK_FALSE(isUsableLanAddress(make_address("fe80::1")));
    CHECK_FALSE(isUsableLanAddress(make_address("ff02::1")));
}

TEST_CASE("LAN IP filter accepts routable addresses", "[lan_ip]")
{
    CHECK(isUsableLanAddress(make_address("192.168.1.42")));
    CHECK(isUsableLanAddress(make_address("10.0.0.5")));
    CHECK(isUsableLanAddress(make_address("172.16.0.5")));
    CHECK(isUsableLanAddress(make_address("2001:db8::1")));
}

TEST_CASE("Primary LAN IP discovery returns usable address when available", "[lan_ip]")
{
    const auto lanIp = getPrimaryLanIp();
    if (!lanIp.empty()) {
        CHECK(isUsableLanAddress(make_address(lanIp)));
    } else {
        WARN("Primary LAN IP could not be discovered, likely due to missing default route");
    }
}
