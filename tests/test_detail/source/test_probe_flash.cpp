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

// probe_flash: the control-message router, the capability advertised on the Scorbitron object,
// and the stage and result POSTs (SB-5764).

#include "net.h"
#include "net_util.h"
#include <scorbit_sdk/scorbit_sdk.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

constexpr auto RUN_A = "3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e55";
constexpr auto RUN_B = "9b1d6c2a-4e3f-4b8a-8c7d-6e5f4a3b2c1d";
constexpr auto RUN_C = "0a1b2c3d-4e5f-4a6b-9c8d-7e6f5a4b3c2d";
constexpr auto SHA = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

constexpr auto STAGE_PATH = "internal/api/diagnostics/probe-flash-stage/{run_id}/";
constexpr auto RESULT_PATH = "internal/api/diagnostics/probe-flash-result/{run_id}/";

json flashPayload(const char *runId, const char *probe = "cpu")
{
    return {{"run_id", runId},     {"probe", probe},        {"target_build", 20261025091656u},
            {"sha256", SHA},       {"size", 74240u},        {"deadline", "2099-01-01T00:00:00Z"}};
}

/// Destroyed after the Net below: the Net's event manager holds a strand on it.
struct IoHolder {
    boost::asio::io_context io;
};

/// A Net that records each probe_flash POST, and the requests it raises, instead of sending.
struct FlashNet : IoHolder, Net {
    struct Post {
        std::string path;
        std::string runId;
        json body;
        HttpStatusCallback callback;
    };

    explicit FlashNet(DeviceInfo info = {})
        : Net(std::move(info), {})
    {
        NetTestAccess::setEventManager(
                *this, std::make_shared<EventManager>(boost::asio::make_strand(io),
                                                      [this](const EventBase &event) {
                                                          std::string runId;
                                                          std::string request;
                                                          if (Event(&event).eventProbeFlashRequested(
                                                                      runId, request)) {
                                                              requests.push_back(json::parse(request));
                                                          }
                                                      }));
    }

    void postProbeFlash(const char *pathTemplate, const std::string &runId, std::string body,
                        HttpStatusCallback callback) override
    {
        posts.push_back({pathTemplate, runId, json::parse(body), std::move(callback)});
    }

    void startNfcFlash(json request) override { nfcRuns.push_back(std::move(request)); }

    void handle(const json &payload)
    {
        NetTestAccess::handleProbeFlash(*this, payload);
        io.run_for(50ms);
        io.restart();
    }

    /// Answers the last POST as the API would.
    void answer(Error error, int httpStatus) { posts.back().callback(error, httpStatus, "{}"); }

    std::vector<Post> posts;
    std::vector<json> requests;
    std::vector<json> nfcRuns; ///< nfc runs handed to the SDK's own flash
};

DeviceInfo flashesCpuAndDmd()
{
    DeviceInfo info;
    info.probeFlashProbes = {"cpu", "dmd"};
    return info;
}

} // namespace

TEST_CASE("probe_flash for a probe the host flashes raises one request with every field")
{
    FlashNet net(flashesCpuAndDmd());

    net.handle(flashPayload(RUN_A, "dmd"));

    REQUIRE(net.requests.size() == 1);
    const auto &request = net.requests[0];
    CHECK(request.at("run_id") == RUN_A);
    CHECK(request.at("probe") == "dmd");
    CHECK(request.at("target_build") == 20261025091656u);
    CHECK(request.at("sha256") == SHA);
    CHECK(request.at("size") == 74240u);
    CHECK(request.at("deadline") == "2099-01-01T00:00:00Z");
    const auto imageUrl = request.at("image_url").get<std::string>();
    CHECK(imageUrl.ends_with(std::string {"/internal/api/diagnostics/probe-flash/"} + RUN_A
                             + "/image/"));
    CHECK(net.posts.empty());
}

TEST_CASE("probe_flash for a probe not flashed here is refused as unsupported")
{
    DeviceInfo info;
    info.probeFlashProbes = {"cpu"};
    info.probeFlashNfcTrigger = true; // no effect: this process cannot open a pad here
    FlashNet net(info);

    SECTION("a host probe the host did not list")
    {
        net.handle(flashPayload(RUN_A, "dmd"));
    }
    SECTION("the NFC pad")
    {
        net.handle(flashPayload(RUN_A, "nfc"));
    }

    CHECK(net.requests.empty());
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].path == RESULT_PATH);
    CHECK(net.posts[0].runId == RUN_A);
    CHECK(net.posts[0].body == json {{"status", "refused"}, {"reason", "unsupported"}});
}

