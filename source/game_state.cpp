/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/game_state.h>
#include "game_state_impl.h"

namespace scorbit {

GameState::GameState(std::unique_ptr<detail::NetBase> net)
    : p {spimpl::make_unique_impl<detail::GameStateImpl>(std::move(net))}
{
}

void GameState::setGameStarted()
{
    p->setGameStarted();
}

void GameState::setGameFinished()
{
    p->setGameFinished();
}

void GameState::setCurrentBall(sb_ball_t ball)
{
    p->setCurrentBall(std::move(ball));
}

void GameState::setActivePlayer(sb_player_t player)
{
    p->setActivePlayer(std::move(player));
}

void GameState::setScore(sb_player_t player, sb_score_t score, sb_score_feature_t feature)
{
    p->setScore(std::move(player), std::move(score), std::move(feature));
}

void GameState::addMode(std::string mode)
{
    p->addMode(std::move(mode));
}

void GameState::removeMode(const std::string &mode)
{
    p->removeMode(mode);
}

void GameState::clearModes()
{
    p->clearModes();
}

void GameState::commit()
{
    p->commit();
}

AuthStatus GameState::getStatus() const
{
    return p->getStatus();
}

std::string GameState::getMachineUuid() const
{
    return p->getMachineUuid();
}

std::string GameState::getPairDeeplink() const
{
    return p->getPairDeeplink();
}

std::string GameState::getClaimDeeplink(int player) const
{
    return p->getClaimDeeplink(player);
}

void GameState::requestTopScores(sb_score_t scoreFilter, StringCallback callback)
{
    p->requestTopScores(scoreFilter, std::move(callback));
}

void GameState::requestPairCode(StringCallback cb) const
{
    p->requestPairCode(std::move(cb));
}

} // namespace scorbit
