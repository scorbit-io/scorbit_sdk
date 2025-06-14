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

#include <scorbit_sdk/net_types.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

using namespace scorbit;

TEST_CASE("DeviceInfo default constructor", "[DeviceInfo]")
{
    DeviceInfo info;

    REQUIRE(info.provider.empty());
    REQUIRE(info.machineId == 0);
    REQUIRE(info.gameCodeVersion.empty());
    REQUIRE(info.hostname.empty());
    REQUIRE(info.uuid.empty());
    REQUIRE(info.serialNumber == 0);
    REQUIRE(info.autoDownloadPlayerPics == false);
    REQUIRE(info.scoreFeatures.empty());
}

TEST_CASE("DeviceInfo constructor from sb_device_info_t", "[DeviceInfo]")
{
    SECTION("Full initialization with all fields")
    {
        const char *tags[] = {"tag1", "tag2", "tag3"};
        sb_device_info_t c_info;
        c_info.provider = "scorbitron";
        c_info.machine_id = 4419;
        c_info.game_code_version = "1.12.3";
        c_info.hostname = "production";
        c_info.uuid = "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce";
        c_info.serial_number = 123456789;
        c_info.auto_download_player_pics = true;
        c_info.score_features = tags;
        c_info.score_features_count = 3;
        c_info.score_features_version = 1;

        DeviceInfo info {c_info};

        REQUIRE(info.provider == "scorbitron");
        REQUIRE(info.machineId == 4419);
        REQUIRE(info.gameCodeVersion == "1.12.3");
        REQUIRE(info.hostname == "production");
        REQUIRE(info.uuid == "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce");
        REQUIRE(info.serialNumber == 123456789);
        REQUIRE(info.autoDownloadPlayerPics == true);
        REQUIRE(info.scoreFeatures.size() == 3);
        REQUIRE(info.scoreFeatures[0] == "tag1");
        REQUIRE(info.scoreFeatures[1] == "tag2");
        REQUIRE(info.scoreFeatures[2] == "tag3");
        REQUIRE(info.scoreFeaturesVersion == 1);
    }

    SECTION("Minimal initialization with required fields only")
    {
        sb_device_info_t c_info;
        c_info.provider = "vpin";
        c_info.machine_id = 1234;
        c_info.game_code_version = "2.0.0";
        c_info.hostname = nullptr;
        c_info.uuid = nullptr;
        c_info.serial_number = 0;
        c_info.auto_download_player_pics = false;
        c_info.score_features = nullptr;
        c_info.score_features_count = 0;
        c_info.score_features_version = 0;

        DeviceInfo info {c_info};

        REQUIRE(info.provider == "vpin");
        REQUIRE(info.machineId == 1234);
        REQUIRE(info.gameCodeVersion == "2.0.0");
        REQUIRE(info.hostname.empty());
        REQUIRE(info.uuid.empty());
        REQUIRE(info.serialNumber == 0);
        REQUIRE(info.autoDownloadPlayerPics == false);
        REQUIRE(info.scoreFeatures.empty());
    }

    SECTION("With null score tags but non-zero count (edge case)")
    {
        sb_device_info_t c_info;
        c_info.provider = "test";
        c_info.machine_id = 1;
        c_info.game_code_version = "1.0.0";
        c_info.hostname = nullptr;
        c_info.uuid = nullptr;
        c_info.serial_number = 0;
        c_info.auto_download_player_pics = false;
        c_info.score_features = nullptr;
        c_info.score_features_count = 5; // Non-zero but null pointer

        DeviceInfo info {c_info};

        REQUIRE(info.scoreFeatures.empty()); // Should handle null pointer gracefully
    }

    SECTION("With some null strings in score tags")
    {
        const char *tags[] = {"tag1", nullptr, "tag3"};
        sb_device_info_t c_info;
        c_info.provider = "test";
        c_info.machine_id = 1;
        c_info.game_code_version = "1.0.0";
        c_info.hostname = nullptr;
        c_info.uuid = nullptr;
        c_info.serial_number = 0;
        c_info.auto_download_player_pics = false;
        c_info.score_features = tags;
        c_info.score_features_count = 3;
        c_info.score_features_version = 1;

        DeviceInfo info {c_info};

        REQUIRE(info.scoreFeatures.size() == 3);
        REQUIRE(info.scoreFeatures[0] == "tag1");
        REQUIRE(info.scoreFeatures[1].empty()); // nullptr should become empty string
        REQUIRE(info.scoreFeatures[2] == "tag3");
    }
}

