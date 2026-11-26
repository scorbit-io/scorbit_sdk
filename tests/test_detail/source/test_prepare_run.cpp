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

// Prepare runs: the steps a unit declares, the pending run it picks up (at start, on a reconnect
// and on a push), its step results resent until the run's deadline, and the release track
// re-read on demand.

#include "net.h"
#include "device_info.h"
#include <scorbit_sdk/scorbit_sdk.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

constexpr auto RUN_A = "3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e55";
constexpr auto RUN_B = "9b1d6c2a-4e3f-4b8a-8c7d-6e5f4a3b2c1d";
constexpr auto STEPS_PATH = "internal/api/diagnostics/prepare/{run_id}/steps/";

struct IoHolder {
    boost::asio::io_context io;
};

DeviceInfo preparing(std::string scorbitdVersion = "0.8.5")
{
    DeviceInfo info;
    info.prepareSteps = {"device_assess", "recheck_track"};
    info.scorbitdVersion = std::move(scorbitdVersion);
    return info;
}

/// A Net that records its Prepare requests and the runs it hands over instead of sending.
struct PrepareNet : IoHolder, Net {
    struct Call {
        HttpStatusCallback callback;
        bool startup;
    };
    struct Post {
        std::string path;
        std::string runId;
        json body;
        HttpStatusCallback callback;
    };

    explicit PrepareNet(DeviceInfo info = preparing())
        : Net(std::move(info), {})
    {
        NetTestAccess::setEventManager(
                *this, std::make_shared<EventManager>(boost::asio::make_strand(io),
                                                      [this](const EventBase &event) {
                                                          std::string runId;
                                                          std::string request;
                                                          if (Event(&event).eventPrepareRunRequested(
                                                                      runId, request)) {
                                                              runs.emplace_back(runId, request);
                                                          }
                                                      }));
    }

    void getPreparePending(HttpStatusCallback callback, bool startup) override
    {
        pendings.push_back({std::move(callback), startup});
    }

    void postProbeFlash(const char *pathTemplate, const std::string &runId, std::string body,
                        HttpStatusCallback callback) override
    {
        posts.push_back({pathTemplate, runId, json::parse(body), std::move(callback)});
    }

    void sendScorbitronObjectThen(std::function<void(bool sent, const std::string &reply)> after) override
    {
        patches.push_back(std::move(after));
    }

    void requestReleaseTrackFor(std::string url, RecheckCallback report) override
    {
        trackUrls.push_back(std::move(url));
        trackReads.push_back(std::move(report));
    }
    std::vector<std::string> trackUrls;

    /// Answers the last pending GET as the API would, then drains the events it raised.
    void answerPending(Error error, int httpStatus, const std::string &body)
    {
        pendings.back().callback(error, httpStatus, body);
        io.run_for(50ms);
        io.restart();
    }

    std::vector<Call> pendings;
    std::vector<Post> posts;
    std::vector<std::function<void(bool, const std::string &)>> patches;
    std::vector<RecheckCallback> trackReads;
    std::vector<std::pair<std::string, std::string>> runs;
};

std::string runBody(const char *runId, const char *deadline = "2099-01-01T00:00:00Z")
{
    return json {{"run_id", runId},
                 {"deadline", deadline},
                 {"steps", {{{"name", "device_assess"}}, {{"name", "recheck_track"}}}}}
            .dump();
}

json signal(const char *name)
{
    return {{"type", "action"}, {"payload", {{"method", "SIGNAL"}, {"name", name}}}};
}

struct Recorded {
    int calls = 0;
    Error error {Error::Unknown};
    int status = -1;
};

HttpStatusCallback recordTo(Recorded &r)
{
    return [&r](Error error, int status, const std::string &) {
        ++r.calls;
        r.error = error;
        r.status = status;
    };
}

} // namespace

// ------------------------------------ declared steps ------------------------------------

TEST_CASE("The declared Prepare steps are reported on the Scorbitron object, in order, once each")
{
    auto *config = sb_config_create();
    sb_config_add_prepare_step(config, "device_assess");
    sb_config_add_prepare_step(config, "recheck_track");
    sb_config_add_prepare_step(config, "device_assess");
    sb_config_add_prepare_step(config, "");
    sb_config_add_prepare_step(config, nullptr);
    const DeviceInfo info = static_cast<DeviceInfo &>(*config);
    sb_config_destroy(config);
    CHECK(info.prepareSteps == std::vector<std::string> {"device_assess", "recheck_track"});

    PrepareNet net(info);
    CHECK(NetTestAccess::scorbitronObject(net).at("prepare_steps")
          == json::array({"device_assess", "recheck_track"}));

    PrepareNet none {DeviceInfo {}};
    CHECK(NetTestAccess::scorbitronObject(none).at("prepare_steps") == json::array());
}

