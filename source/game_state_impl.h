/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/common_types_c.h>
#include <string>

namespace scorbit {
namespace detail {

class GameStateImpl
{
public:
    GameStateImpl();

    void setGameStarted();
    void setGameFinished();

    void setActivePlayer(sb_player_t player);
    void setScore(sb_player_t player, sb_score_t score);

    void addMode(std::string mode);
    void removeMode(const std::string &mode);
    void clearModes();

    void commit();
};

} // namespace detail
} // namespace scorbit
