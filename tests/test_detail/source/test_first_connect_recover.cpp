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

// On a first connect the device asks Centrifugo to recover its lobby: and attract: channels, and
// only those: recovering the token's other channels would re-send a control command and replay
// stream history.

#include "net.h"
#include "utils/jwt_parser.h"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace scorbit::detail;
using json = nlohmann::json;

namespace {

std::string base64Url(const std::string &in)
{
    static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    int value = 0;
    int bits = -6;
    for (const unsigned char c : in) {
        value = (value << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(table[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        out.push_back(table[((value << 8) >> (bits + 8)) & 0x3F]);
    }
    return out; // unpadded, as JWTs are
}

std::string token(const json &payload)
{
    return base64Url(R"({"alg":"HS256","typ":"JWT"})") + "." + base64Url(payload.dump()) + ".sig";
}

constexpr auto VM = "7a1c9e52-0d3b-4f6a-9c11-5e2b8d4f0a77";

} // namespace

TEST_CASE("parseJwtChannels reads the channels claim in order")
{
    const auto t = token({{"sub", "x"},
                          {"exp", 4102444800},
                          {"channels", {"control_scorbitron:1", "lobby:a", 7, "machine:2"}}});
    CHECK(parseJwtChannels(t) == std::vector<std::string> {"control_scorbitron:1", "lobby:a", "machine:2"});
    CHECK(parseJwtExpiration(t).has_value()); // the shared payload decoding still reads exp
}

TEST_CASE("parseJwtChannels is empty for a token without channels or that can't be decoded")
{
    CHECK(parseJwtChannels(token({{"exp", 4102444800}})).empty());
    CHECK(parseJwtChannels(token({{"channels", "lobby:a"}})).empty()); // not an array
    CHECK(parseJwtChannels("").empty());
    CHECK(parseJwtChannels("not.a.jwt!").empty());
    CHECK(parseJwtChannels("onlyonepart").empty());
}

TEST_CASE("Only the token's lobby: and attract: channels are recovered on a first connect")
{
    const auto t = token({{"channels",
                           {std::string("control_scorbitron:9"), std::string("control_machine:") + VM,
                            std::string("machine:") + VM, "variant:3", "game:4", "venue:5",
                            std::string("lobby:") + VM, std::string("attract:") + VM,
                            "lobbyist:x", "attractions:y"}}});
    CHECK(firstConnectRecoverChannels(t)
          == std::vector<std::string> {std::string("lobby:") + VM, std::string("attract:") + VM});
}

TEST_CASE("Without attract: in the token, only the lobby is recovered")
{
    const auto t = token({{"channels", {std::string("machine:") + VM, std::string("lobby:") + VM}}});
    CHECK(firstConnectRecoverChannels(t) == std::vector<std::string> {std::string("lobby:") + VM});
    CHECK(firstConnectRecoverChannels("").empty());
}
