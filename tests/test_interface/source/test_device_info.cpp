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

#include <scorbit_sdk/config.h>
#include <scorbit_sdk/config_c.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

using namespace scorbit;

// =================================================================================================
// C API Tests (sb_config_t)
// =================================================================================================

TEST_CASE("sb_config_t creation and destruction", "[Config][C]")
{
    SECTION("Create and destroy config")
    {
        sb_config_t config = sb_config_create();
        REQUIRE(config != nullptr);
        sb_config_destroy(config);
    }

    SECTION("Destroy null config is safe")
    {
        sb_config_destroy(nullptr); // Should not crash
    }
}

TEST_CASE("sb_config_t setters", "[Config][C]")
{
    sb_config_t config = sb_config_create();
    REQUIRE(config != nullptr);

    SECTION("Set provider")
    {
        sb_config_set_provider(config, "scorbitron");
        // No getter in C API, but setter should not crash
    }

    SECTION("Set machine_id")
    {
        sb_config_set_machine_id(config, 4419);
    }

    SECTION("Set game_code_version")
    {
        sb_config_set_game_code_version(config, "1.12.3");
    }

    SECTION("Set hostname")
    {
        sb_config_set_hostname(config, "staging");
    }

    SECTION("Set hostname to null")
    {
        sb_config_set_hostname(config, nullptr);
    }

    SECTION("Set uuid")
    {
        sb_config_set_uuid(config, "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce");
    }

    SECTION("Set uuid to null")
    {
        sb_config_set_uuid(config, nullptr);
    }

    SECTION("Set serial_number")
    {
        sb_config_set_serial_number(config, 123456789);
    }

    SECTION("Set auto_download_player_pics")
    {
        sb_config_set_auto_download_player_pics(config, true);
        sb_config_set_auto_download_player_pics(config, false);
    }

    SECTION("Set threads_priority")
    {
        sb_config_set_threads_priority(config, 0);
        sb_config_set_threads_priority(config, 10);
    }

    SECTION("Set score_features")
    {
        const char *features[] = {"ramp", "spinner", "target"};
        sb_config_set_score_features(config, features, 3, 1);
    }

    SECTION("Set score_features with null")
    {
        sb_config_set_score_features(config, nullptr, 0, 0);
    }

    SECTION("Set encrypted_key")
    {
        sb_config_set_encrypted_key(config, "encrypted_key_data");
    }

    sb_config_destroy(config);
}

TEST_CASE("sb_config_t null safety", "[Config][C]")
{
    // All setters should be safe to call with null config
    sb_config_set_provider(nullptr, "test");
    sb_config_set_machine_id(nullptr, 1);
    sb_config_set_game_code_version(nullptr, "1.0.0");
    sb_config_set_hostname(nullptr, "staging");
    sb_config_set_uuid(nullptr, "uuid");
    sb_config_set_serial_number(nullptr, 123);
    sb_config_set_auto_download_player_pics(nullptr, true);
    sb_config_set_threads_priority(nullptr, 10);
    sb_config_set_score_features(nullptr, nullptr, 0, 0);
    sb_config_set_encrypted_key(nullptr, "key");
}

// =================================================================================================
// C++ API Tests (scorbit::Config)
// =================================================================================================

TEST_CASE("Config default constructor", "[Config][C++]")
{
    Config config;
    REQUIRE(config.isValid());
}

TEST_CASE("Config method chaining", "[Config][C++]")
{
    Config config;

    // All setters should return reference and allow chaining
    config.setProvider("scorbitron")
            .setMachineId(4419)
            .setGameCodeVersion("1.12.3")
            .setHostname("staging")
            .setUuid("f0b188f8-9f2d-4f8d-abe4-c3107516e7ce")
            .setSerialNumber(123456789)
            .setAutoDownloadPlayerPics(true)
            .setThreadsPriority(10)
            .setScoreFeatures({"ramp", "spinner", "target"}, 1)
            .setEncryptedKey("encrypted_key_data");

    REQUIRE(config.isValid());
}

