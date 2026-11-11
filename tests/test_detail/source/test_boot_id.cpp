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

// SB-6523: every Scorbitron object PATCH carries a boot_id that lasts as long as its Net, so the
// API can tell a Net that lost its sessions from one re-sending the object.
//
// Every Net here is built from a DeviceInfo that does not validate, so its worker never starts and
// its object is built by NetTestAccess::scorbitronObject(), as a validated one's constructor does.

#include "device_info.h"
#include "net.h"
#include "net_util.h"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

using namespace scorbit;
using namespace scorbit::detail;

namespace {

/// The boot_id in the body the Scorbitron object's PATCH sends now.
std::string sentBootId(const Net &net)
{
    return nlohmann::json::parse(NetTestAccess::scorbitronObjectBody(net)).at("boot_id");
}

} // namespace

TEST_CASE("The Scorbitron object's PATCH carries a boot ID", "[boot_id]")
{
    Net net(DeviceInfo {}, {});
    NetTestAccess::scorbitronObject(net);

    CHECK(isUuid(sentBootId(net)));
}

TEST_CASE("A Net sends the same boot ID in every Scorbitron object", "[boot_id]")
{
    Net net(DeviceInfo {}, {});
    NetTestAccess::scorbitronObject(net);
    const auto bootId = sentBootId(net);

    SECTION("when the pairing check's PATCH fails and is retried")
    {
        NetTestAccess::setStatus(net, AuthStatus::AuthenticatedCheckingPairing);
        const auto backoff = NetTestAccess::scorbitronRetryBackoff(net);
        // As the PATCH's reply callback does with a failed reply.
        NetTestAccess::scorbitronObjectSendDone(net, false);
        NetTestAccess::scorbitronReplyFailed(net);
        REQUIRE(NetTestAccess::scorbitronRetryBackoff(net) > backoff); // the retry is armed
        CHECK(sentBootId(net) == bootId);
    }
    SECTION("when the object is built again")
    {
        NetTestAccess::scorbitronObject(net);
        CHECK(sentBootId(net) == bootId);
    }
}

TEST_CASE("A new Net sends a new boot ID", "[boot_id]")
{
    Net first(DeviceInfo {}, {});
    NetTestAccess::scorbitronObject(first);
    Net second(DeviceInfo {}, {});
    NetTestAccess::scorbitronObject(second);

    CHECK(sentBootId(first) != sentBootId(second));
}
