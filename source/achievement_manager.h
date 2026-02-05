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

#include <scorbit_sdk/achievements.h>
#include "utils/lru_cache.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {

constexpr auto MAX_DMD_FRAMES_CACHED = 32; // Maximum number of DMD frames to cache

using DmdFrame = std::vector<uint8_t>; // DMD frame binary data (PNG or raw)

/**
 * @brief Callback for achievement events triggered by local matching.
 *
 * @param key Achievement key that was triggered
 * @param userId User ID for whom the achievement was triggered
 * @param isUnlock true if unlocked, false if progress update
 * @param progress Current progress value
 */
using AchievementTriggeredCallback =
        std::function<void(const std::string &key, int64_t userId, bool isUnlock, int progress)>;

/**
 * @brief AchievementManager handles caching and local matching of achievements.
 *
 * Features:
 * - Caches achievement definitions fetched from server
 * - Caches user progress per user
 * - Provides local matching against game state (modes, scores)
 * - Caches DMD frames for display (internal use by scorbitd)
 */
class AchievementManager
{
public:
    AchievementManager() = default;

    // ---- Achievement Definition Cache ----

    /**
     * @brief Store fetched achievements in cache.
     * @param achievements Vector of achievement definitions from server
     */
    void setAchievements(std::vector<Achievement> achievements);

    /**
     * @brief Check if achievements have been cached.
     */
    bool hasAchievements() const;

    /**
     * @brief Get all cached achievements.
     */
    std::vector<Achievement> getAchievements() const;

    /**
     * @brief Get a specific achievement by key.
     * @return Achievement if found, nullopt otherwise
     */
    std::optional<Achievement> getAchievement(const std::string &key) const;

    /**
     * @brief Clear cached achievements.
     */
    void clearAchievements();

    // ---- User Progress Cache ----

    /**
     * @brief Store user's achievement progress in cache.
     * @param userId User ID
     * @param progress Vector of progress entries
     */
    void setUserProgress(int64_t userId, std::vector<AchievementProgress> progress);

    /**
     * @brief Get cached progress for a user.
     * @return Progress vector if cached, nullopt otherwise
     */
    std::optional<std::vector<AchievementProgress>> getUserProgress(int64_t userId) const;

    /**
     * @brief Get progress for a specific achievement and user.
     * @return Progress entry if found, nullopt otherwise
     */
    std::optional<AchievementProgress> getProgress(int64_t userId, const std::string &key) const;

    /**
     * @brief Update local progress for a user's achievement.
     * Called when progress events are received or local matching triggers.
     */
    void updateProgress(int64_t userId, const std::string &key, int progress, bool unlocked);

    /**
     * @brief Clear progress cache for a user (e.g., on session end for limited achievements).
     */
    void clearUserProgress(int64_t userId);

    /**
     * @brief Clear all progress caches.
     */
    void clearAllProgress();

    // ---- Local Achievement Matching ----

    /**
     * @brief Check if a mode-based achievement should trigger.
     * @param modeName The mode that started/completed
     * @param modeType "start", "complete", or "stack"
     * @param userId User to check against
     * @return Vector of achievement keys that match
     */
    std::vector<std::string> checkModeAchievements(const std::string &modeName,
                                                   const std::string &modeType,
                                                   int64_t userId) const;

    /**
     * @brief Check if a score-based achievement should trigger.
     * @param score Current score
     * @param userId User to check against
     * @return Vector of achievement keys that match
     */
    std::vector<std::string> checkScoreAchievements(int64_t score, int64_t userId) const;

    /**
     * @brief Increment counter-based achievement progress locally.
     * @param key Achievement key
     * @param userId User ID
     * @param increment Amount to increment (default 1)
     * @return true if achievement was newly unlocked
     */
    bool incrementProgress(const std::string &key, int64_t userId, int increment = 1);

    // ---- DMD Frame Cache (Internal - scorbitd only) ----

    /**
     * @brief Store a DMD frame for an achievement.
     * @param key Achievement key
     * @param frame Binary frame data
     */
    void setDmdFrame(const std::string &key, DmdFrame frame);

    /**
     * @brief Check if DMD frame is cached.
     */
    bool hasDmdFrame(const std::string &key) const;

    /**
     * @brief Get cached DMD frame.
     * @return Frame data if cached, empty vector otherwise
     */
    DmdFrame getDmdFrame(const std::string &key) const;

    /**
     * @brief Get list of achievement keys that need DMD frame downloads.
     * @return Vector of keys with image URLs but no cached frame
     */
    std::vector<std::pair<std::string, std::string>> getFramesToDownload() const;

    // ---- Callback Registration ----

    /**
     * @brief Set callback for locally-triggered achievements.
     */
    void setTriggeredCallback(AchievementTriggeredCallback callback);

private:
    void notifyTriggered(const std::string &key, int64_t userId, bool isUnlock, int progress);

private:
    mutable std::mutex m_achievementsMutex;
    std::vector<Achievement> m_achievements;
    std::map<std::string, size_t> m_achievementIndex; // key -> index in m_achievements

    mutable std::mutex m_progressMutex;
    std::map<int64_t, std::map<std::string, AchievementProgress>> m_userProgress; // userId -> (key -> progress)

    mutable std::mutex m_framesMutex;
    mutable LRUCache<std::string, DmdFrame> m_framesCache {MAX_DMD_FRAMES_CACHED};

    AchievementTriggeredCallback m_triggeredCallback;
};

} // namespace detail
} // namespace scorbit
