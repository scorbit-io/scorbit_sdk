/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include "game_state_impl.h"

namespace scorbit {
namespace detail {

GameStateImpl::GameStateImpl(std::unique_ptr<NetBase> net)
    : m_net {std::move(net)}
{
}

void GameStateImpl::setGameStarted()
{
}

void GameStateImpl::setGameFinished()
{
}

void GameStateImpl::setActivePlayer(sb_player_t player)
{
    (void)player;
}

void GameStateImpl::setScore(sb_player_t player, sb_score_t score)
{
    (void)player;
    (void)score;
}

void GameStateImpl::addMode(std::string mode)
{
    (void)mode;
}

void GameStateImpl::removeMode(const std::string &mode)
{
    (void)mode;
}

void GameStateImpl::clearModes()
{
}

void GameStateImpl::commit()
{
}

} // namespace detail
} // namespace scorbit
