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

#include <scorbit_sdk/game_state_c.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

// clazy:excludeall=non-pod-global-static

using json = nlohmann::json;

namespace {

int dummySigner(uint8_t signature[SB_SIGNATURE_MAX_LENGTH], size_t *signature_len,
                const uint8_t digest[SB_DIGEST_LENGTH], void *user_data)
{
    (void)signature;
    (void)signature_len;
    (void)digest;
    (void)user_data;
    return 0;
}

// Valid P-256 test private key (32 bytes = 64 hex chars)
constexpr auto TEST_PRIVATE_KEY_HEX =
        "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2";
constexpr auto TEST_UUID = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab";
constexpr uint64_t TEST_SERIAL = 12345;

std::string g_savedKey;

void testSaveKeyCallback(const char *key, void * /*user_data*/)
{
    g_savedKey = key;
}

int testLoadKeyCallback(char *buffer, size_t buffer_size, void * /*user_data*/)
{
    json j;
    j["private_key"] = TEST_PRIVATE_KEY_HEX;
    j["uuid"] = TEST_UUID;
    j["serial_number"] = TEST_SERIAL;
    std::string data = j.dump();

    if (data.size() >= buffer_size) {
        return -1;
    }
    std::copy(data.begin(), data.end(), buffer);
    buffer[data.size()] = '\0';
    return static_cast<int>(data.size());
}

int testEmptyLoadKeyCallback(char * /*buffer*/, size_t /*buffer_size*/, void * /*user_data*/)
{
    return 0;
}

} // namespace

TEST_CASE("Create and destroy game state with signer callback", "[GameState]")
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "vscorbitron");
    sb_config_set_machine_id(cfg, 4419);
    sb_config_set_game_code_version(cfg, "0.1.0");
    sb_config_set_signer(cfg, dummySigner, nullptr);
    sb_config_set_serial_number(cfg, 9876543210ULL);

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    CHECK(sb_get_machine_serial(h) == 9876543210ULL);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("Create game state with software key from callback", "[GameState][SoftKey]")
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "testprovider");
    sb_config_set_machine_id(cfg, 1234);
    sb_config_set_game_code_version(cfg, "1.0.0");
    sb_config_set_hostname(cfg, "staging");
    sb_config_set_encrypted_key(cfg, "dummy_encrypted_key");
    sb_config_set_save_key_callback(cfg, testSaveKeyCallback, nullptr);
    sb_config_set_load_key_callback(cfg, testLoadKeyCallback, nullptr);

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("Create game state returns nullptr with no auth", "[GameState]")
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "testprovider");
    sb_config_set_machine_id(cfg, 1234);
    sb_config_set_game_code_version(cfg, "1.0.0");

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h == nullptr);

    sb_config_destroy(cfg);
}

TEST_CASE("Create game state returns nullptr with null config", "[GameState]")
{
    sb_game_handle_t h = sb_create_game_state(nullptr);
    REQUIRE(h == nullptr);
}

TEST_CASE("Create game state with soft key when load callback returns empty (first run)",
          "[GameState][SoftKey]")
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "testprovider");
    sb_config_set_machine_id(cfg, 1234);
    sb_config_set_game_code_version(cfg, "1.0.0");
    sb_config_set_hostname(cfg, "staging");
    sb_config_set_encrypted_key(cfg, "dummy_encrypted_key");
    sb_config_set_save_key_callback(cfg, testSaveKeyCallback, nullptr);
    sb_config_set_load_key_callback(cfg, testEmptyLoadKeyCallback, nullptr);

    // No saved key yet: provisioning is attempted asynchronously after create; handle is still
    // valid.
    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

// SB-4795: the typed config update and the diagnostics request-generation echo. These cases run
// against the built shared library, so they also prove the new entry points are actually exported
// and that the C ABI's optional arguments are null-safe.

namespace {

sb_config_t makeSignerConfig()
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "vscorbitron");
    sb_config_set_machine_id(cfg, 4419);
    sb_config_set_game_code_version(cfg, "0.1.0");
    sb_config_set_signer(cfg, dummySigner, nullptr);
    sb_config_set_serial_number(cfg, 9876543210ULL);
    return cfg;
}

} // namespace

