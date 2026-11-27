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

// The build identity on the Scorbitron object: commits and active map hashes, sent with boot_id.
//
// Every Net here is built from a DeviceInfo that does not validate, so its worker never starts.

#include "device_info.h"
#include "net.h"
#include "net_util.h"
#include <scorbit_sdk/config_c.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;
using nlohmann::json;

namespace {

const std::string SDK_COMMIT(40, 'a');
const std::string SCORBITD_COMMIT = "0123456789abcdef0123456789abcdef01234567";
const std::string CPU_MAP(64, 'c');
const std::string DMD_PATTERN(64, 'd');
const std::string NEW_CPU_MAP(64, 'e');

struct CapturingNet : Net {
    explicit CapturingNet(DeviceInfo info)
        : Net(std::move(info), {})
    {
    }

    void sendScorbitronUpdate(std::string body, std::string) override
    {
        patches.push_back(json::parse(body));
    }

    void finishLast(bool sent) { scorbitronUpdateDone(patches.back(), sent); }

    std::vector<json> patches;
};

DeviceInfo scorbitd()
{
    DeviceInfo info;
    info.scorbitdCommit = SCORBITD_COMMIT;
    info.scorbitdDirty = false;
    return info;
}

} // namespace

TEST_CASE("A digest is exactly its hex digits, lower-cased")
{
    CHECK(hexDigest(SCORBITD_COMMIT, COMMIT_HEX_DIGITS) == SCORBITD_COMMIT);
    CHECK(hexDigest("0123456789ABCDEF0123456789ABCDEF01234567", COMMIT_HEX_DIGITS)
          == SCORBITD_COMMIT);
    CHECK_FALSE(hexDigest(SCORBITD_COMMIT.substr(1), COMMIT_HEX_DIGITS));
    CHECK_FALSE(hexDigest(SCORBITD_COMMIT + "0", COMMIT_HEX_DIGITS));
    CHECK_FALSE(hexDigest(std::string(39, 'a') + "g", COMMIT_HEX_DIGITS));
    CHECK_FALSE(hexDigest("", COMMIT_HEX_DIGITS));
    CHECK(hexDigest(CPU_MAP, SHA256_HEX_DIGITS) == CPU_MAP);
    CHECK_FALSE(hexDigest(CPU_MAP, COMMIT_HEX_DIGITS));
}

TEST_CASE("The build identity carries boot_id and every known key, and only those")
{
    SECTION("everything known")
    {
        const auto fields =
                buildIdentityFields(scorbitd(), SDK_COMMIT, CPU_MAP, DMD_PATTERN, "boot-1");
        CHECK(fields
              == json {{"boot_id", "boot-1"},
                       {"sdk_commit", SDK_COMMIT},
                       {"scorbitd_commit", SCORBITD_COMMIT},
                       {"scorbitd_dirty", false},
                       {"cpu_map_sha256", CPU_MAP},
                       {"dmd_pattern_sha256", DMD_PATTERN}});
    }
    SECTION("nothing reported, as for any host other than scorbitd: only boot_id")
    {
        CHECK(buildIdentityFields(DeviceInfo {}, "", std::nullopt, std::nullopt, "boot-1")
              == json {{"boot_id", "boot-1"}});
    }
    SECTION("no active map or pattern is null, not left out")
    {
        const auto fields = buildIdentityFields(scorbitd(), SDK_COMMIT, "", "", "boot-1");
        CHECK(fields.at("cpu_map_sha256").is_null());
        CHECK(fields.at("dmd_pattern_sha256").is_null());
    }
    SECTION("a malformed value is left out, so the API reads it as unknown")
    {
        auto info = scorbitd();
        info.scorbitdCommit = "v0.8.4";
        const auto fields = buildIdentityFields(info, "not-a-commit", "xyz", DMD_PATTERN, "b");
        CHECK_FALSE(fields.contains("sdk_commit"));
        CHECK_FALSE(fields.contains("scorbitd_commit"));
        CHECK_FALSE(fields.contains("cpu_map_sha256"));
        CHECK(fields.at("dmd_pattern_sha256") == DMD_PATTERN);
        CHECK(fields.at("scorbitd_dirty") == false);
    }
    SECTION("a dirty host build says so, and hex is sent lower-case")
    {
        auto info = scorbitd();
        info.scorbitdDirty = true;
        std::string upper(64, 'C');
        const auto fields = buildIdentityFields(info, SDK_COMMIT, upper, DMD_PATTERN, "b");
        CHECK(fields.at("scorbitd_dirty") == true);
        CHECK(fields.at("cpu_map_sha256") == CPU_MAP);
    }
}