TEST_CASE("DeviceInfo conversion to sb_device_info_t", "[DeviceInfo]")
{
    SECTION("Full conversion with all fields")
    {
        DeviceInfo info;
        info.provider = "scorbitron";
        info.machineId = 4419;
        info.gameCodeVersion = "1.12.3";
        info.hostname = "staging";
        info.uuid = "F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE";
        info.serialNumber = 987654321;
        info.autoDownloadPlayerPics = true;
        info.scoreFeatures = {"score", "bonus", "multiball"};
        info.scoreFeaturesVersion = 2;

        sb_device_info_t c_info = info;

        REQUIRE(std::string(c_info.provider) == "scorbitron");
        REQUIRE(c_info.machine_id == 4419);
        REQUIRE(std::string(c_info.game_code_version) == "1.12.3");
        REQUIRE(std::string(c_info.hostname) == "staging");
        REQUIRE(std::string(c_info.uuid) == "F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE");
        REQUIRE(c_info.serial_number == 987654321);
        REQUIRE(c_info.auto_download_player_pics == true);
        REQUIRE(c_info.score_features_count == 3);
        REQUIRE(c_info.score_features != nullptr);
        REQUIRE(std::string(c_info.score_features[0]) == "score");
        REQUIRE(std::string(c_info.score_features[1]) == "bonus");
        REQUIRE(std::string(c_info.score_features[2]) == "multiball");
        REQUIRE(c_info.score_features_version == 2);
    }

    SECTION("Conversion with empty score tags")
    {
        DeviceInfo info;
        info.provider = "vpin";
        info.machineId = 1234;
        info.gameCodeVersion = "2.0.0";
        // scoreTags remains empty

        sb_device_info_t c_info = info;

        REQUIRE(std::string(c_info.provider) == "vpin");
        REQUIRE(c_info.machine_id == 1234);
        REQUIRE(std::string(c_info.game_code_version) == "2.0.0");
        REQUIRE(c_info.score_features == nullptr);
        REQUIRE(c_info.score_features_count == 0);
    }

    SECTION("Conversion with empty strings in optional fields")
    {
        DeviceInfo info;
        info.provider = "test";
        info.machineId = 1;
        info.gameCodeVersion = "1.0.0";
        // hostname, uuid remain empty

        sb_device_info_t c_info = info;

        REQUIRE(std::string(c_info.hostname).empty());
        REQUIRE(std::string(c_info.uuid).empty());
    }
}

TEST_CASE("DeviceInfo round-trip conversion", "[DeviceInfo]")
{
    SECTION("Full round-trip conversion")
    {
        // Create original C struct
        const char *original_tags[] = {"tag1", "tag2"};
        sb_device_info_t original;
        original.provider = "scorbitron";
        original.machine_id = 4419;
        original.game_code_version = "1.12.3";
        original.hostname = "production";
        original.uuid = "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce";
        original.serial_number = 123456789;
        original.auto_download_player_pics = true;
        original.score_features = original_tags;
        original.score_features_count = 2;

        // Convert to DeviceInfo and back
        DeviceInfo info(original);
        sb_device_info_t converted = info;

        // Verify all fields match
        REQUIRE(std::string(converted.provider) == std::string(original.provider));
        REQUIRE(converted.machine_id == original.machine_id);
        REQUIRE(std::string(converted.game_code_version)
                == std::string(original.game_code_version));
        REQUIRE(std::string(converted.hostname) == std::string(original.hostname));
        REQUIRE(std::string(converted.uuid) == std::string(original.uuid));
        REQUIRE(converted.serial_number == original.serial_number);
        REQUIRE(converted.auto_download_player_pics == original.auto_download_player_pics);
        REQUIRE(converted.score_features_count == original.score_features_count);

        for (size_t i = 0; i < converted.score_features_count; ++i) {
            REQUIRE(std::string(converted.score_features[i])
                    == std::string(original.score_features[i]));
        }
    }
}

