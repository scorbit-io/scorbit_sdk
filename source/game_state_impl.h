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
#include <scorbit_sdk/achievements.h>
#include "achievement_manager.h"
#include "leaderboard_internal.h"
#include "net_base.h"
#include "game_data.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
     * Add a mode that is removed automatically after a duration.
     * @param duration_seconds unsigned seconds; 0 is normalized to 2, values above 5 clamp to 5.
     */
    void addModeExpiring(std::string mode, uint32_t duration_seconds);

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

    void requestTopScores(LeaderboardScope scope, LeaderboardPeriod period, const std::string &since,
                          LeaderboardVpinFilter vpinFilter,
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
                           std::vector<std::string> recordingPaths, std::string logString);

    // ---------------------------- Achievements ----------------------------
    //
    // The AchievementManager is owned here, alongside m_net, rather than by Net: the local cache
    // and matcher are game-state concerns, and Net stays purely a transport. The two fetch methods
    // therefore do double duty - they hand the result to the caller's callback *and* refresh the
    // local cache the check*() methods read. unlockAchievement()/lockAchievement() are pure
    // passthroughs: only incrementProgress() mutates local progress from game code, and
    // server-side unlock state arrives back through the AchievementUnlocked/AchievementLocked
    // events rather than through the unlock/lock reply.

    /** Fetch definitions from the API, delivering them to @p callback and refreshing the cache. */
    void fetchAchievements(AchievementsCallback callback);

    // TEMPORARY - LOCAL TESTING ONLY - DELETE BEFORE COMMIT. Seeds the cache directly, bypassing
    // the network, since there is no real key.json available for auth on this dev machine.
    void debugSeedAchievements(std::vector<Achievement> achievements)
    {
        m_achievementManager.setAchievements(std::move(achievements));
    }

    /** Fetch one user's progress, delivering it to @p callback and refreshing the cache. */
    void fetchAchievementProgress(const std::string &userId, AchievementProgressCallback callback);

    /** Ask the server to unlock an achievement. Does not touch the local cache. */
    void unlockAchievement(const std::string &userId, const std::string &key, int count,
                           AchievementUnlockCallback callback);

    /** Ask the server to revoke a trophy. Does not touch the local cache. */
    void lockAchievement(const std::string &userId, const std::string &key,
                         AchievementUnlockCallback callback);

    // Local matching and cache access - no network, no job queue. AchievementManager is internally
    // thread-safe, so these are plain synchronous delegations.

    std::vector<std::string> checkModeAchievements(const std::string &modeName,
                                                  const std::string &modeType, const std::string &userId);
    std::vector<std::string> checkModeAchievementsWithScore(const std::string &modeName,
                                                            const std::string &modeType,
                                                            const std::string &userId, int64_t score);
    std::vector<std::string> checkScoreAchievements(int64_t score, const std::string &userId);
    bool incrementProgress(const std::string &key, const std::string &userId, int increment,
                           const std::string &metricKey);
    void clearUserProgress(const std::string &userId);
    void clearAllProgress();

    bool hasAchievements();
    std::vector<Achievement> getCachedAchievements();
    std::optional<Achievement> getCachedAchievement(const std::string &key);
    std::optional<AchievementProgress> getCachedProgress(const std::string &userId, const std::string &key);

    void setAchievementTriggeredCallback(AchievementTriggeredCallback callback);

    /** Download every cached definition's artwork that has no DMD frame cached yet. */
    void downloadAchievementFrames();
    bool hasDmdFrame(const std::string &key);
    DmdFrame getDmdFrame(const std::string &key);

private:
    void addNewPlayer(sb_player_t player);
    void submitGameData(bool forceSending);
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

    // Declared before m_net so it outlives it: the fetch callbacks handed to m_net capture `this`
    // and write into the manager, and ~Net() stops the worker before this member is destroyed.
    AchievementManager m_achievementManager;

    std::unique_ptr<NetBase> m_net;
};

} // namespace detail
} // namespace scorbit
