/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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
#include "fmt_formatters.h"
#include <logger/logger.h>
#include <scorbit_sdk/version.h>
#include <nfc/probes_manager.h>
#include <nfc/Probe.h>
#include <boost/uuid.hpp>
#include <boost/filesystem.hpp>
#include <utility>
#include <functional>

using namespace std::placeholders;

namespace {

void displayProbeInfo(ProbeBase *probe, const std::string &device)
{
    ProbeBase::ProbeInformations_t probeInfo;
    if (probe->GetInformations(&probeInfo) && !probeInfo.Id.empty()) {
        INF("Found probe: port {} = {} ({}) / v{}.{}.{} ({})", device, probeInfo.Id, probeInfo.Name,
            probeInfo.VersionMajor, probeInfo.VersionMinor, probeInfo.VersionRevision,
            probeInfo.Timestamp);
    }
}

int64_t toEpochMs(std::chrono::system_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

/** The fact model's view of one committed row: exactly what the session history CSV records. */
scorbit::detail::achievements::TimelineRow toTimelineRow(const scorbit::detail::GameData &data)
{
    scorbit::detail::achievements::TimelineRow row;
    row.timeMs = toEpochMs(data.timestamp);
    row.player = data.activePlayer;
    row.ball = data.ball;
    for (const auto &[player, state] : data.players) {
        if (state.score() >= 0) {
            row.scores.emplace(player, state.score());
        }
    }
    row.modes = data.modes.names();
    row.completedModes = data.completedModes.names();
    for (const auto &op : data.events) {
        row.events.emplace_back(op.name.get(), op.value);
    }
    return row;
}

/** Where the achievements cache lives: the configured data dir, else a temporary one. */
std::string achievementsDataDir(const scorbit::DeviceInfo &deviceInfo)
{
    if (!deviceInfo.dataDir.empty()) {
        return deviceInfo.dataDir;
    }

    boost::system::error_code ec;
    const auto dir = boost::filesystem::temp_directory_path(ec) / "scorbit_sdk"
                   / (deviceInfo.uuid.empty() ? std::string {"default"} : deviceInfo.uuid);
    INF("Achievements: no data dir configured, caching in {}", dir.string());
    return dir.string();
}

scorbit::detail::EventPtr toEvent(scorbit::detail::achievements::AchievementNotification n)
{
    using scorbit::detail::achievements::AchievementStatus;

    sb_achievement_status_t status = SB_ACHIEVEMENT_PROGRESS;
    switch (n.status) {
    case AchievementStatus::Progress:
        status = SB_ACHIEVEMENT_PROGRESS;
        break;
    case AchievementStatus::UnlockedLocally:
        status = SB_ACHIEVEMENT_UNLOCKED_LOCALLY;
        break;
    case AchievementStatus::Confirmed:
        status = SB_ACHIEVEMENT_CONFIRMED;
        break;
    case AchievementStatus::AlreadyHeld:
        status = SB_ACHIEVEMENT_ALREADY_HELD;
        break;
    case AchievementStatus::Retracted:
        status = SB_ACHIEVEMENT_RETRACTED;
        break;
    }

    std::vector<scorbit::detail::AchievementUpdatedEvent::Rule> rules;
    for (const auto &[index, progress] : n.rules) {
        rules.push_back({index, progress.value, progress.satisfied});
    }

    return std::make_shared<scorbit::detail::AchievementUpdatedEvent>(
            std::move(n.key), n.player, std::move(n.userId), status, std::move(rules));
}

} // namespace

namespace scorbit {
namespace detail {

GameStateImpl::GameStateImpl(std::unique_ptr<NetBase> net)
    : m_probesManager {std::make_shared<nfc::ProbesManager>()}
    , m_net {std::move(net)}
{
    m_probesManager->enumerate(nfc::ProbeType::NFC, std::string {}, displayProbeInfo);
    m_probesManager->setNfcLeds(nfc::NfcLedMode::Idle);

    m_net->setProbesManager(m_probesManager);
    // m_net->connectToGameStartRequested(std::bind(&GameStateImpl::gameStartRequested, this, _1));
    m_net->authenticate();

    m_achievements = std::make_unique<achievements::AchievementService>(
            *m_net, achievements::AchievementStorage {achievementsDataDir(m_net->deviceInfo())},
            [this](std::function<void()> fn) { post(std::move(fn)); },
            [this](achievements::AchievementNotification notification) {
                m_net->publishEvent(toEvent(std::move(notification)));
            });
    m_net->setPlayersChangedCallback([this](const std::vector<PlayerProfile> &) {
        post([this] { m_achievements->onPlayersChanged(); });
    });
    m_achievements->refreshDefinitions();
}

void GameStateImpl::setDispatcherPoster(std::function<void(std::function<void()>)> poster)
{
    std::vector<std::function<void()>> pending;
    {
        std::scoped_lock lock(m_postMutex);
        m_postToDispatcher = poster;
        pending.swap(m_pendingPosts);
    }
    for (auto &fn : pending) {
        poster(std::move(fn));
    }
}

void GameStateImpl::post(std::function<void()> fn)
{
    std::function<void(std::function<void()>)> poster;
    {
        std::scoped_lock lock(m_postMutex);
        if (!m_postToDispatcher) {
            m_pendingPosts.push_back(std::move(fn));
            return;
        }
        poster = m_postToDispatcher;
    }
    poster(std::move(fn));
}

size_t GameStateImpl::runPendingPosts()
{
    std::vector<std::function<void()>> pending;
    {
        std::scoped_lock lock(m_postMutex);
        pending.swap(m_pendingPosts);
    }
    for (auto &fn : pending) {
        fn();
    }
    return pending.size();
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

    clearModeExpirySchedule();

    m_probesManager->setNfcLeds(nfc::NfcLedMode::Idle);

    m_data.isGameActive = false;
    submitGameData(false);
    m_achievements->onSessionFinished(toEpochMs(std::chrono::system_clock::now()));

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

void GameStateImpl::setModeExpiryPoster(std::function<void()> postTickToCApiThread)
{
    m_postModeExpiryToCApi = std::move(postTickToCApiThread);
}

void GameStateImpl::addModeExpiring(std::string mode, uint32_t duration_seconds)
{
    if (!m_data.isGameActive) {
        return;
    }

    m_data.modes.addModeExpiring(std::move(mode), duration_seconds);
    rescheduleModeExpiryTimer();
}

void GameStateImpl::setModeCompleted(std::string mode)
{
    if (!m_data.isGameActive) {
        return;
    }

    m_data.completedModes.addMode(std::move(mode));
}

void GameStateImpl::addEvent(std::string name, int64_t value)
{
    if (!m_data.isGameActive) {
        return;
    }

    if (!isValidTimelineName(name)) {
        WRN("Ignoring event with invalid name '{}'", name);
        return;
    }

    m_data.events.push_back(EventOp {std::move(name), value});
}

void GameStateImpl::tickModeExpiries()
{
    if (!m_data.isGameActive) {
        clearModeExpirySchedule();
        return;
    }

    m_data.modes.tickExpiries();
    rescheduleModeExpiryTimer();
}

void GameStateImpl::rescheduleModeExpiryTimer()
{
    if (!m_postModeExpiryToCApi) {
        return;
    }

    auto delay = m_data.modes.nextExpiryDelay();
    if (!delay) {
        m_net->cancelModeExpiryTimer();
        return;
    }

    m_net->scheduleDelayedOnWorker(*delay, [this]() {
        if (m_postModeExpiryToCApi) {
            m_postModeExpiryToCApi();
        }
    });
}

void GameStateImpl::clearModeExpirySchedule()
{
    m_data.modes.clearExpiries();
    m_net->cancelModeExpiryTimer();
}

void GameStateImpl::removeMode(const std::string &mode)
{
    if (!m_data.isGameActive) {
        return;
    }

    const bool hadExpiry = m_data.modes.hasExpiryDeadlines();
    m_data.modes.removeMode(mode);
    if (hadExpiry) {
        rescheduleModeExpiryTimer();
    }
}

void GameStateImpl::clearModes()
{
    if (!m_data.isGameActive) {
        return;
    }

    m_data.modes.clear();
    m_net->cancelModeExpiryTimer();
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

std::uint64_t GameStateImpl::getMachineSerial() const
{
    return m_net->getMachineSerial();
}

const std::string &GameStateImpl::getPairDeeplink() const
{
    return m_net->getPairDeeplink();
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

void GameStateImpl::requestTopScores(LeaderboardScope scope, LeaderboardPeriod period,
                                     const std::string &since, LeaderboardVpinFilter vpinFilter,
                                     LeaderboardHandleCallback callback)
{
    m_net->requestTopScores(scope, period, since, vpinFilter, std::move(callback));
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

void GameStateImpl::download(StringCallback callback, const std::string &url,
                             const std::string &filename, const HttpHeaders &headers)
{
    m_net->download(true, std::move(callback), url, filename, headers);
}

void GameStateImpl::downloadBuffer(VectorCallback callback, const std::string &url,
                                   size_t reserveBufferSize, const HttpHeaders &headers)
{
    m_net->downloadBuffer(true, std::move(callback), url, reserveBufferSize, headers);
}

void GameStateImpl::uploadDiagnostics(std::vector<std::string> logPaths,
                                      std::vector<std::string> recordingPaths,
                                      std::string logString,
                                      std::optional<std::uint64_t> requestGeneration)
{
    m_net->uploadDiagnostics(std::move(logPaths), std::move(recordingPaths), std::move(logString),
                             requestGeneration);
}

void GameStateImpl::reportDeviceState(const std::string &type, const std::string &version,
                                      bool installed, std::optional<std::string> log,
                                      HttpStatusCallback callback)
{
    m_net->updateConfig(type, version, installed, std::move(log), std::move(callback));
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
    if (m_net->status() == AuthStatus::AuthenticationFailed) {
        return;
    }

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
                    // Modes, completed modes and events belong to the real update below, not to
                    // this synthetic bonus score row: otherwise edges would be reported twice, and
                    // a mode started in this update would be attributed to the previous player.
                    tempData.modes = m_prevData.modes;
                    tempData.completedModes.clear();
                    tempData.events.clear();

                    SessionFlags tempFlags;
                    tempFlags.set(SessionFlag::UploadHistoryLogs);

                    INF("Detected bonus score for previous player {}, submit CSV logs as current "
                        "player",
                        prevActivePlayer);
                    emitRow(tempData, tempFlags);
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
        emitRow(m_data, flags);

        m_prevData = m_data;

        // Completed modes and events are one-shot edges: they are reported in the update just
        // published and must not be repeated in the following ones. Clearing both keeps
        // isChanged() false, so an otherwise unchanged game state doesn't produce an extra update.
        m_data.completedModes.clear();
        m_prevData.completedModes.clear();
        m_data.events.clear();
        m_prevData.events.clear();
    }
}

void GameStateImpl::emitRow(const GameData &data, SessionFlags flags)
{
    // Every row of the session history also feeds achievements, so both see the same timeline
    m_net->submitGameData(data, flags);
    m_achievements->onRow(toTimelineRow(data));
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

    if (m_net->status() == AuthStatus::AuthenticationFailed) {
        return false;
    }

    m_probesManager->setNfcLeds(nfc::NfcLedMode::GameSession);

    clearModeExpirySchedule();

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
    m_net->sessionCreate(m_data, origin, [this, sessionId = m_data.id](const std::string &uuid) {
        // Runs on a network thread: bring it back to the thread game state lives on
        post([this, sessionId, uuid] {
            m_achievements->onSessionCreated(sessionId, uuid);
            // A reply for a game that has already ended must not publish the next one's state
            if (m_data.isGameActive && m_data.id == sessionId) {
                submitGameData(true);
            }
        });
    });
    m_achievements->onSessionStarted(m_data.id, toTimelineRow(m_data));

    return true;
}

} // namespace detail
} // namespace scorbit