// ------------------------------------- pickup -------------------------------------

TEST_CASE("The pending run is asked for at start on the startup queue, and at once on reconnect")
{
    PrepareNet net;
    NetTestAccess::requestPreparePending(net, true);
    NetTestAccess::requestPreparePending(net, false);
    REQUIRE(net.pendings.size() == 2);
    CHECK(net.pendings[0].startup);
    CHECK_FALSE(net.pendings[1].startup);
}

TEST_CASE("A unit that declared no Prepare steps never asks for a run")
{
    PrepareNet net {DeviceInfo {}};
    NetTestAccess::requestPreparePending(net, true);
    NetTestAccess::handleControlScorbitron(net, signal("scorbitronPrepareRun"));
    CHECK(net.pendings.empty());
}

TEST_CASE("The Prepare push asks for the pending run; other signals don't")
{
    PrepareNet net;
    NetTestAccess::handleControlScorbitron(net, signal("scorbitronPrepareRun"));
    REQUIRE(net.pendings.size() == 1);
    CHECK_FALSE(net.pendings[0].startup);

    NetTestAccess::handleControlScorbitron(net, signal("scorbitronSomethingElse"));
    CHECK(net.pendings.size() == 1);
}

TEST_CASE("A pending run is handed over once per run_id, as the server sent it")
{
    PrepareNet net;
    NetTestAccess::requestPreparePending(net, true);
    const auto body = runBody("3F2B8C1E-8D4A-4A57-9A3E-2C6F1D0B7E55");
    net.answerPending(Error::Success, 200, body);
    REQUIRE(net.runs.size() == 1);
    CHECK(net.runs[0].first == RUN_A);
    CHECK(net.runs[0].second == body);

    // The API keeps answering with the run until its steps are final: a push or a reconnect
    NetTestAccess::requestPreparePending(net, false);
    net.answerPending(Error::Success, 200, runBody(RUN_A));
    CHECK(net.runs.size() == 1);

    NetTestAccess::requestPreparePending(net, false);
    net.answerPending(Error::Success, 200, runBody(RUN_B));
    REQUIRE(net.runs.size() == 2);
    CHECK(net.runs[1].first == RUN_B);
}

TEST_CASE("No run, a failed ask or a malformed reply raises nothing")
{
    PrepareNet net;
    NetTestAccess::requestPreparePending(net, false);
    net.answerPending(Error::Success, 204, "");
    net.answerPending(Error::ApiError, 0, "");
    net.answerPending(Error::ApiError, 503, "");
    net.answerPending(Error::Success, 200, "not json");
    net.answerPending(Error::Success, 200, R"({"deadline":"2099-01-01T00:00:00Z"})");
    net.answerPending(Error::Success, 200, R"({"run_id":"not-a-uuid"})");
    net.answerPending(Error::Success, 200, R"({"run_id":7})");
    CHECK(net.runs.empty());
}

TEST_CASE("A run without a valid deadline or steps is ignored, and its corrected reply is handed "
          "over")
{
    PrepareNet net;
    NetTestAccess::requestPreparePending(net, false);
    net.answerPending(Error::Success, 200,
                      json {{"run_id", RUN_A}, {"steps", json::array()}}.dump());
    net.answerPending(
            Error::Success, 200,
            json {{"run_id", RUN_A}, {"deadline", "soon"}, {"steps", json::array()}}.dump());
    net.answerPending(Error::Success, 200,
                      json {{"run_id", RUN_A}, {"deadline", "2099-01-01T00:00:00Z"}}.dump());
    net.answerPending(Error::Success, 200,
                      json {{"run_id", RUN_A},
                            {"deadline", "2099-01-01T00:00:00Z"},
                            {"steps", json::array({"device_assess"})}}
                              .dump());
    // An empty steps list is not a run: nothing waiting is a 204
    net.answerPending(
            Error::Success, 200,
            json {{"run_id", RUN_A}, {"deadline", "2099-01-01T00:00:00Z"}, {"steps", json::array()}}
                    .dump());
    CHECK(net.runs.empty());

    net.answerPending(Error::Success, 200, runBody(RUN_A));
    REQUIRE(net.runs.size() == 1);
    CHECK(net.runs[0].first == RUN_A);
}

TEST_CASE("Every run is handed over once per process, however many runs come between")
{
    PrepareNet net;
    NetTestAccess::requestPreparePending(net, false);
    std::vector<std::string> ids;
    for (int i = 0; i < 12; ++i) {
        ids.push_back(fmt::format("3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e{:02x}", i));
        net.answerPending(Error::Success, 200, runBody(ids.back().c_str()));
    }
    REQUIRE(net.runs.size() == 12);
    net.answerPending(Error::Success, 200, runBody(ids.front().c_str()));
    CHECK(net.runs.size() == 12);
}

