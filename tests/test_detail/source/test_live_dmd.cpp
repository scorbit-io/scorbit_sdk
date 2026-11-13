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

#include "live_dmd.h"
#include "net.h"
#include <scorbit_sdk/scorbit_sdk.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <future>
#include <string>
#include <utility>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;
using json = nlohmann::json;

namespace {

constexpr auto CHANNEL = "live_dmd:2f0d6f2e-5a43-4c55-9d6e-0c4a2b7f8e11";
constexpr std::int64_t UNTIL = 1763049600;

json liveDmdMessage(json metadata, std::string name = "scorbitronLiveDmd")
{
    return {{"type", "action"},
            {"metadata", std::move(metadata)},
            {"payload", {{"name", std::move(name)}, {"method", "SIGNAL"}}}};
}

json validMetadata()
{
    return {{"channel", CHANNEL}, {"until", UNTIL}};
}

/// A Net whose events run on a local io_context: an unprovisioned Net never starts its worker.
/// io is declared first so it outlives the net's event manager, which holds a strand on it.
struct ControlNet {
    boost::asio::io_context io;
    std::vector<std::pair<std::string, std::int64_t>> liveDmd;
    int diagnostics = 0;
    Net net {DeviceInfo {}, {}};

    ControlNet()
    {
        auto onEvent = [this](const EventBase &event) {
            std::string channel;
            std::int64_t until = 0;
            if (Event(&event).eventLiveDmdRequested(channel, until)) {
                liveDmd.emplace_back(channel, until);
            } else if (event.type() == EventType::DiagnosticsUploadRequested) {
                ++diagnostics;
            }
        };
        NetTestAccess::setEventManager(
                net, std::make_shared<EventManager>(boost::asio::make_strand(io), onEvent));
    }

