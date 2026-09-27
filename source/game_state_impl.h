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

#pragma once

#include "scorbit_sdk/common_types_c.h"
#include <nfc/probes_manager.h>
#include "leaderboard_internal.h"
#include "net_base.h"
#include "game_data.h"
#include "achievements/achievement_service.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <optional>
#include <string>

namespace scorbit {
namespace detail {

class GameStateImpl
{
public:
    GameStateImpl(std::unique_ptr<NetBase> net);

    void setGameStarted(GameStartOrigin origin);
    void setGameFinished();

    void setCurrentBall(sb_ball_t ball);

    void setActivePlayer(sb_player_t player);
    void setScore(sb_player_t player, sb_score_t score, sb_score_feature_t feature = 0);

    void addMode(std::string mode);
    void removeMode(const std::string &mode);
    void clearModes();

    /** Queue poster from C API layer; required for expiring modes scheduling. */
    void setModeExpiryPoster(std::function<void()> postTickToCApiThread);

    /**
     * Poster running a function on the thread game state lives on (the C API dispatcher). Network
     * replies are brought back through it. Functions posted before it is set are held and handed
     * to it then; with none ever set (unit tests) call @ref runPendingPosts to run them.
     */
    void setDispatcherPoster(std::function<void(std::function<void()>)> poster);

    /** Runs the functions posted while no dispatcher poster is set; returns how many ran. */
    size_t runPendingPosts();

    /**
     * Add a mode that is removed automatically after a duration.
     * @param duration_seconds unsigned seconds; 0 is normalized to 2, values above 5 clamp to 5.
     */
    void addModeExpiring(std::string mode, uint32_t duration_seconds);

    /**
     * Mark a mode as completed (achievements input). Completed modes are not part of the active
     * mode list and are never sent to the cloud except as in CSV history logs.
     */
    void setModeCompleted(std::string mode);

    /**
     * Add @p value (may be negative) to the event register @p name (achievements input). The
     * operation is recorded in the `events` column of the next history row only.
     */
    void addEvent(std::string name, int64_t value);

    /** Called from C API thread when the worker timer fires. */
    void tickModeExpiries();

    void commit();

    AuthStatus getStatus() const;

    /** Nice / thread scheduling value from config (see @ref sb_config_set_threads_priority). */
    int configuredSdkThreadsNice() const { return m_net->deviceInfo().threadsNice; }

    const std::string &getMachineUuid() const;
    std::uint64_t getMachineSerial() const;
    const std::string &getPairDeeplink() const;

    void setCapabilities(Capabilities capabilities);

    void setCreditsDropped(int credits, const std::string &transaction, bool success);
    void setCreditsStatus(bool freePlay, int credits, int maxCredits, const char *pricing);

    void requestTopScores(LeaderboardScope scope, LeaderboardPeriod period,
                          const std::string &since, LeaderboardVpinFilter vpinFilter,
                          LeaderboardHandleCallback callback);

    void requestPairCode(StringCallback callback) const;
    void requestUnpair(StringCallback callback) const;

    void requestPairMachine(const std::string &machineUuid, const std::string &ownerUuid,
                            StringCallback callback);

    void download(StringCallback callback, const std::string &url, const std::string &filename,
                  const HttpHeaders &headers = {});
    void downloadBuffer(VectorCallback callback, const std::string &url, size_t reserveBufferSize,
                        const HttpHeaders &headers = {});

    void uploadDiagnostics(std::vector<std::string> logPaths,
                           std::vector<std::string> recordingPaths, std::string logString,
                           std::optional<std::uint64_t> requestGeneration = std::nullopt);

    void reportDeviceState(const std::string &type, const std::string &version, bool installed,
                           std::optional<std::string> log, HttpStatusCallback callback);

    void submitHardwareProbeResult(const std::string &runId, const std::string &resultJson,
                                   HttpStatusCallback callback);

    achievements::AchievementService &achievements() { return *m_achievements; }

    /** Fetches @p userId's stored achievements state as the raw progress document. */
    void fetchPlayerAchievements(const std::string &userId, StringCallback callback);
    const achievements::AchievementService &achievements() const { return *m_achievements; }

private:
    void addNewPlayer(sb_player_t player);
    void submitGameData(bool forceSending);
    void emitRow(const GameData &data, SessionFlags flags);
    void post(std::function<void()> fn);
    bool isChanged() const;
    bool isPlayerValid(sb_player_t player) const;
    bool isBallValid(sb_ball_t ball) const;
    bool startGame(int playersCount, GameStartOrigin origin);

    void rescheduleModeExpiryTimer();
    void clearModeExpirySchedule();

    // Members destroy in reverse declaration order. m_net must be destroyed FIRST so ~Net()
    // stops timers/worker and sets m_stop before GameData is destroyed; otherwise late
    // session-create replies can call submitGameData() on torn-down m_data.
    GameData m_data;
    GameData m_prevData;
    int m_sessionId {0};

    std::shared_ptr<nfc::ProbesManager> m_probesManager;

    std::function<void()> m_postModeExpiryToCApi;
    std::function<void(std::function<void()>)> m_postToDispatcher;
    std::vector<std::function<void()>> m_pendingPosts;
    std::mutex m_postMutex;

    // Declared before m_net so it outlives it: once ~Net() returns no reply can reach it.
    std::unique_ptr<achievements::AchievementService> m_achievements;

    std::unique_ptr<NetBase> m_net;
};

} // namespace detail
} // namespace scorbit