TEST_CASE("sb_report_device_state accepts an absent log and an absent callback",
          "[GameState][Config]")
{
    sb_config_t cfg = makeSignerConfig();
    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    // A NULL log omits the field; a NULL callback means the caller wants no result. Neither may
    // crash, and an unknown type must be accepted rather than rejected locally.
    sb_report_device_state(h, "capture_retained", "2026-09-15T00:00:00Z", true, nullptr, nullptr,
                           nullptr);

    // A blank version is the documented way to withdraw a previous report.
    sb_report_device_state(h, "capture_retained", "", true, nullptr, nullptr, nullptr);

    // With a log attached.
    sb_report_device_state(h, "sdk", "1.2.3", false, "some log text", nullptr, nullptr);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("sb_submit_hardware_probe_result answers invalid JSON without sending",
          "[GameState][Diagnostics]")
{
    sb_config_t cfg = makeSignerConfig();
    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    struct Seen {
        std::atomic<int> calls {0};
        std::atomic<int> error {-1};
        std::atomic<int> status {-1};
    } seen;
    auto callback = [](sb_error_t error, int httpStatus, const char *, void *userData) {
        auto *s = static_cast<Seen *>(userData);
        s->error = error;
        s->status = httpStatus;
        ++s->calls;
    };

    sb_submit_hardware_probe_result(h, "3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e55", "not json", callback,
                                    &seen);
    sb_submit_hardware_probe_result(h, "", "{}", nullptr, nullptr); // NULL callback is allowed

    for (int i = 0; i < 200 && seen.calls == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(seen.calls == 1);
    CHECK(seen.error == SB_EC_UNKNOWN);
    CHECK(seen.status == 0);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("sb_upload_diagnostics_ex accepts an absent request generation", "[GameState][Config]")
{
    sb_config_t cfg = makeSignerConfig();
    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    // NULL means "this upload answers no particular request" and must be accepted as such.
    sb_upload_diagnostics_ex(h, nullptr, 0, nullptr, 0, "", nullptr);

    const uint64_t generation = 7;
    sb_upload_diagnostics_ex(h, nullptr, 0, nullptr, 0, "", &generation);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("The table mappings start empty, never fetched", "[GameState][Vpin]")
{
    sb_config_t cfg = makeSignerConfig();
    sb_config_set_vpin_table_mappings(cfg, true);
    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    sb_table_mappings_t m = sb_get_table_mappings(h);
    REQUIRE(m != nullptr);
    CHECK(sb_table_mappings_result(m) == SB_TABLE_MAPPINGS_NONE);
    CHECK(sb_table_mappings_count(m) == 0);
    CHECK(std::string {sb_table_mappings_etag(m)}.empty());
    CHECK(std::string {sb_table_mapping_venuemachine(m, 0)}.empty());
    sb_table_mappings_free(m);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("sb_switch_machine before authentication is refused through the callback",
          "[GameState][Vpin]")
{
    sb_config_t cfg = makeSignerConfig();
    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    struct Seen {
        std::atomic<int> calls {0};
        std::atomic<int> result {-1};
        std::string target;
    } seen;
    sb_switch_machine(
            h, "6a0d1f7e-2b9c-4e8a-9d3f-1c2b3a4d5e6f",
            [](sb_switch_machine_result_t result, const char *venuemachine, void *userData) {
                auto *s = static_cast<Seen *>(userData);
                s->target = venuemachine ? venuemachine : "";
                s->result = result;
                ++s->calls;
            },
            &seen);

    for (int i = 0; i < 200 && seen.calls == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(seen.calls == 1);
    CHECK(seen.result == SB_SWITCH_MACHINE_NOT_AUTHENTICATED);
    CHECK(seen.target == "6a0d1f7e-2b9c-4e8a-9d3f-1c2b3a4d5e6f");

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("The table-match request and answer accept NULL", "[GameState][Vpin]")
{
    sb_table_match_request_free(nullptr);
    sb_table_match_request_set_key(nullptr, "rom", "mm_109c");
    sb_table_match_request_set_firmware_id(nullptr, "fw");
    sb_table_match_request_set_signatures_json(nullptr, "[]");
    sb_table_match_request_set_signature_unavailable(nullptr, "table_unreadable");

    sb_table_match_request_t r = sb_table_match_request_create();
    REQUIRE(r != nullptr);
    sb_table_match_request_set_key(r, nullptr, nullptr);
    sb_table_match_request_set_firmware_id(r, nullptr);
    sb_table_match_request_set_signatures_json(r, nullptr);
    sb_table_match_request_set_signature_unavailable(r, nullptr);
    sb_table_match_request_free(r);

    CHECK(sb_table_match_http_status(nullptr) == 0);
    CHECK(std::string {sb_table_match_error_code(nullptr)}.empty());
    CHECK(std::string {sb_table_match_error_attr(nullptr)}.empty());
    CHECK(std::string {sb_table_match_key_type(nullptr)}.empty());
    CHECK(std::string {sb_table_match_table_key(nullptr)}.empty());
    CHECK(std::string {sb_table_match_mapping_id(nullptr)}.empty());
    CHECK(std::string {sb_table_match_venuemachine(nullptr)}.empty());
    CHECK(std::string {sb_table_match_venuemachine_name(nullptr)}.empty());
    CHECK(std::string {sb_table_match_machine(nullptr)}.empty());
    CHECK(std::string {sb_table_match_machine_name(nullptr)}.empty());
    CHECK_FALSE(sb_table_match_has_verdict(nullptr));
    CHECK(std::string {sb_table_match_verdict_status(nullptr)}.empty());
    CHECK(std::string {sb_table_match_verdict_accepted_version(nullptr)}.empty());
    CHECK(sb_table_match_verdict_changed_sections_count(nullptr) == 0);
    CHECK(std::string {sb_table_match_verdict_changed_section(nullptr, 0)}.empty());
    CHECK(sb_table_match_verdict_changed_procedures_count(nullptr) == 0);
    CHECK(std::string {sb_table_match_verdict_changed_procedure(nullptr, 0)}.empty());
}

TEST_CASE("sb_match_table_mapping refuses a bad request through the callback, from a copy",
          "[GameState][Vpin]")
{
    sb_config_t cfg = makeSignerConfig();
    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    struct Seen {
        std::atomic<int> calls {0};
        std::atomic<int> result {-1};
        std::atomic<int> httpStatus {-1};
        std::string errorCode {"unset"};
        std::string venueMachine {"unset"};
        bool hasVerdict {true};
        std::thread::id thread;
    };

    auto wait = [](Seen &seen) {
        for (int i = 0; i < 200 && seen.calls == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    const auto callback = [](sb_table_match_result_t result, sb_table_match_t match,
                             void *userData) {
        auto *s = static_cast<Seen *>(userData);
        s->httpStatus = sb_table_match_http_status(match);
        s->errorCode = sb_table_match_error_code(match);
        s->venueMachine = sb_table_match_venuemachine(match);
        s->hasVerdict = sb_table_match_has_verdict(match);
        s->thread = std::this_thread::get_id();
        s->result = result;
        ++s->calls;
    };

    SECTION("both kinds of evidence, the request freed before the answer")
    {
        Seen seen;
        sb_table_match_request_t r = sb_table_match_request_create();
        sb_table_match_request_set_key(r, "script_id", "6a0d1f7e-2b9c-4e8a-9d3f-1c2b3a4d5e6f");
        sb_table_match_request_set_signatures_json(r, R"([{"version":1}])");
        sb_table_match_request_set_signature_unavailable(r, "table_unreadable");
        sb_match_table_mapping(h, r, callback, &seen);
        sb_table_match_request_free(r);

        wait(seen);
        REQUIRE(seen.calls == 1);
        CHECK(seen.result == SB_TABLE_MATCH_INVALID);
        CHECK(seen.httpStatus == 0);
        CHECK(seen.errorCode.empty());
        CHECK(seen.venueMachine.empty());
        CHECK_FALSE(seen.hasVerdict);
        CHECK(seen.thread != std::this_thread::get_id());
    }

    SECTION("no request at all")
    {
        Seen seen;
        sb_match_table_mapping(h, nullptr, callback, &seen);
        wait(seen);
        REQUIRE(seen.calls == 1);
        CHECK(seen.result == SB_TABLE_MATCH_INVALID);
    }

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}
