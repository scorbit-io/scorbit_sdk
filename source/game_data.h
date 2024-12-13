/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/common_types_c.h>
#include "player_state.h"
#include "modes.h"
#include <boost/flyweight.hpp>
#include <map>

namespace scorbit {
namespace detail {

struct GameData {
    bool isGameActive {false};

    sb_ball_t ball {0};
    sb_player_t activePlayer {0};
    std::map<sb_player_t, PlayerState> players;
    Modes modes;

    boost::flyweight<std::string> sessionUuid;
    std::chrono::time_point<std::chrono::system_clock> timestamp;
};

inline bool operator==(const scorbit::detail::GameData &lhs, const scorbit::detail::GameData &rhs)
{
    return lhs.isGameActive == rhs.isGameActive && lhs.ball == rhs.ball
        && lhs.activePlayer == rhs.activePlayer && lhs.modes == rhs.modes
        && lhs.players == rhs.players && lhs.sessionUuid == rhs.sessionUuid;
}

inline bool operator!=(const scorbit::detail::GameData &lhs, const scorbit::detail::GameData &rhs)
{
    return !(lhs == rhs);
}

using GameHistory = std::vector<GameData>;

} // namespace detail
} // namespace scorbit