TEST_CASE("The host build config setter fills the config; never called reports nothing")
{
    const auto config = sb_config_create();
    REQUIRE(config);
    CHECK(config->scorbitdCommit.empty());
    CHECK_FALSE(config->scorbitdDirty.has_value());

    sb_config_set_scorbitd_build(config, SCORBITD_COMMIT.c_str(), true);
    CHECK(config->scorbitdCommit == SCORBITD_COMMIT);
    CHECK(config->scorbitdDirty == true);

    sb_config_set_scorbitd_build(config, nullptr, false);
    CHECK(config->scorbitdCommit.empty());
    CHECK(config->scorbitdDirty == false);

    sb_config_set_scorbitd_build(nullptr, "x", false); // ignored, no crash
    sb_config_destroy(config);
}

TEST_CASE("The full Scorbitron object carries the host build with its boot_id")
{
    Net net(scorbitd(), {});
    const auto object = NetTestAccess::scorbitronObject(net);
    CHECK(object.at("boot_id") == net.bootId());
    CHECK(object.at("scorbitd_commit") == SCORBITD_COMMIT);
    CHECK(object.at("scorbitd_dirty") == false);
    // Unknown until the host reports what it loaded
    CHECK_FALSE(object.contains("cpu_map_sha256"));
    CHECK_FALSE(object.contains("dmd_pattern_sha256"));
    // The test build is not given a commit, so it reports none rather than guess
    CHECK(object.contains("sdk_commit") == !std::string {sdkCommit()}.empty());
}

TEST_CASE("A map change sends the whole build identity with this boot's id")
{
    CapturingNet net(scorbitd());
    NetTestAccess::scorbitronObject(net);
    NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);

    net.setMappingIdentity(CPU_MAP, DMD_PATTERN);
    REQUIRE(net.patches.size() == 1);
    CHECK(net.patches[0].at("cpu_map_sha256") == CPU_MAP);
    net.finishLast(true);
    net.patches.clear();

    net.setMappingIdentity(NEW_CPU_MAP, DMD_PATTERN);
    REQUIRE(net.patches.size() == 1);
    const auto expected =
            buildIdentityFields(scorbitd(), sdkCommit(), NEW_CPU_MAP, DMD_PATTERN, net.bootId());
    CHECK(net.patches[0] == expected);
    CHECK(net.patches[0].at("scorbitd_commit") == SCORBITD_COMMIT); // unchanged, still sent
    CHECK(NetTestAccess::currentScorbitronObject(net).at("cpu_map_sha256") == NEW_CPU_MAP);

    SECTION("the same report again sends nothing")
    {
        net.finishLast(true);
        net.setMappingIdentity(NEW_CPU_MAP, DMD_PATTERN);
        CHECK(net.patches.size() == 1);
    }
    SECTION("a failed send is retried by its timer, and by the next report, until it is sent")
    {
        net.finishLast(false);
        CHECK(NetTestAccess::scorbitronRetryBackoff(net, "boot_id").has_value());

        NetTestAccess::retryBuildIdentity(net);
        REQUIRE(net.patches.size() == 2);
        CHECK(net.patches[1] == expected);
        net.finishLast(false);

        net.setMappingIdentity(NEW_CPU_MAP, DMD_PATTERN); // unchanged, but owed
        REQUIRE(net.patches.size() == 3);
        CHECK(net.patches[2] == expected);
        net.finishLast(true);
        CHECK_FALSE(NetTestAccess::scorbitronRetryBackoff(net, "boot_id").has_value());

        net.setMappingIdentity(NEW_CPU_MAP, DMD_PATTERN);
        CHECK(net.patches.size() == 3);

        // A retry armed by an earlier failure finds nothing owed once a later send landed
        NetTestAccess::retryBuildIdentity(net);
        CHECK(net.patches.size() == 3);
    }
    SECTION("no active pattern is sent as null")
    {
        net.setMappingIdentity(NEW_CPU_MAP, std::string {});
        REQUIRE(net.patches.size() == 2);
        CHECK(net.patches[1].at("dmd_pattern_sha256").is_null());
    }
    SECTION("a malformed map is left out of the set, and of the object")
    {
        net.setMappingIdentity(std::string {"bad"}, DMD_PATTERN);
        REQUIRE(net.patches.size() == 2);
        CHECK(net.patches[1].contains("boot_id"));
        CHECK_FALSE(net.patches[1].contains("cpu_map_sha256"));
        CHECK_FALSE(NetTestAccess::currentScorbitronObject(net).contains("cpu_map_sha256"));
    }
}

TEST_CASE("Before authentication a map change waits for the full object to carry it")
{
    CapturingNet net(scorbitd());
    NetTestAccess::scorbitronObject(net);

    net.setMappingIdentity(NEW_CPU_MAP, std::string {});
    CHECK(net.patches.empty());
    const auto body = json::parse(NetTestAccess::scorbitronObjectBody(net));
    CHECK(body.at("cpu_map_sha256") == NEW_CPU_MAP);
    CHECK(body.at("dmd_pattern_sha256").is_null());
    CHECK(body.at("boot_id") == net.bootId());
}
