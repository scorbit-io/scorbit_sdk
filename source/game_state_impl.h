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

#pragma once

#include "scorbit_sdk/common_types_c.h"
#include <scorbit_sdk/achievements.h>
#include <nfc/probes_manager.h>
#include "net_base.h"
#include "game_data.h"
#include "event_classes.h"
#include <string>
#include <memory>

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

    void commit();

    AuthStatus getStatus() const;

    const std::string &getMachineUuid() const;
    const std::string &getPairDeeplink() const;
    const std::string &getClaimDeeplink(int player) const;

    bool isPlayersInfoUpdated();
    std::optional<PlayerProfile> getPlayerProfile(sb_player_t player) const;
    const Picture &getPlayerPicture(sb_player_t player) const;

    void setCapabilities(Capabilities capabilities);

    void setCreditsDropped(int credits, const std::string &transaction, bool success);
    void setCreditsStatus(bool freePlay, int credits, int maxCredits, const char *pricing);

    void requestTopScores(sb_score_t scoreFilter, StringCallback callback);

    void requestPairCode(StringCallback callback) const;
    void requestUnpair(StringCallback callback) const;

    void requestPairMachine(const std::string &machineUuid, const std::string &ownerUuid,
                            StringCallback callback);

    // Achievement REST API
    void fetchAchievements(AchievementsCallback callback);
    void fetchAchievementProgress(int64_t userId, AchievementProgressCallback callback);
    void unlockAchievement(int64_t userId, const std::string &achievementKey, int count,
                           AchievementUnlockCallback callback);
    void lockAchievement(int64_t userId, const std::string &achievementKey,
                         AchievementUnlockCallback callback);

    // Achievement caching and local matching
    bool hasAchievements() const;
    std::vector<Achievement> getAchievements() const;
    std::optional<Achievement> getAchievement(const std::string &key) const;
    std::optional<std::vector<AchievementProgress>> getUserProgress(int64_t userId) const;
    std::optional<AchievementProgress> getProgress(int64_t userId, const std::string &key) const;
    std::vector<std::string> checkModeAchievements(const std::string &modeName,
                                                   const std::string &modeType, int64_t userId) const;
    std::vector<std::string> checkModeAchievementsWithScore(const std::string &modeName,
                                                            const std::string &modeType,
                                                            int64_t userId, int64_t score) const;
    std::vector<std::string> checkScoreAchievements(int64_t score, int64_t userId) const;
    bool incrementProgress(const std::string &key, int64_t userId, int increment = 1);
    void setAchievementTriggeredCallback(
            std::function<void(const std::string &, int64_t, bool, int)> callback);

    // DMD Frame Download (internal for scorbitd)
    void downloadAchievementFrames();
    bool hasDmdFrame(const std::string &key) const;
    std::vector<uint8_t> getDmdFrame(const std::string &key) const;

private:
    void addNewPlayer(sb_player_t player);
    void submitGameData(bool forceSending);
    bool isChanged() const;
    bool isPlayerValid(sb_player_t player) const;
    bool isBallValid(sb_ball_t ball) const;
    bool startGame(int playersCount, GameStartOrigin origin);

private:
    std::unique_ptr<NetBase> m_net;
    GameData m_data;
    GameData m_prevData;
    int m_sessionId {0};

    std::shared_ptr<nfc::ProbesManager> m_probesManager;
};

} // namespace detail
} // namespace scorbit