    void handle(const json &message) { NetTestAccess::handleControlScorbitron(net, message); }
    void drain() { io.run_for(std::chrono::milliseconds(50)); }
};

using PublishOutcome = std::pair<PublishResult, int>;

/// Publishes through Net and waits for the one result.
PublishOutcome publishThroughNet(Net &net, const std::string &channel, const std::string &data)
{
    std::promise<PublishOutcome> done;
    auto result = done.get_future();
    int calls = 0;
    net.publishRealtime(channel, data, [&](PublishResult r, int code) {
        if (++calls == 1) {
            done.set_value({r, code});
        }
    });
    REQUIRE(result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    return result.get();
}

} // namespace

TEST_CASE("parseLiveDmdSignal passes channel and until on unchanged")
{
    const auto signal = parseLiveDmdSignal(liveDmdMessage(validMetadata()));

    REQUIRE(signal);
    CHECK(signal->channel == CHANNEL);
    CHECK(signal->until == UNTIL);
}

TEST_CASE("parseLiveDmdSignal rejects a signal without a usable channel and until")
{
    const std::vector<json> metadata {
            json(nullptr),
            json::array(),
            {{"until", UNTIL}},
            {{"channel", CHANNEL}},
            {{"channel", ""}, {"until", UNTIL}},
            {{"channel", 7}, {"until", UNTIL}},
            {{"channel", CHANNEL}, {"until", "1763049600"}},
            {{"channel", CHANNEL}, {"until", 1763049600.5}},
    };
    for (const auto &m : metadata) {
        CAPTURE(m.dump());
        CHECK_FALSE(parseLiveDmdSignal(liveDmdMessage(m)));
    }
    CHECK_FALSE(parseLiveDmdSignal({{"type", "action"}}));
}

TEST_CASE("Each scorbitronLiveDmd signal raises one LiveDmdRequested, the latest replacing")
{
    ControlNet control;

    control.handle(liveDmdMessage(validMetadata()));
    control.handle(liveDmdMessage({{"channel", CHANNEL}, {"until", UNTIL + 20}}));
    control.drain();

    REQUIRE(control.liveDmd.size() == 2);
    CHECK(control.liveDmd[0] == std::make_pair(std::string {CHANNEL}, UNTIL));
    CHECK(control.liveDmd[1] == std::make_pair(std::string {CHANNEL}, UNTIL + 20));
}

TEST_CASE("Other control signals raise no LiveDmdRequested, and a malformed one raises nothing")
{
    ControlNet control;

    control.handle(liveDmdMessage(validMetadata(), "scorbitronUploadDiagnostics"));
    control.handle(liveDmdMessage({{"channel", CHANNEL}}));
    control.handle(liveDmdMessage(validMetadata(), "somethingElse"));
    auto get = liveDmdMessage(validMetadata());
    get["payload"]["method"] = "GET";
    control.handle(get);
    control.drain();

    CHECK(control.liveDmd.empty());
    CHECK(control.diagnostics == 1);
}

TEST_CASE("sb_event_live_dmd_requested reads only its own event")
{
    const LiveDmdRequestedEvent live {CHANNEL, UNTIL};
    const char *channel = nullptr;
    std::int64_t until = 0;

    REQUIRE(sb_event_live_dmd_requested(&live, &channel, &until));
    CHECK(std::string {channel} == CHANNEL);
    CHECK(until == UNTIL);
    CHECK(live.type() == EventType::LiveDmdRequested);
    CHECK(static_cast<int>(EventType::LiveDmdRequested) == SB_EVT_LIVE_DMD_REQUESTED);

    CHECK_FALSE(sb_event_live_dmd_requested(&live, nullptr, &until));
    CHECK_FALSE(sb_event_live_dmd_requested(&live, &channel, nullptr));
    CHECK_FALSE(sb_event_live_dmd_requested(nullptr, &channel, &until));
    const MachineChangedEvent other {"vm"};
    CHECK_FALSE(sb_event_live_dmd_requested(&other, &channel, &until));
}

TEST_CASE("checkPublishData refuses invalid JSON, an empty channel and anything over 48 KB")
{
    json parsed;
    CHECK(checkPublishData(CHANNEL, R"({"seq":1})", parsed) == PublishResult::Ok);
    CHECK(parsed == json {{"seq", 1}});

    CHECK(checkPublishData(CHANNEL, "{not json", parsed) == PublishResult::Invalid);
    CHECK(checkPublishData(CHANNEL, "", parsed) == PublishResult::Invalid);
    CHECK(checkPublishData("", R"({"seq":1})", parsed) == PublishResult::Invalid);

    const auto fill = [](std::size_t size) {
        return "\"" + std::string(size - 2, 'x') + "\""; // a JSON string of exactly size bytes
    };
    CHECK(checkPublishData(CHANNEL, fill(PUBLISH_MAX_BYTES), parsed) == PublishResult::Ok);
    CHECK(checkPublishData(CHANNEL, fill(PUBLISH_MAX_BYTES + 1), parsed)
          == PublishResult::TooLarge);
    CHECK(PUBLISH_MAX_BYTES == 48 * 1024);
}

TEST_CASE("toPublishResult maps every centrifugo failure to a plain failure")
{
    CHECK(toPublishResult(nullptr) == PublishOutcome {PublishResult::Ok, 0});

    const centrifugo::Error notConnected {centrifugo::ErrorType::NotConnected, "not connected"};
    CHECK(toPublishResult(&notConnected) == PublishOutcome {PublishResult::NotConnected, 0});

    // A failed publish proxy answers 100 (temporary); it is still a plain failure.
    const centrifugo::Error proxy {static_cast<centrifugo::ErrorType>(100), "internal"};
    CHECK(toPublishResult(&proxy) == PublishOutcome {PublishResult::Refused, 100});
    const centrifugo::Error denied {centrifugo::ErrorType::PermissionDenied, "denied"};
    CHECK(toPublishResult(&denied) == PublishOutcome {PublishResult::Refused, 103});
}

// Answered on the caller's thread; the not-connected case needs a running SDK, see test_interface.
TEST_CASE("Net publishRealtime refuses bad input before sending anything")
{
    Net net {DeviceInfo {}, {}};

    CHECK(publishThroughNet(net, CHANNEL, "{not json")
          == PublishOutcome {PublishResult::Invalid, 0});
    CHECK(publishThroughNet(net, "", "{}") == PublishOutcome {PublishResult::Invalid, 0});
    CHECK(publishThroughNet(net, CHANNEL, std::string(PUBLISH_MAX_BYTES + 1, ' '))
          == PublishOutcome {PublishResult::TooLarge, 0});
}
