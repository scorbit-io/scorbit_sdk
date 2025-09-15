/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#pragma once

#include <fmt/format.h>

namespace scorbit {
namespace detail {

struct GameData;

enum class GameStartOrigin {
    StartButton, // started by the machine when player press Start button
    FromLobby,   // started explicitly via Scorbit app request
};

} // namespace detail
} // namespace scorbit

template<>
struct fmt::formatter<scorbit::detail::GameStartOrigin> : fmt::formatter<std::string_view> {
    auto format(scorbit::detail::GameStartOrigin c, fmt::format_context &ctx) const
    {
        using namespace scorbit::detail;
        std::string_view name = "unknown";
        switch (c) {
        case GameStartOrigin::StartButton:
            name = "StartButton";
            break;
        case GameStartOrigin::FromLobby:
            name = "FromLobby";
            break;
        }
        return fmt::formatter<std::string_view>::format(name, ctx);
    }
};