// ----------------------------------- step results -----------------------------------

TEST_CASE("A step result is posted to its run's steps endpoint; 2xx and 409 end it")
{
    PrepareNet net;
    Recorded accepted;
    net.submitPrepareStep("3F2B8C1E-8D4A-4A57-9A3E-2C6F1D0B7E55",
                          R"({"name":"device_assess","status":"ok"})", recordTo(accepted));
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].path == STEPS_PATH);
    CHECK(net.posts[0].runId == RUN_A);
    CHECK(net.posts[0].body.at("status") == "ok");
    net.posts[0].callback(Error::Success, 202, "{}");
    CHECK(accepted.calls == 1);
    CHECK(accepted.status == 202);

    Recorded conflict;
    net.submitPrepareStep(RUN_A, R"({"name":"recheck_track","status":"fixed"})",
                          recordTo(conflict));
    net.posts.back().callback(Error::ApiError, 409, "{}");
    CHECK(conflict.calls == 1);
    CHECK(conflict.status == 409);
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
}

TEST_CASE("A transient failure is resent while the run's deadline allows")
{
    PrepareNet net;
    NetTestAccess::requestPreparePending(net, false);
    net.answerPending(Error::Success, 200, runBody(RUN_A, "2099-01-01T00:00:00Z"));

    Recorded r;
    net.submitPrepareStep(RUN_A, R"({"name":"device_assess","status":"ok"})", recordTo(r));
    net.posts.back().callback(Error::ApiError, 503, "");
    CHECK(r.calls == 0); // not final yet
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 1);

    // The resend goes to the same endpoint
    NetTestAccess::resendDueProbeFlashResults(net, std::chrono::steady_clock::now() + 1h);
    REQUIRE(net.posts.size() == 2);
    CHECK(net.posts[1].path == STEPS_PATH);
    net.posts[1].callback(Error::ApiError, 0, ""); // no response
    CHECK(r.calls == 0);
    NetTestAccess::resendDueProbeFlashResults(net, std::chrono::steady_clock::now() + 1h);
    net.posts.back().callback(Error::Success, 202, "{}");
    CHECK(r.calls == 1);
    CHECK(r.status == 202);
}

TEST_CASE("A step result is not resent past its run's deadline")
{
    PrepareNet net;
    NetTestAccess::requestPreparePending(net, false);
    net.answerPending(Error::Success, 200, runBody(RUN_A, "2000-01-01T00:00:00Z"));

    Recorded r;
    net.submitPrepareStep(RUN_A, R"({"name":"device_assess","status":"ok"})", recordTo(r));
    net.posts.back().callback(Error::ApiError, 503, "");
    CHECK(r.calls == 1);
    CHECK(r.status == 503);
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
}

TEST_CASE("A resend held past the run's deadline is not sent; the last answer is final")
{
    PrepareNet net;
    NetTestAccess::requestPreparePending(net, false);
    net.answerPending(Error::Success, 200, runBody(RUN_A, "2099-01-01T00:00:00Z"));

    Recorded r;
    net.submitPrepareStep(RUN_A, R"({"name":"device_assess","status":"ok"})", recordTo(r));
    net.posts.back().callback(Error::ApiError, 503, "");
    REQUIRE(r.calls == 0);

    // The timer fires only after the deadline (clamped to a day ahead) has passed
    NetTestAccess::resendDueProbeFlashResults(net, std::chrono::steady_clock::now() + 25h);
    CHECK(net.posts.size() == 1);
    CHECK(r.calls == 1);
    CHECK(r.status == 503);
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
}

TEST_CASE("A step result for a run this process was not handed is resent up to 30 times")
{
    PrepareNet net; // no pending run seen, so no deadline is known
    Recorded r;
    net.submitPrepareStep(RUN_B, R"({"name":"device_assess","status":"ok"})", recordTo(r));
    for (int attempt = 1; attempt < 30; ++attempt) {
        net.posts.back().callback(Error::ApiError, 503, "");
        REQUIRE(r.calls == 0);
        NetTestAccess::resendDueProbeFlashResults(net, std::chrono::steady_clock::now() + 24h);
    }
    REQUIRE(net.posts.size() == 30);
    net.posts.back().callback(Error::ApiError, 503, "");
    CHECK(r.calls == 1);
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
}

TEST_CASE("A step result for a bad run id or body is not sent")
{
    PrepareNet net;
    Recorded badRun;
    net.submitPrepareStep("nope", R"({"name":"device_assess"})", recordTo(badRun));
    Recorded badBody;
    net.submitPrepareStep(RUN_A, "[1]", recordTo(badBody));
    CHECK(net.posts.empty());
    CHECK(badRun.calls == 1);
    CHECK(badRun.error == Error::Unknown);
    CHECK(badBody.calls == 1);
}