TEST_CASE("One flash runs per unit until its result is submitted")
{
    FlashNet net(flashesCpuAndDmd());
    net.handle(flashPayload(RUN_A, "cpu"));
    REQUIRE(net.requests.size() == 1);

    net.handle(flashPayload(RUN_B, "dmd")); // another probe, same unit
    CHECK(net.requests.size() == 1);
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].runId == RUN_B);
    CHECK(net.posts[0].body == json {{"status", "refused"}, {"reason", "already_running"}});

    net.submitProbeFlashResult(RUN_A, R"({"status":"succeeded","stage":"done"})");
    REQUIRE(net.posts.size() == 2);
    CHECK(net.posts[1].runId == RUN_A);

    net.handle(flashPayload(RUN_C, "dmd"));
    CHECK(net.requests.size() == 2);
}

TEST_CASE("A flash whose result never came blocks only until its window has passed")
{
    FlashNet net(flashesCpuAndDmd());
    auto stale = flashPayload(RUN_A);
    stale["deadline"] = "2020-01-01T00:00:00Z"; // already past: still held for the flash window
    net.handle(stale);
    REQUIRE(net.requests.size() == 1);

    net.handle(flashPayload(RUN_B));
    CHECK(net.requests.size() == 1);
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].body.at("reason") == "already_running");

    NetTestAccess::expireProbeFlashRun(net);
    net.handle(flashPayload(RUN_C));
    CHECK(net.requests.size() == 2);
}

TEST_CASE("An upper-case run id is the same run as its lower-case form")
{
    FlashNet net(flashesCpuAndDmd());
    net.handle(flashPayload("3F2B8C1E-8D4A-4A57-9A3E-2C6F1D0B7E55"));
    REQUIRE(net.requests.size() == 1);
    CHECK(net.requests[0].at("run_id") == RUN_A);

    net.handle(flashPayload(RUN_A)); // the same run again
    CHECK(net.requests.size() == 1);

    net.submitProbeFlashResult("3F2B8C1E-8D4A-4A57-9A3E-2C6F1D0B7E55", R"({"status":"deferred"})");
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].runId == RUN_A);
    net.handle(flashPayload(RUN_B)); // the result ended the run
    CHECK(net.requests.size() == 2);
}

TEST_CASE("A malformed probe_flash fails with invalid_policy and blocks nothing")
{
    FlashNet net(flashesCpuAndDmd());
    auto payload = flashPayload(RUN_A);

    SECTION("an unknown probe") { payload["probe"] = "spike"; }
    SECTION("a build that is not 14 digits") { payload["target_build"] = 2026102509165u; }
    SECTION("a build sent as text") { payload["target_build"] = "20261025091656"; }
    SECTION("a short sha256") { payload["sha256"] = "e3b0c442"; }
    SECTION("a sha256 that is not hex") { payload["sha256"] = std::string(64, 'z'); }
    SECTION("an empty image") { payload["size"] = 0u; }
    SECTION("a negative size, as parsed off the wire") { payload["size"] = json::parse("-1"); }
    SECTION("a fractional size, as parsed off the wire") { payload["size"] = json::parse("1.5"); }
    SECTION("a deadline that is not ISO-8601 UTC") { payload["deadline"] = "tomorrow"; }
    SECTION("a missing deadline") { payload.erase("deadline"); }
    SECTION("a deadline on a day that does not exist")
    {
        payload["deadline"] = "2026-02-30T00:00:00Z";
    }

    net.handle(payload);

    CHECK(net.requests.empty());
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].body
          == json {{"status", "failed"}, {"stage", "idle_check"}, {"failure", "invalid_policy"}});

    net.handle(flashPayload(RUN_B));
    CHECK(net.requests.size() == 1);
}

TEST_CASE("probe_flash without a UUID run_id, or seen before, is dropped")
{
    FlashNet net(flashesCpuAndDmd());

    SECTION("not a UUID")
    {
        net.handle(flashPayload("run-1"));
        net.handle(flashPayload("3f2b8c1e8d4a4a579a3e2c6f1d0b7e55")); // no dashes
        auto numeric = flashPayload(RUN_A);
        numeric["run_id"] = 42;
        net.handle(numeric);
        net.handle(json::array({flashPayload(RUN_A)})); // not an object at all
        CHECK(net.requests.empty());
        CHECK(net.posts.empty());
    }
    SECTION("the same run delivered twice")
    {
        net.handle(flashPayload(RUN_A));
        net.submitProbeFlashResult(RUN_A, R"({"status":"succeeded"})");
        net.handle(flashPayload(RUN_A));
        CHECK(net.requests.size() == 1);
        CHECK(net.posts.size() == 1); // the submitted result, never a refusal
    }
}

