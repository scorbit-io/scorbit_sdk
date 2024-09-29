/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/game_state.h>

namespace scorbit {

GameState::GameState()
{
}

void GameState::setGameStarted()
{
}

void GameState::setGameFinished()
{
}

void GameState::setActivePlayer(sb_player_t player)
{
    (void)player;
}

void GameState::setScore(sb_player_t player, sb_score_t score)
{
    (void)player;
    (void)score;
}

void GameState::addMode(std::string mode)
{
    (void)mode;
}

void GameState::removeMode(const std::string &mode)
{
    (void)mode;
}

void GameState::clearModes()
{
}

void GameState::commit()
{
}

} // namespace scorbit
