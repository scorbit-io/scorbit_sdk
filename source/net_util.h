/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "game_data.h"
#include <string>
#include <string_view>

namespace scorbit {
namespace detail {

struct UrlInfo {
    std::string protocol;
    std::string hostname;
    std::string port;
};

UrlInfo exctractHostAndPort(const std::string &url);

std::string removeSymbols(std::string_view str, std::string_view symbols);

std::string deriveUuid(const std::string &source);

std::string parseUuid(const std::string &str);

std::string gameHistoryToCsv(const GameHistory &history);

} // namespace detail
} // namespace scorbit
