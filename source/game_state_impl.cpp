/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include "game_state_impl.h"
#include "logger.h"
#include <scorbit_sdk/version.h>
#include <boost/uuid.hpp>
#include <utility>

namespace scorbit {
namespace detail {

GameStateImpl::GameStateImpl(std::unique_ptr<NetBase> net)
    : m_net {std::move(net)}
{
    m_net->authenticate();

    const auto &deviceInfo = m_net->deviceInfo();
    // m_net->sendInstalled("sdk", SCORBIT_SDK_VERSION, true);
    m_net->sendInstalled("provider", deviceInfo.gameCodeVersion, true);
}

void GameStateImpl::setGameStarted()
{
    if (m_data.isGameActive) {
        DBG("Game is already active, ignore starting game");
        return;
    }

    // Reset game data
    m_prevData = m_data = GameData {};

    m_data.isGameActive = true;
    m_data.sessionUuid = boost::uuids::to_string(boost::uuids::random_generator()());
    setCurrentBall(1);
    setActivePlayer(1);
    INF("New game session started: {}", m_data.sessionUuid.get());
}

void GameStateImpl::setGameFinished()
{
    m_data.isGameActive = false;
    sendGameData();

    // Reset game data
    m_data = GameData {};
}

void GameStateImpl::setCurrentBall(sb_ball_t ball)
{
    if (!isBallValid(ball)) {
        WRN("Ignoring attempt to set current ball to {}", ball);
        return;
    }

    m_data.ball = ball;
}

void GameStateImpl::setActivePlayer(sb_player_t player)
{
    if (!isPlayerValid(player)) {
        WRN("Ignoring attempt to set active player to {}", player);
        return;
    }

    if (m_data.players.count(player) == 0) {
        addNewPlayer(player);
    }

    m_data.activePlayer = player;
}

void GameStateImpl::setScore(sb_player_t player, sb_score_t score, sb_score_feature_t feature)
{
    if (!isPlayerValid(player)) {
        WRN("Ignoring attempt to set score for invalid player {}, score: {}", player, score);
        return;
    }

    if (m_data.players.count(player) == 0) {
        addNewPlayer(player);
    }

    m_data.players.at(player).setScore(score, feature);
}

void GameStateImpl::addMode(std::string mode)
{
    m_data.modes.addMode(std::move(mode));
}

void GameStateImpl::removeMode(const std::string &mode)
{
    m_data.modes.removeMode(mode);
}

void GameStateImpl::clearModes()
{
    m_data.modes.clear();
}

void GameStateImpl::commit()
{
    if (m_data.isGameActive) {
        sendGameData();
    }
}

AuthStatus GameStateImpl::getStatus() const
{
    return m_net->status();
}

std::string GameStateImpl::getMachineUuid() const
{
    return m_net->getMachineUuid();
}

std::string GameStateImpl::getPairDeeplink() const
{
    return m_net->getPairDeeplink();
}

std::string GameStateImpl::getClaimDeeplink(int player) const
{
    return m_net->getClaimDeeplink(player);
}

void GameStateImpl::requestTopScores(sb_score_t scoreFilter, StringCallback callback)
{
    m_net->requestTopScores(scoreFilter, std::move(callback));
}

void GameStateImpl::requestPairCode(StringCallback cb) const
{
    m_net->requestPairCode(std::move(cb));
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
        m_data.timestamp = std::chrono::system_clock::now();
        m_net->sendGameData(m_data);
        m_prevData = m_data;
    }
}

bool GameStateImpl::isChanged() const
{
    return m_data != m_prevData;
}

bool GameStateImpl::isPlayerValid(sb_player_t player) const
{
    return 1 <= player && player <= 9;
}

bool GameStateImpl::isBallValid(sb_ball_t ball) const
{
    return 1 <= ball && ball <= 9;
}

} // namespace detail
} // namespace scorbit
