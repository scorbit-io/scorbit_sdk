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

#include "net_util.h"
#include "fmt/format.h"
#include "logger.h"
#include <boost/uuid.hpp>
#include <boost/url/url_view.hpp>
#include <boost/url/parse.hpp>

namespace scorbit {
namespace detail {

constexpr auto ABSOLUTE_MAX_PLAYERS_NUM = 6;

// Function to extract protocol, hostname, and port
UrlInfo exctractHostAndPort(const std::string &url)
{
    const auto r = boost::urls::parse_uri(url);
    if (!r) {
        ERR("Invalid URL: {}, {}", url, r.error().message());
        return {};
    }

    const auto u = *r;
    UrlInfo result;
    result.protocol = u.scheme();
    result.hostname = u.host();

    if (u.has_port()) {
        result.port = u.port();
    } else if (result.protocol == "https" || result.protocol == "wss") {
        result.port = "443"; // Default port for HTTPS and WSS
    } else if (result.protocol == "http" || result.protocol == "ws") {
        result.port = "80"; // Default port for HTTP (if not specified, we assume HTTP)
    } else {
        // Invalid scheme: accepted only http(s) or ws(s)
        result = {};
    }

    return result;
}

std::string removeSymbols(std::string_view str, std::string_view symbols)
{
    std::string result {str};
    for (const auto &symbol : symbols) {
        result.erase(std::remove(result.begin(), result.end(), symbol), result.end());
    }

    return result;
}

// Returns a name-based UUID version 5
std::string deriveUuid(const std::string &source)
{
    using namespace boost::uuids;
    name_generator_sha1 gen(ns::dns());
    return to_string(gen(source));
}

std::string parseUuid(const std::string &str)
{
    using namespace boost::uuids;
    string_generator gen;
    uuid u1;
    try {
        u1 = gen(str);
    } catch (const std::exception &e) {
        return {};
    }

    return to_string(u1);
}

std::string gameHistoryToCsv(const GameHistory &history)
{
    std::string rv;
    rv.reserve(50 * 1024);


    // CSV header: "time,p1,p2,p3,p4,p5,p6,player,ball,game_modes\n";
    rv.append("time");
    for (sb_player_t playerNum = 1; playerNum <= ABSOLUTE_MAX_PLAYERS_NUM; ++playerNum) {
        rv.append(fmt::format(",p{}", playerNum));
    }
    rv.append(",player,ball,game_modes\n");

    // CSV body
    for (const auto &data : history) {
        auto timestamp =
                std::chrono::duration_cast<std::chrono::seconds>(data.timestamp.time_since_epoch())
                        .count();

        std::string scores;
        for (sb_player_t playerNum = 1; playerNum <= ABSOLUTE_MAX_PLAYERS_NUM; ++playerNum) {
            if (data.players.count(playerNum) && data.players.at(playerNum).score() >= 0) {
                scores.append(fmt::format("{}", data.players.at(playerNum).score()));
            }
            scores.append(",");
        }

        std::string modes;
        if (!data.modes.isEmpty()) {
            modes = fmt::format("\"{}\"", data.modes.str());
        }

        rv.append(fmt::format("{},{}{},{},{}\n", timestamp, scores, data.activePlayer, data.ball,
                              modes));
    }

    return rv;
}

} // namespace detail
} // namespace scorbit