TEST_CASE("sb_event_prepare_run_requested reads only its own event")
{
    const PrepareRunRequestedEvent run(RUN_A, runBody(RUN_A));
    const char *runId = nullptr;
    const char *request = nullptr;
    REQUIRE(sb_event_prepare_run_requested(&run, &runId, &request));
    CHECK(std::string(runId) == RUN_A);
    CHECK(json::parse(request).at("steps").size() == 2);

    const ProbeFlashRequestedEvent flash(RUN_A, "{}");
    CHECK_FALSE(sb_event_prepare_run_requested(&flash, &runId, &request));
    CHECK_FALSE(sb_event_prepare_run_requested(nullptr, &runId, &request));
}

// ---------------------------------- release track recheck ----------------------------------

namespace {

struct Answers {
    std::vector<std::pair<RecheckResult, std::string>> list;
    RecheckCallback callback()
    {
        return [this](RecheckResult result, const std::string &detail) {
            list.emplace_back(result, detail);
        };
    }
};

// Scorbitron PATCH replies, with and without a release track
constexpr auto TRACKED =
        R"({"release_track":{"id":"lab","url":"https://example.com/api/v2/release-tracks/lab/"}})";
constexpr auto UNTRACKED = R"({"release_track":null})";

} // namespace

TEST_CASE("A recheck re-sends the Scorbitron object, then reads the track it names")
{
    PrepareNet net;
    Answers answers;
    net.recheckReleaseTrack(answers.callback());
    REQUIRE(net.patches.size() == 1);
    CHECK(net.trackReads.empty());

    net.patches[0](true, TRACKED);
    REQUIRE(net.trackReads.size() == 1);
    // The url of this reply, whatever the reply's other parts left in the SDK's state
    CHECK(net.trackUrls[0] == "https://example.com/api/v2/release-tracks/lab/");
    CHECK(answers.list.empty());
}

TEST_CASE("A recheck fails when the object was not sent or names no track")
{
    PrepareNet net;
    Answers notSent;
    net.recheckReleaseTrack(notSent.callback());
    net.patches.back()(false, "");
    REQUIRE(notSent.list.size() == 1);
    CHECK(notSent.list[0].first == RecheckResult::Failed);
    CHECK(net.trackReads.empty());

    Answers noTrack;
    net.recheckReleaseTrack(noTrack.callback());
    net.patches.back()(true, "{}");
    REQUIRE(noTrack.list.size() == 1);
    CHECK(noTrack.list[0].second == "no release track");
}

TEST_CASE("A recheck reads only the track its own reply names, never one kept from before")
{
    PrepareNet net;
    Answers tracked;
    net.recheckReleaseTrack(tracked.callback());
    net.patches.back()(true, TRACKED);
    REQUIRE(net.trackReads.size() == 1);

    // The unit has since left its track: the url the earlier reply gave must not be read
    Answers left;
    net.recheckReleaseTrack(left.callback());
    net.patches.back()(true, UNTRACKED);
    CHECK(net.trackReads.size() == 1);
    REQUIRE(left.list.size() == 1);
    CHECK(left.list[0].first == RecheckResult::Failed);
    CHECK(left.list[0].second == "no release track");
}

TEST_CASE("A recheck is answered once: up to date, or failed when the track can't be read")
{
    PrepareNet net;

    Answers current;
    net.recheckReleaseTrack(current.callback());
    net.patches.back()(true, TRACKED);
    const json track {{"id", "lab"},
                      {"scorbitd", {{"version", "0.8.5"}, {"assets_json", json::array()}}}};
    NetTestAccess::checkReleaseTrackReply(net, Error::Success, track.dump(),
                                          net.trackReads.back());
    REQUIRE(current.list.size() == 1);
    CHECK(current.list[0].first == RecheckResult::UpToDate);
    CHECK(current.list[0].second == "track lab running 0.8.5 offers 0.8.5");

    Answers unreadable;
    net.recheckReleaseTrack(unreadable.callback());
    net.patches.back()(true, TRACKED);
    NetTestAccess::checkReleaseTrackReply(net, Error::ApiError, "", net.trackReads.back());
    NetTestAccess::checkReleaseTrackReply(net, Error::ApiError, "", net.trackReads.back());
    REQUIRE(unreadable.list.size() == 1);
    CHECK(unreadable.list[0].first == RecheckResult::Failed);

    Answers garbled;
    net.recheckReleaseTrack(garbled.callback());
    net.patches.back()(true, TRACKED);
    NetTestAccess::checkReleaseTrackReply(net, Error::Success, "not json", net.trackReads.back());
    REQUIRE(garbled.list.size() == 1);
    CHECK(garbled.list[0].first == RecheckResult::Failed);
}