TEST_CASE("The Scorbitron object advertises the probes flashed here")
{
    SECTION("the host's probes")
    {
        FlashNet net(flashesCpuAndDmd());
        const auto object = NetTestAccess::scorbitronObject(net);
        CHECK(object.at("probe_flash_capable") == true);
        CHECK(object.at("probe_flash_probes") == json {"cpu", "dmd"});
    }
    SECTION("nothing set up")
    {
        FlashNet net;
        const auto object = NetTestAccess::scorbitronObject(net);
        CHECK(object.at("probe_flash_capable") == false);
        CHECK(object.at("probe_flash_probes") == json::array());
    }
    SECTION("the NFC trigger alone advertises nothing")
    {
        DeviceInfo info;
        info.probeFlashNfcTrigger = true;
        FlashNet net(info);
        CHECK(NetTestAccess::scorbitronObject(net).at("probe_flash_capable") == false);
    }
}

TEST_CASE("An nfc run goes to the SDK's own flash once this process can flash the pad")
{
    FlashNet net(flashesCpuAndDmd());
    NetTestAccess::setNfcFlashable(net, true);

    net.handle(flashPayload(RUN_A, "nfc"));

    CHECK(net.requests.empty()); // never handed to the host
    CHECK(net.posts.empty());
    REQUIRE(net.nfcRuns.size() == 1);
    const auto &run = net.nfcRuns[0];
    CHECK(run.at("run_id") == RUN_A);
    CHECK(run.at("probe") == "nfc");
    CHECK(run.at("target_build") == 20261025091656u);
    CHECK(run.at("sha256") == SHA);
    CHECK(run.at("size") == 74240u);
    CHECK(run.at("image_url")
                  .get<std::string>()
                  .ends_with(std::string {"/internal/api/diagnostics/probe-flash/"} + RUN_A
                             + "/image/"));

    // It holds the unit like any other flash.
    net.handle(flashPayload(RUN_B, "cpu"));
    CHECK(net.requests.empty());
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].body == json {{"status", "refused"}, {"reason", "already_running"}});
}

TEST_CASE("The NFC trigger does not decide whether the SDK flashes the pad")
{
    DeviceInfo info;
    info.probeFlashNfcTrigger = false;
    FlashNet net(info);
    NetTestAccess::setNfcFlashable(net, true);

    net.handle(flashPayload(RUN_A, "nfc"));
    CHECK(net.nfcRuns.size() == 1);
}

TEST_CASE("The NFC flash waits until the unit has been idle for a while")
{
    FlashNet net;
    CHECK_FALSE(NetTestAccess::nfcFlashIdle(net)); // just started
}

TEST_CASE("A probe flash stage is posted once, stamped when it has no time")
{
    FlashNet net;
    std::vector<int> statuses;
    const auto record = [&statuses](Error, int status, const std::string &) {
        statuses.push_back(status);
    };

    SECTION("stamped")
    {
        net.submitProbeFlashStage(RUN_A, R"({"stage":"copy"})", record);
        REQUIRE(net.posts.size() == 1);
        CHECK(net.posts[0].path == STAGE_PATH);
        CHECK(net.posts[0].body.at("stage") == "copy");
        CHECK(net.posts[0].body.at("at").get<std::string>().ends_with("Z"));

        net.answer(Error::ApiError, 503); // best effort: no resend
        CHECK(statuses == std::vector {503});
        CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
    }
    SECTION("its own time kept")
    {
        net.submitProbeFlashStage(RUN_A, R"({"stage":"copy","at":"2026-10-25T12:00:00Z"})");
        REQUIRE(net.posts.size() == 1);
        CHECK(net.posts[0].body.at("at") == "2026-10-25T12:00:00Z");
    }
    SECTION("invalid input sends nothing")
    {
        net.submitProbeFlashStage("run-1", R"({"stage":"copy"})", record);
        net.submitProbeFlashStage(RUN_A, "not json", record);
        CHECK(net.posts.empty());
        CHECK(statuses == std::vector {0, 0});
    }
}

