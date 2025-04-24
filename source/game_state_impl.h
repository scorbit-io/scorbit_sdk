/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#pragma once

#include "scorbit_sdk/common_types_c.h"
#include "net_base.h"
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

    void setCurrentBall(sb_ball_t ball);

    void setActivePlayer(sb_player_t player);
    void setScore(sb_player_t player, sb_score_t score, sb_score_feature_t feature);

    void addMode(std::string mode);
    void removeMode(const std::string &mode);
    void clearModes();

    void commit();

    AuthStatus getStatus() const;

    const std::string &getMachineUuid() const;
    const std::string &getPairDeeplink() const;
    const std::string &getClaimDeeplink(int player) const;

    void requestTopScores(sb_score_t scoreFilter, StringCallback callback);

    void requestPairCode(StringCallback callback) const;
    void requestUnpair(StringCallback callback) const;

private:
    void addNewPlayer(sb_player_t player);
    void sendGameData();
    bool isChanged() const;
    bool isPlayerValid(sb_player_t player) const;
    bool isBallValid(sb_ball_t ball) const;

private:
    std::unique_ptr<NetBase> m_net;
    GameData m_data;
    GameData m_prevData;
};

} // namespace detail
} // namespace scorbit
