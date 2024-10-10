/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include "game_state_impl.h"
#include "logger.h"

namespace scorbit {
namespace detail {

GameStateImpl::GameStateImpl(std::unique_ptr<NetBase> net)
    : m_net {std::move(net)}
{
}

void GameStateImpl::setGameStarted()
{
    if (m_data.isGameStarted) {
        DBG("Game is already active, ignore starting game");
        return;
    }

    // Reset game data
    m_prevData = m_data = GameData {};

    m_data.isGameStarted = true;
    setCurrentBall(1);
    setActivePlayer(1);
}

void GameStateImpl::setGameFinished()
{
    m_data.isGameStarted = false;
    sendGameData();

    // Reset game data
    m_data = GameData {};
}

void GameStateImpl::setCurrentBall(sb_ball_t ball)
{
    if (ball == 0) {
        WRN("Ignoring attempt to set current ball to {}", ball);
        return;
    }

    m_data.ball = ball;
}

void GameStateImpl::setActivePlayer(sb_player_t player)
{
    if (m_data.players.count(player) == 0) {
        addNewPlayer(player);
    }

    m_data.activePlayer = player;
}

void GameStateImpl::setScore(sb_player_t player, sb_score_t score)
{
    if (m_data.players.count(player) == 0) {
        addNewPlayer(player);
    }

    m_data.players.at(player).setScore(score);
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
    if (m_data.isGameStarted) {
        sendGameData();
    }
}

void GameStateImpl::addNewPlayer(sb_player_t player)
{
    if (m_data.players.count(player) != 0) {
        // Skipping, this player already exists
        return;
    }

    m_data.players.insert(std::make_pair(player, PlayerState {player, 0}));
    DBG("Player {} added", player);
}

void GameStateImpl::sendGameData()
{
    if (isChanged()) {
        m_net->sendGameData(m_data);
        m_prevData = m_data;
    }
}

bool GameStateImpl::isChanged() const
{
    return m_data != m_prevData;
}

} // namespace detail
} // namespace scorbit
