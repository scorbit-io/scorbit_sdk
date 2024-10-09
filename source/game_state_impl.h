/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/common_types_c.h>
#include <scorbit_sdk/net_base.h>
#include "game_data.h"
#include <string>
#include <memory>

namespace scorbit {
namespace detail {

class GameStateImpl
{
public:
    GameStateImpl(std::unique_ptr<NetBase> net);

    void setGameStarted();
    void setGameFinished();

    void setActivePlayer(sb_player_t player);
    void setScore(sb_player_t player, sb_score_t score);

    void addMode(std::string mode);
    void removeMode(const std::string &mode);
    void clearModes();

    void commit();

private:
    std::unique_ptr<NetBase> m_net;
    GameData m_data;
};

} // namespace detail
} // namespace scorbit