TEST_CASE("DeviceInfo edge cases and validation", "[DeviceInfo]")
{
    SECTION("UUID format variations")
    {
        DeviceInfo info;

        // Test different UUID formats mentioned in comments
        std::vector<std::string> valid_uuids = {
                "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce", "f0b188f89f2d4f8dabe4c3107516e7ce",
                "F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE", "F0B188F89F2D4F8DABE4C3107516E7CE"};

        for (const auto &uuid : valid_uuids) {
            info.uuid = uuid;
            sb_device_info_t c_info = info;
            REQUIRE(std::string(c_info.uuid) == uuid);
        }
    }

    SECTION("Hostname format variations")
    {
        DeviceInfo info;
        info.provider = "test";
        info.machineId = 1;
        info.gameCodeVersion = "1.0.0";

        std::vector<std::string> valid_hostnames = {
                "production", "staging", "https://api.scorbit.io", "https://staging.scorbit.io",
                "http://localhost:8080"};

        for (const auto &hostname : valid_hostnames) {
            info.hostname = hostname;
            sb_device_info_t c_info = info;
            REQUIRE(std::string(c_info.hostname) == hostname);
        }
    }

    SECTION("Large number of score tags")
    {
        DeviceInfo info;
        info.provider = "test";
        info.machineId = 1;
        info.gameCodeVersion = "1.0.0";

        // Create a large number of tags
        for (int i = 0; i < 100; ++i) {
            info.scoreFeatures.push_back("tag" + std::to_string(i));
        }

        sb_device_info_t c_info = info;
        REQUIRE(c_info.score_features_count == 100);
        REQUIRE(c_info.score_features != nullptr);

        for (int i = 0; i < 100; ++i) {
            REQUIRE(std::string(c_info.score_features[i]) == ("tag" + std::to_string(i)));
        }
    }

    SECTION("Maximum values for numeric fields")
    {
        DeviceInfo info;
        info.provider = "test";
        info.machineId = std::numeric_limits<int32_t>::max();
        info.gameCodeVersion = "1.0.0";
        info.serialNumber = std::numeric_limits<uint64_t>::max();

        sb_device_info_t c_info = info;
        REQUIRE(c_info.machine_id == std::numeric_limits<int32_t>::max());
        REQUIRE(c_info.serial_number == std::numeric_limits<uint64_t>::max());
    }

    SECTION("Empty string handling")
    {
        DeviceInfo info;
        info.provider.clear();
        info.machineId = 1;
        info.gameCodeVersion.clear();
        info.hostname.clear();
        info.uuid.clear();
        info.scoreFeatures = {"", "valid", ""};

        sb_device_info_t c_info = info;
        REQUIRE(std::string(c_info.provider).empty());
        REQUIRE(std::string(c_info.game_code_version).empty());
        REQUIRE(std::string(c_info.hostname).empty());
        REQUIRE(std::string(c_info.uuid).empty());
        REQUIRE(c_info.score_features_count == 3);
        REQUIRE(std::string(c_info.score_features[0]).empty());
        REQUIRE(std::string(c_info.score_features[1]) == "valid");
        REQUIRE(std::string(c_info.score_features[2]).empty());
    }
}

TEST_CASE("DeviceInfo memory safety", "[DeviceInfo]")
{
    SECTION("Conversion operator creates valid pointers")
    {
        DeviceInfo info;
        info.provider = "test";
        info.machineId = 1;
        info.gameCodeVersion = "1.0.0";
        info.scoreFeatures = {"tag1", "tag2"};

        sb_device_info_t c_info = info;

        // Verify that all C-string pointers are valid
        REQUIRE(c_info.provider != nullptr);
        REQUIRE(c_info.game_code_version != nullptr);
        REQUIRE(c_info.score_features != nullptr);
        REQUIRE(c_info.score_features[0] != nullptr);
        REQUIRE(c_info.score_features[1] != nullptr);

        // Verify content is accessible
        REQUIRE(strlen(c_info.provider) > 0);
        REQUIRE(strlen(c_info.game_code_version) > 0);
        REQUIRE(strlen(c_info.score_features[0]) > 0);
        REQUIRE(strlen(c_info.score_features[1]) > 0);
    }

    SECTION("Multiple conversions don't interfere")
    {
        DeviceInfo info;
        info.provider = "test";
        info.machineId = 1;
        info.gameCodeVersion = "1.0.0";
        info.scoreFeatures = {"tag1", "tag2"};

        sb_device_info_t c_info1 = info;
        sb_device_info_t c_info2 = info;

        // Both conversions should be valid and equal
        REQUIRE(std::string(c_info1.provider) == std::string(c_info2.provider));
        REQUIRE(c_info1.score_features_count == c_info2.score_features_count);
        REQUIRE(std::string(c_info1.score_features[0]) == std::string(c_info2.score_features[0]));
    }
}
