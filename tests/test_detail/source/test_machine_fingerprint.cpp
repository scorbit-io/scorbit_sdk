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

#include "utils/machine_fingerprint.h"
#include <catch2/catch_test_macros.hpp>
#include <platform_id.h>
#include <regex>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("collectFingerprints returns at least platform type", "[MachineFingerprint]")
{
    auto fp = collectFingerprints();

    REQUIRE_FALSE(fp.platformType.empty());
    CHECK(fp.platformType == SCORBIT_SDK_PLATFORM_ID);
    REQUIRE(fp.hasAny());
}

TEST_CASE("collectFingerprints MAC address format", "[MachineFingerprint]")
{
    auto fp = collectFingerprints();

    if (!fp.macAddressPrimary.empty()) {
        static const auto re = std::regex("([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})");
        CHECK(std::regex_match(fp.macAddressPrimary, re));
    } else {
        WARN("MAC address could not be retrieved (likely due to sandbox restrictions)");
    }
}

TEST_CASE("MachineFingerprint hasAny checks all fields", "[MachineFingerprint]")
{
    SECTION("All empty returns false")
    {
        MachineFingerprint fp;
        REQUIRE_FALSE(fp.hasAny());
    }

    SECTION("Only macAddressPrimary returns true")
    {
        MachineFingerprint fp;
        fp.macAddressPrimary = "AA:BB:CC:DD:EE:FF";
        REQUIRE(fp.hasAny());
    }

    SECTION("Only boardSerial returns true")
    {
        MachineFingerprint fp;
        fp.boardSerial = "12345678";
        REQUIRE(fp.hasAny());
    }

    SECTION("Only cpuSerial returns true")
    {
        MachineFingerprint fp;
        fp.cpuSerial = "ABCDEF";
        REQUIRE(fp.hasAny());
    }

    SECTION("Only platformType returns true")
    {
        MachineFingerprint fp;
        fp.platformType = "spike2";
        REQUIRE(fp.hasAny());
    }
}

TEST_CASE("collectFingerprints is consistent across calls", "[MachineFingerprint]")
{
    auto fp1 = collectFingerprints();
    auto fp2 = collectFingerprints();

    CHECK(fp1.macAddressPrimary == fp2.macAddressPrimary);
    CHECK(fp1.boardSerial == fp2.boardSerial);
    CHECK(fp1.cpuSerial == fp2.cpuSerial);
    CHECK(fp1.platformType == fp2.platformType);
}

TEST_CASE("computeHash returns 64-char hex SHA-256", "[MachineFingerprint]")
{
    MachineFingerprint fp;
    fp.macAddressPrimary = "AA:BB:CC:DD:EE:FF";
    fp.boardSerial = "BOARD123";
    fp.cpuSerial = "CPU456";
    fp.platformType = "spike2";

    const auto hash = fp.computeHash();
    REQUIRE(hash.size() == 64);

    static const auto hexRe = std::regex("[0-9a-f]{64}");
    CHECK(std::regex_match(hash, hexRe));
}

TEST_CASE("computeHash normalizes to lowercase", "[MachineFingerprint]")
{
    MachineFingerprint fp1;
    fp1.macAddressPrimary = "AA:BB:CC:DD:EE:FF";
    fp1.platformType = "SPIKE2";

    MachineFingerprint fp2;
    fp2.macAddressPrimary = "aa:bb:cc:dd:ee:ff";
    fp2.platformType = "spike2";

    CHECK(fp1.computeHash() == fp2.computeHash());
}

TEST_CASE("computeHash is deterministic", "[MachineFingerprint]")
{
    MachineFingerprint fp;
    fp.macAddressPrimary = "42:00:61:74:98:37";
    fp.platformType = "arm64_unknown";

    CHECK(fp.computeHash() == fp.computeHash());
}

TEST_CASE("computeHash differs for different inputs", "[MachineFingerprint]")
{
    MachineFingerprint fp1;
    fp1.macAddressPrimary = "AA:BB:CC:DD:EE:FF";

    MachineFingerprint fp2;
    fp2.macAddressPrimary = "11:22:33:44:55:66";

    CHECK(fp1.computeHash() != fp2.computeHash());
}

TEST_CASE("computeHash returns empty for all-empty fields", "[MachineFingerprint]")
{
    MachineFingerprint fp;
    CHECK(fp.computeHash().empty());
}

TEST_CASE("toJson includes only non-empty fields", "[MachineFingerprint]")
{
    MachineFingerprint fp;
    fp.macAddressPrimary = "AA:BB:CC:DD:EE:FF";
    fp.platformType = "linux_x86_64";

    auto j = fp.toJson();
    REQUIRE(j.contains("mac_address_primary"));
    CHECK(j["mac_address_primary"] == "AA:BB:CC:DD:EE:FF");
    REQUIRE(j.contains("platform_type"));
    CHECK(j["platform_type"] == "linux_x86_64");
    CHECK_FALSE(j.contains("board_serial"));
    CHECK_FALSE(j.contains("cpu_serial"));
}

TEST_CASE("toJson includes all fields when populated", "[MachineFingerprint]")
{
    MachineFingerprint fp;
    fp.macAddressPrimary = "11:22:33:44:55:66";
    fp.boardSerial = "BOARD123";
    fp.cpuSerial = "CPU456";
    fp.platformType = "arm64_rpi";

    auto j = fp.toJson();
    CHECK(j["mac_address_primary"] == "11:22:33:44:55:66");
    CHECK(j["board_serial"] == "BOARD123");
    CHECK(j["cpu_serial"] == "CPU456");
    CHECK(j["platform_type"] == "arm64_rpi");
}

TEST_CASE("toJson returns empty object for empty fingerprint", "[MachineFingerprint]")
{
    MachineFingerprint fp;
    auto j = fp.toJson();
    CHECK(j.empty());
}
