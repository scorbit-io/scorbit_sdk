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

void GameState::setScore(sb_player_t player, sb_score_t score)
{
    p->setScore(std::move(player), std::move(score));
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

} // namespace scorbit
