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

// The connectivity evidence a host polls (sb_realtime_connected and its siblings).

#include "event_manager.h"
#include "net.h"
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;

namespace {

cpr::Response response(int status, std::string body = {})
{
    cpr::Response r;
    r.status_code = status;
    r.text = std::move(body);
    return r;
}

/// An unprovisioned Net (its worker never starts) that reads the VPIN table mappings, so a
/// mappings read drives the shared request helper through a stand-in transport.
struct ConnectivityNet {
    boost::asio::io_context io;
    std::unique_ptr<Net> net;

    ConnectivityNet()
    {
        DeviceInfo info;
        info.vpinTableMappings = true;
        net = std::make_unique<Net>(std::move(info), std::vector<std::unique_ptr<IKeyResolver>> {});
        NetTestAccess::setEventManager(*net,
                                       std::make_shared<EventManager>(boost::asio::make_strand(io),
                                                                      [](const EventBase &) {}));
        NetTestAccess::setStatus(*net, AuthStatus::AuthenticatedPaired);
        NetTestAccess::setProbesManager(*net, std::make_shared<nfc::ProbesManager>(nullptr));
    }

    /// Answers every mappings read with @p reply, and reads once.
    void readWith(const cpr::Response &reply)
    {
        NetTestAccess::setVpinTransport(
                *net, {.get = [reply](const cpr::Url &, const cpr::Header &) { return reply; }});
        NetTestAccess::fetchTableMappingsNow(*net);
    }
};

} // namespace

TEST_CASE("Before any traffic there is no evidence either way", "[connectivity]")
{
    ConnectivityNet c;
    CHECK_FALSE(c.net->realtimeConnected());
    CHECK(c.net->secondsSinceRestSuccess() == -1);
    CHECK(c.net->restTransportFailures() == 0);
    CHECK(c.net->secondsSinceHeartbeatReply() == -1);
}

TEST_CASE("Only attempts with no HTTP response count as transport failures", "[connectivity]")
{
    ConnectivityNet c;

    // No response at all: each of the helper's attempts counts
    c.readWith(response(0));
    const auto afterLoss = c.net->restTransportFailures();
    CHECK(afterLoss >= 2);
    CHECK(c.net->secondsSinceRestSuccess() == -1);

    // Any HTTP status is an answer: it neither counts nor resets
    c.readWith(response(503));
    CHECK(c.net->restTransportFailures() == afterLoss);
    c.readWith(response(403, R"({"errors":[{"code":"no_owner"}]})"));
    CHECK(c.net->restTransportFailures() == afterLoss);

    // A success resets the count and starts the clock
    c.readWith(response(200, R"({"server_time":"t","mappings":[]})"));
    CHECK(c.net->restTransportFailures() == 0);
    const auto since = c.net->secondsSinceRestSuccess();
    CHECK(since >= 0);
    CHECK(since <= 1);

    // And a later loss counts again from zero
    c.readWith(response(0));
    CHECK(c.net->restTransportFailures() == afterLoss);
}

TEST_CASE("Any REST success resets the transport failures", "[connectivity]")
{
    ConnectivityNet c;
    c.readWith(response(0));
    REQUIRE(c.net->restTransportFailures() > 0);
    NetTestAccess::noteRestSuccess(*c.net, {});
    CHECK(c.net->restTransportFailures() == 0);
    CHECK(c.net->secondsSinceRestSuccess() >= 0);
}

TEST_CASE("The realtime state is true only while connected", "[connectivity]")
{
    ConnectivityNet c;
    NetTestAccess::setRealtimeConnected(*c.net, true);
    CHECK(c.net->realtimeConnected());
    NetTestAccess::setRealtimeConnected(*c.net, false);
    CHECK_FALSE(c.net->realtimeConnected());
}
