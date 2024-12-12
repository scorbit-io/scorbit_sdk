/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "net_util.h"
#include "fmt/format.h"
#include <boost/uuid.hpp>
#include <regex>

namespace scorbit {
namespace detail {

constexpr auto ABSOLUTE_MAX_PLAYERS_NUM = 6;

// Function to extract protocol, hostname, and port
UrlInfo exctractHostAndPort(const std::string &url)
{
    static const std::regex url_regex(R"((https?):\/\/([^\/:]+)(?::(\d+))?)");
    std::smatch url_match_result;

    UrlInfo result;

    if (std::regex_search(url, url_match_result, url_regex)) {
        result.protocol = url_match_result[1].str();
        result.hostname = url_match_result[2].str();
        result.port = url_match_result[3].str();

        // If no port is found, assign a default one based on the protocol
        if (result.port.empty()) {
            if (result.protocol == "https") {
                result.port = "443"; // Default port for HTTPS
            } else if (result.protocol == "http") {
                result.port = "80"; // Default port for HTTP
            }
        }
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