TEST_CASE("A probe flash result is resent with backoff until the API answers finally")
{
    FlashNet net;
    std::vector<int> statuses;
    net.submitProbeFlashResult(RUN_A, R"({"status":"failed","failure":"checksum"})",
                               [&statuses](Error, int status, const std::string &) {
                                   statuses.push_back(status);
                               });
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].path == RESULT_PATH);
    CHECK(net.posts[0].body == json {{"status", "failed"}, {"failure", "checksum"}});

    net.answer(Error::ApiError, 503);
    CHECK(statuses.empty());
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 1);

    NetTestAccess::resendDueProbeFlashResults(net, std::chrono::steady_clock::now()); // too soon
    CHECK(net.posts.size() == 1);

    const auto later = std::chrono::steady_clock::now() + 1h;
    NetTestAccess::resendDueProbeFlashResults(net, later);
    REQUIRE(net.posts.size() == 2);
    CHECK(net.posts[1].body == net.posts[0].body);

    SECTION("no response at all is resent too")
    {
        net.answer(Error::ApiError, 0);
        NetTestAccess::resendDueProbeFlashResults(net, later + 1h);
        CHECK(net.posts.size() == 3);
        CHECK(statuses.empty());
    }
    SECTION("a 401 whose re-authentication left pairing being re-checked is resent")
    {
        net.answer(Error::NotPaired, 401);
        CHECK(statuses.empty());
        CHECK(NetTestAccess::pendingProbeFlashResults(net) == 1);
    }
    SECTION("409, a different result recorded, is final")
    {
        net.answer(Error::ApiError, 409);
        CHECK(statuses == std::vector {409});
        CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
    }
    SECTION("accepted is final")
    {
        net.answer(Error::Success, 202);
        CHECK(statuses == std::vector {202});
        CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
    }
}

TEST_CASE("The retry timer stays armed while any result is still waiting")
{
    FlashNet net;
    net.submitProbeFlashResult(RUN_A, R"({"status":"succeeded"})");
    net.answer(Error::ApiError, 503); // RUN_A waits 5 s
    const auto firstRetry = NetTestAccess::probeFlashRetryAt(net);
    REQUIRE(firstRetry);

    net.submitProbeFlashResult(RUN_B, R"({"status":"succeeded"})");
    net.answer(Error::ApiError, 503); // RUN_B waits 5 s too, a little later
    REQUIRE(NetTestAccess::pendingProbeFlashResults(net) == 2);

    // Only RUN_A is due; once it is accepted RUN_B must still have a timer
    NetTestAccess::resendDueProbeFlashResults(net, *firstRetry);
    REQUIRE(net.posts.size() == 3);
    CHECK(net.posts[2].runId == RUN_A);
    CHECK(NetTestAccess::probeFlashRetryAt(net));
    net.answer(Error::Success, 202);
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 1);
    CHECK(NetTestAccess::probeFlashRetryAt(net));
}

TEST_CASE("A probe flash result is not retried while the SDK is stopping")
{
    FlashNet net;
    std::vector<int> statuses;
    net.submitProbeFlashResult(RUN_A, R"({"status":"succeeded"})",
                               [&statuses](Error, int status, const std::string &) {
                                   statuses.push_back(status);
                               });
    NetTestAccess::setStopping(net);
    net.answer(Error::ApiError, 503);

    CHECK(statuses == std::vector {503});
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
    CHECK_FALSE(NetTestAccess::probeFlashRetryAt(net));
}

TEST_CASE("A probe flash result still waiting at shutdown gets a final answer")
{
    std::vector<int> statuses;
    {
        FlashNet net;
        net.submitProbeFlashResult(RUN_A, R"({"status":"succeeded"})",
                                   [&statuses](Error, int status, const std::string &) {
                                       statuses.push_back(status);
                                   });
        net.answer(Error::ApiError, 503); // now waiting for its retry
        REQUIRE(NetTestAccess::pendingProbeFlashResults(net) == 1);
        CHECK(statuses.empty());
    } // destroyed with the result still waiting
    CHECK(statuses == std::vector {0});
}

TEST_CASE("A probe flash result gives up after its last attempt")
{
    FlashNet net;
    std::vector<int> statuses;
    net.submitProbeFlashResult(RUN_A, R"({"status":"succeeded"})",
                               [&statuses](Error, int status, const std::string &) {
                                   statuses.push_back(status);
                               });
    auto later = std::chrono::steady_clock::now();
    for (int attempt = 1; attempt < 30; ++attempt) {
        net.answer(Error::ApiError, 503);
        later += 1h;
        NetTestAccess::resendDueProbeFlashResults(net, later);
    }
    CHECK(net.posts.size() == 30);
    CHECK(statuses.empty());

    net.answer(Error::ApiError, 503); // the 30th attempt
    CHECK(statuses == std::vector {503});
    CHECK(NetTestAccess::pendingProbeFlashResults(net) == 0);
}