TEST_CASE("Config setters", "[Config][C++]")
{
    Config config;

    SECTION("Set provider")
    {
        config.setProvider("scorbitron");
        REQUIRE(config.isValid());
    }

    SECTION("Set machine_id")
    {
        config.setMachineId(4419);
        REQUIRE(config.isValid());
    }

    SECTION("Set game_code_version")
    {
        config.setGameCodeVersion("1.12.3");
        REQUIRE(config.isValid());
    }

    SECTION("Set hostname variants")
    {
        std::vector<std::string> valid_hostnames = {
                "production", "staging", "https://api.scorbit.io", "https://staging.scorbit.io",
                "http://localhost:8080"};

        for (const auto &hostname : valid_hostnames) {
            config.setHostname(hostname);
            REQUIRE(config.isValid());
        }
    }

    SECTION("Set uuid variants")
    {
        std::vector<std::string> valid_uuids = {
                "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce", "f0b188f89f2d4f8dabe4c3107516e7ce",
                "F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE", "F0B188F89F2D4F8DABE4C3107516E7CE"};

        for (const auto &uuid : valid_uuids) {
            config.setUuid(uuid);
            REQUIRE(config.isValid());
        }
    }

    SECTION("Set serial_number with max value")
    {
        config.setSerialNumber(std::numeric_limits<uint64_t>::max());
        REQUIRE(config.isValid());
    }

    SECTION("Set auto_download_player_pics")
    {
        config.setAutoDownloadPlayerPics(true);
        config.setAutoDownloadPlayerPics(false);
        REQUIRE(config.isValid());
    }

    SECTION("Set threads_priority")
    {
        config.setThreadsPriority(0);
        config.setThreadsPriority(10);
        REQUIRE(config.isValid());
    }

    SECTION("Set score_features")
    {
        config.setScoreFeatures({"ramp", "spinner", "target"}, 1);
        REQUIRE(config.isValid());
    }

    SECTION("Set score_features empty")
    {
        config.setScoreFeatures({}, 0);
        REQUIRE(config.isValid());
    }

    SECTION("Set score_features large count")
    {
        std::vector<std::string> features;
        for (int i = 0; i < 100; ++i) {
            features.push_back("feature" + std::to_string(i));
        }
        config.setScoreFeatures(features, 1);
        REQUIRE(config.isValid());
    }

    SECTION("Set encrypted_key")
    {
        config.setEncryptedKey("encrypted_key_data");
        REQUIRE(config.isValid());
    }
}

TEST_CASE("Config move semantics", "[Config][C++]")
{
    Config config1;
    config1.setProvider("test").setMachineId(1);

    Config config2 = std::move(config1);
    REQUIRE(config2.isValid());
}

TEST_CASE("Config handle access", "[Config][C++]")
{
    Config config;
    REQUIRE(config.handle() != nullptr);
}

TEST_CASE("Config full initialization", "[Config][C++]")
{
    SECTION("Full configuration with all fields")
    {
        Config config;
        config.setProvider("scorbitron")
                .setMachineId(4419)
                .setGameCodeVersion("1.12.3")
                .setHostname("production")
                .setUuid("f0b188f8-9f2d-4f8d-abe4-c3107516e7ce")
                .setSerialNumber(123456789)
                .setAutoDownloadPlayerPics(true)
                .setScoreFeatures({"ramp", "left spinner", "right spinner"}, 1)
                .setEncryptedKey("encrypted_key_data");

        REQUIRE(config.isValid());
        REQUIRE(config.handle() != nullptr);
    }

    SECTION("Minimal configuration with required fields only")
    {
        Config config;
        config.setProvider("vpin").setMachineId(1234).setGameCodeVersion("2.0.0").setEncryptedKey(
                "key");

        REQUIRE(config.isValid());
    }
}
