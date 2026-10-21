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

// SB-4880: the game's firmware, set by the host, rides on the Scorbitron object.
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

using namespace scorbit;
using namespace scorbit::detail;

namespace {

DeviceInfo withFirmware(std::string id, std::string version, std::string serial,
                        std::string pbspkcomm)
{
    DeviceInfo info;
    info.gameFirmwareId = std::move(id);
    info.gameFirmwareVersion = std::move(version);
    info.gameSerial = std::move(serial);
    info.pbspkcommFirmwareId = std::move(pbspkcomm);
    return info;
}

} // namespace

TEST_CASE("The game firmware setters fill the config, and NULL clears a field")
{
    const auto config = sb_config_create();
    REQUIRE(config);

    sb_config_set_game_firmware(config, "87e6f23c", "v8.48", "0123456789abcdef");
    sb_config_set_pbspkcomm_firmware_id(config, "87e6f23c");
    CHECK(config->gameFirmwareId == "87e6f23c");
    CHECK(config->gameFirmwareVersion == "v8.48");
    CHECK(config->gameSerial == "0123456789abcdef");
    CHECK(config->pbspkcommFirmwareId == "87e6f23c");

    sb_config_set_game_firmware(config, "1b4474ef", nullptr, nullptr);
    sb_config_set_pbspkcomm_firmware_id(config, nullptr);
    CHECK(config->gameFirmwareId == "1b4474ef");
    CHECK(config->gameFirmwareVersion.empty());
    CHECK(config->gameSerial.empty());
    CHECK(config->pbspkcommFirmwareId.empty());

    sb_config_set_game_firmware(nullptr, "1b4474ef", "v8.48", "1234"); // ignored, no crash
    sb_config_destroy(config);
}

TEST_CASE("The Scorbitron object carries the game firmware only where it is set")
{
    SECTION("all four set")
    {
        Net net(withFirmware("87e6f23c", "v8.48", "0123456789abcdef", "87e6f23c"), {});
        const auto object = NetTestAccess::scorbitronObject(net);
        CHECK(object.at("game_firmware_id") == "87e6f23c");
        CHECK(object.at("game_firmware_version") == "v8.48");
        CHECK(object.at("game_serial") == "0123456789abcdef");
        CHECK(object.at("pbspkcomm_firmware_id") == "87e6f23c");
    }
    SECTION("only the ID: the others are left out, not sent empty")
    {
        Net net(withFirmware("1b4474ef", "", "", ""), {});
        const auto object = NetTestAccess::scorbitronObject(net);
        CHECK(object.at("game_firmware_id") == "1b4474ef");
        CHECK_FALSE(object.contains("game_firmware_version"));
        CHECK_FALSE(object.contains("game_serial"));
        CHECK_FALSE(object.contains("pbspkcomm_firmware_id"));
    }
    SECTION("nothing set, as for any host other than scorbitd")
    {
        Net net(DeviceInfo {}, {});
        const auto object = NetTestAccess::scorbitronObject(net);
        CHECK_FALSE(object.contains("game_firmware_id"));
        CHECK_FALSE(object.contains("game_firmware_version"));
        CHECK_FALSE(object.contains("game_serial"));
        CHECK_FALSE(object.contains("pbspkcomm_firmware_id"));
    }
}