TEST_CASE("The probe flash event's C helper")
{
    ProbeFlashRequestedEvent event {RUN_A, R"({"probe":"cpu"})"};
    CHECK(sb_event_type(&event) == SB_EVT_PROBE_FLASH_REQUESTED);
    const char *runId = nullptr;
    const char *request = nullptr;

    REQUIRE(sb_event_probe_flash_requested(&event, &runId, &request));
    CHECK(std::string {runId} == RUN_A);
    CHECK(std::string {request} == R"({"probe":"cpu"})");

    CHECK_FALSE(sb_event_probe_flash_requested(nullptr, &runId, &request));
    CHECK_FALSE(sb_event_probe_flash_requested(&event, nullptr, &request));
    CHECK_FALSE(sb_event_probe_flash_requested(&event, &runId, nullptr));

    HardwareProbeRequestedEvent other {RUN_A, false};
    CHECK_FALSE(sb_event_probe_flash_requested(&other, &runId, &request));
}

TEST_CASE("The host can declare only the probes it can flash")
{
    const auto config = sb_config_create();
    REQUIRE(config);
    sb_config_add_probe_flash_probe(config, "cpu");
    sb_config_add_probe_flash_probe(config, "link");
    sb_config_add_probe_flash_probe(config, "nfc"); // the SDK's, through the trigger instead
    sb_config_add_probe_flash_probe(config, "spike");
    sb_config_add_probe_flash_probe(config, nullptr);
    sb_config_add_probe_flash_probe(nullptr, "dmd");
    sb_config_set_probe_flash_nfc_trigger(config, true);

    CHECK(config->probeFlashProbes == std::set<std::string> {"cpu", "link"});
    CHECK(config->probeFlashNfcTrigger);
    sb_config_destroy(config);
}

TEST_CASE("A probe flash result for a run the SDK never saw is still sent")
{
    FlashNet net;
    net.submitProbeFlashResult(RUN_C, R"({"status":"failed","failure":"interrupted"})");
    REQUIRE(net.posts.size() == 1);
    CHECK(net.posts[0].runId == RUN_C);
}

TEST_CASE("probe_flash run ids, retry rules and backoff")
{
    CHECK(isUuid(RUN_A));
    CHECK(isUuid("3F2B8C1E-8D4A-4A57-9A3E-2C6F1D0B7E55"));
    CHECK_FALSE(isUuid(""));
    CHECK_FALSE(isUuid("3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e5"));
    CHECK_FALSE(isUuid("3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e5g"));
    CHECK_FALSE(isUuid("3f2b8c1e-8d4a-4a57-9a3e/2c6f1d0b7e55"));

    CHECK(isProbeFlashResultRetryable(Error::ApiError, 0));
    CHECK(isProbeFlashResultRetryable(Error::ApiError, 429));
    CHECK(isProbeFlashResultRetryable(Error::ApiError, 503));
    CHECK_FALSE(isProbeFlashResultRetryable(Error::Success, 202));
    CHECK_FALSE(isProbeFlashResultRetryable(Error::ApiError, 400));
    CHECK_FALSE(isProbeFlashResultRetryable(Error::ApiError, 401));
    CHECK_FALSE(isProbeFlashResultRetryable(Error::ApiError, 404));
    CHECK_FALSE(isProbeFlashResultRetryable(Error::ApiError, 409));
    // Stopped by the auth gate, not answered: the stale 401 from before re-authentication
    CHECK(isProbeFlashResultRetryable(Error::NotPaired, 401));
    CHECK(isProbeFlashResultRetryable(Error::AuthFailed, 0));

    CHECK(probeFlashResultBackoff(1) == 5s);
    CHECK(probeFlashResultBackoff(2) == 10s);
    CHECK(probeFlashResultBackoff(6) == 160s);
    CHECK(probeFlashResultBackoff(7) == 5min);
    CHECK(probeFlashResultBackoff(1000) == 5min);

    using std::chrono::sys_days;
    using namespace std::chrono;
    CHECK(parseProbeFlashDeadline("2026-10-25T18:30:15Z")
          == sys_days {2026y / October / 25} + 18h + 30min + 15s);
    CHECK(parseProbeFlashDeadline("2026-10-25T18:30:15.250+00:00")
          == sys_days {2026y / October / 25} + 18h + 30min + 15s);
    CHECK_FALSE(parseProbeFlashDeadline("2026-02-30T00:00:00Z"));
    CHECK_FALSE(parseProbeFlashDeadline("9999-12-31T23:59:59Z")); // would overflow
    CHECK_FALSE(parseProbeFlashDeadline("1970-01-01T00:00:00Z"));
    CHECK(parseProbeFlashDeadline("2100-12-31T23:59:59Z"));
    CHECK_FALSE(parseProbeFlashDeadline("2026-10-25"));
}
