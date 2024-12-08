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
#include <map>

namespace scorbit {
namespace detail {

struct GameData {
    bool isGameStarted {false};

    sb_ball_t ball {0};
    sb_player_t activePlayer {0};
    Modes modes;
    std::map<sb_player_t, PlayerState> players;
    std::string sessionUuid;
};

inline bool operator==(const scorbit::detail::GameData &lhs, const scorbit::detail::GameData &rhs)
{
    return lhs.isGameStarted == rhs.isGameStarted && lhs.ball == rhs.ball
        && lhs.activePlayer == rhs.activePlayer && lhs.modes == rhs.modes
        && lhs.players == rhs.players && lhs.sessionUuid == rhs.sessionUuid;
}

inline bool operator!=(const scorbit::detail::GameData &lhs, const scorbit::detail::GameData &rhs)
{
    return !(lhs == rhs);
}

} // namespace detail
} // namespace scorbit
