/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "game_state_impl.h"
#include "logger.h"
#include <scorbit_sdk/version.h>
#include <spb/probes_manager.h>
#include <spb/Probe.h>
#include <boost/uuid.hpp>
#include <utility>
#include <functional>

using namespace std::placeholders;

namespace {

void displayProbeInfo(ProbeBase *probe, const std::string &device)
{
    ProbeBase::ProbeInformations_t probeInfo;
    if (probe->GetInformations(&probeInfo) && probeInfo.Id.size() != 0) {
        INF("Found probe: port {} = {} ({}) / v{}.{}.{} ({})", device, probeInfo.Id, probeInfo.Name,
            probeInfo.VersionMajor, probeInfo.VersionMinor, probeInfo.VersionRevision,
            probeInfo.Timestamp);
    }
}

} // namespace

namespace scorbit {
namespace detail {

GameStateImpl::GameStateImpl(std::unique_ptr<NetBase> net)
    : m_net {std::move(net)}
    , m_probesManager {std::make_shared<spb::ProbesManager>()}
{
    m_probesManager->enumerate(spb::ProbeType::NFC, std::string {}, displayProbeInfo);
    m_probesManager->setNfcLeds(spb::NfcLedMode::Idle);

    m_net->setProbesManager(m_probesManager);
    // m_net->connectToGameStartRequested(std::bind(&GameStateImpl::gameStartRequested, this, _1));
    m_net->authenticate();
}

void GameStateImpl::setEventCallback(EventCallback &&callback)
{
    m_net->setEventCallback(std::move(callback));
}

void GameStateImpl::setGameStarted(GameStartOrigin origin)
{
    startGame(1, origin);
}

void GameStateImpl::setGameFinished()
{
    if (!m_data.isGameActive) {
        return;
    }

    m_probesManager->setNfcLeds(spb::NfcLedMode::Idle);

    m_data.isGameActive = false;
    submitGameData(false);

    // Reset game data
    m_data = GameData {};
}

void GameStateImpl::setCurrentBall(sb_ball_t ball)
{
    if (!m_data.isGameActive) {
        return;
    }

    if (!isBallValid(ball)) {
        WRN("Ignoring attempt to set current ball to {}", ball);
        return;
    }

    m_data.ball = ball;
}

void GameStateImpl::setActivePlayer(sb_player_t player)
{
    if (!m_data.isGameActive) {
        return;
    }

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
    if (!m_data.isGameActive) {
        return;
    }

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
    if (!m_data.isGameActive) {
        return;
    }

    m_data.modes.addMode(std::move(mode));
}

void GameStateImpl::removeMode(const std::string &mode)
{
    if (!m_data.isGameActive) {
        return;
    }

    m_data.modes.removeMode(mode);
}

void GameStateImpl::clearModes()
{
    if (!m_data.isGameActive) {
        return;
    }

    m_data.modes.clear();
}

void GameStateImpl::commit()
{
    if (m_data.isGameActive) {
        submitGameData(false);
    }
}

AuthStatus GameStateImpl::getStatus() const
{
    return m_net->status();
}

const std::string &GameStateImpl::getMachineUuid() const
{
    return m_net->getMachineUuid();
}

const std::string &GameStateImpl::getPairDeeplink() const
{
    return m_net->getPairDeeplink();
}

const std::string &GameStateImpl::getClaimDeeplink(int player) const
{
    return m_net->getClaimDeeplink(player);
}

bool GameStateImpl::isPlayersInfoUpdated()
{
    return m_net->playersManager().hasUpdate();
}

std::optional<PlayerProfile> GameStateImpl::getPlayerProfile(sb_player_t player) const
{
    return m_net->playersManager().profile(player);
}

const Picture &GameStateImpl::getPlayerPicture(sb_player_t player) const
{
    return m_net->playersManager().picture(player);
}

void GameStateImpl::setCapabilities(Capabilities capabilities)
{
    m_net->setCapabilities(capabilities);
}

void GameStateImpl::setCreditsDropped(int credits, const std::string &transaction, bool success)
{
    m_net->setCreditsDropped(credits, transaction, success);
}

void GameStateImpl::setCreditsStatus(bool freePlay, int credits, int maxCredits,
                                     const char *pricing)
{
    m_net->setCreditsStatus(freePlay, credits, maxCredits, pricing);
}

void GameStateImpl::requestTopScores(sb_score_t scoreFilter, StringCallback callback)
{
    m_net->requestTopScores(scoreFilter, std::move(callback));
}

void GameStateImpl::requestPairCode(StringCallback callback) const
{
    m_net->requestPairCode(std::move(callback));
}

void GameStateImpl::requestUnpair(StringCallback callback) const
{
    m_net->requestUnpair(std::move(callback));
}

void GameStateImpl::requestPairMachine(const std::string &machineUuid, const std::string &ownerUuid,
                                       StringCallback callback)
{
    m_net->requestPairMachine(machineUuid, ownerUuid, std::move(callback));
}

void GameStateImpl::addNewPlayer(sb_player_t player)
{
    if (m_data.players.count(player) != 0) {
        // Skipping, this player already exists
        return;
    }

    m_data.players.emplace(std::make_pair(player, PlayerState {player, 0}));
    DBG("Player {} added", player);
}

void GameStateImpl::submitGameData(bool forceSending)
{
    if (isChanged() || forceSending) {
        m_data.timestamp = std::chrono::system_clock::now();

        const auto isGameJustStarted = !m_prevData.isGameActive && m_data.isGameActive;
        const auto isGameJustFinished = m_prevData.isGameActive && !m_data.isGameActive;

        const auto isActivePlayerChanged = m_prevData.activePlayer != m_data.activePlayer;
        const auto isBallChanged = m_prevData.ball != m_data.ball;
        const auto isPlayersNumberChanged = m_prevData.players.size() != m_data.players.size();

        bool bonusScoreSubmitted = false;

        // If active player changed and previous player's score also changed due to bonus, do extra
        // submit with previous player as current player and then submit again with new active
        // player.
        if (isActivePlayerChanged) {
            const auto prevActivePlayer = m_prevData.activePlayer;
            if (prevActivePlayer != 0 && m_prevData.players.count(prevActivePlayer) != 0
                && m_data.players.count(prevActivePlayer) != 0) {
                const auto &prevPlayerPrevState = m_prevData.players.at(prevActivePlayer);
                const auto &prevPlayerCurrState = m_data.players.at(prevActivePlayer);
                if (prevPlayerPrevState.score() != prevPlayerCurrState.score()) {
                    GameData tempData = m_data;
                    // Use previous active player as current active player and prev ball
                    tempData.activePlayer = prevActivePlayer;
                    tempData.ball = m_prevData.ball;

                    SessionFlags tempFlags;
                    tempFlags.set(SessionFlag::UploadHistoryLogs);

                    INF("Detected bonus score for previous player {}, submit CSV logs as current "
                        "player",
                        prevActivePlayer);
                    m_net->submitGameData(tempData, tempFlags);
                    bonusScoreSubmitted = true;
                }
            }
        }

        // Conditions to upload session logs
        // Skip session update right after game start or if bonus score already submitted
        const auto hasToUploadSessionLogs =
                !bonusScoreSubmitted && !isGameJustStarted
                && (isActivePlayerChanged || isBallChanged || isGameJustFinished);

        // Conditions to update session
        const auto hasToUpdateSession = hasToUploadSessionLogs || isPlayersNumberChanged;

        SessionFlags flags;

        // Update session at certain conditions
        if (hasToUpdateSession) {
            if (hasToUploadSessionLogs) {
                flags.set(SessionFlag::UploadHistoryLogs);
            }

            if (isPlayersNumberChanged) {
                flags.set(SessionFlag::PlayersAdd);
            }
        }

        // Publish game data
        m_net->submitGameData(m_data, flags);

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

bool GameStateImpl::startGame(int playersCount, GameStartOrigin origin)
{
    if (m_data.isGameActive) {
        return false;
    }

    m_probesManager->setNfcLeds(spb::NfcLedMode::GameSession);

    // Reset game data
    m_prevData = m_data = GameData {};

    m_data.id = ++m_sessionId;
    m_data.isGameActive = true;

    for (int i = 1; i <= playersCount; ++i) {
        addNewPlayer(i);
    }

    setCurrentBall(1);
    setActivePlayer(1);
    m_data.timestamp = std::chrono::system_clock::now();

    INF("New game session started, id: {}, game start origin: {}", m_data.id, origin);

    // Create session and send initial game data later when session uuid will be available,
    // so it will publish initial state (which maybe 0) to centrifugo channel.
    // This prevents situation when just started game doesn't publish 0 and app stuck waiting
    m_net->sessionCreate(m_data, origin, std::bind(&GameStateImpl::submitGameData, this, true));

    return true;
}

} // namespace detail
} // namespace scorbit
