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

/**
 * @file achievements.h
 * @brief Scorbit SDK Achievements Module - C++ API
 *
 * @section achievements_overview Overview
 *
 * The Achievements module provides functionality for:
 * - Fetching achievement definitions for the current machine
 * - Tracking user progress towards achievements
 * - Unlocking/locking achievements via REST API
 * - Local caching and matching for real-time achievement detection
 *
 * @section achievements_types Achievement Types
 *
 * **By Persistence (InputTime):**
 * - **Limited**: Progress resets each game session (session-scoped)
 * - **Unlimited**: Progress persists across sessions (lifetime achievements)
 *
 * **By Trigger:**
 * - **Game**: Triggered by general game events (SDK handles via unlock API)
 * - **Mode**: Triggered when specific game modes start/complete/stack
 * - **Score**: Triggered when score reaches a threshold
 * - **SubAchievement**: Triggered when other achievements are earned
 *
 * **Special Types:**
 * - **Trophy**: Only one user can hold at a time (previous holder loses it)
 * - **Obscured**: Details hidden until earned (adds mystery)
 *
 * @section achievements_usage Basic Usage
 *
 * @code{.cpp}
 * // 1. Fetch achievements for this machine (call once at startup or when needed)
 * gameState.fetchAchievements([](Error error, std::vector<Achievement> achievements) {
 *     if (error == Error::Success) {
 *         // Achievements are now cached locally for matching
 *         std::cout << "Loaded " << achievements.size() << " achievements\n";
 *     }
 * });
 *
 * // 2. Fetch user progress when player claims a slot
 * gameState.fetchAchievementProgress(userId, [](Error error, std::vector<AchievementProgress> progress) {
 *     if (error == Error::Success) {
 *         // Progress is now cached for local matching
 *     }
 * });
 *
 * // 3. Check for mode-based achievements when modes change
 * auto matched = gameState.checkModeAchievements("Multiball", "start", userId);
 * for (const auto& key : matched) {
 *     gameState.unlockAchievement(userId, key, 1, [](Error e, AchievementUnlockResult r) {
 *         // Achievement unlocked on server
 *     });
 * }
 *
 * // 4. Check for score-based achievements when score updates
 * auto scoreMatched = gameState.checkScoreAchievements(currentScore, userId);
 * // ... unlock matched achievements
 *
 * // 5. For counter-based achievements, increment progress locally
 * bool newlyUnlocked = gameState.incrementProgress("ramp_shots", userId, 1);
 * if (newlyUnlocked) {
 *     // Sync to server
 *     gameState.unlockAchievement(userId, "ramp_shots", 1, callback);
 * }
 * @endcode
 *
 * @section achievements_realtime Real-time Events
 *
 * Achievement events (unlocks by other players, progress updates) are delivered
 * via WebSocket using the Centrifugo protocol. These events arrive through the
 * SDK's event system - see @ref event.h for details.
 *
 * @section achievements_caching Caching Behavior
 *
 * - Achievement definitions are cached after `fetchAchievements()` succeeds
 * - User progress is cached per-user after `fetchAchievementProgress()` succeeds
 * - Local matching uses cached data (no network calls)
 * - Cache is cleared on disconnect; re-fetch on reconnect
 *
 * @see achievements_c.h for the C API
 */

#pragma once

#include <scorbit_sdk/achievements_c.h>
#include <scorbit_sdk/net_types.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace scorbit {

/**
 * @brief Input time type for achievements.
 *
 * Determines whether achievement progress is tracked per-session or lifetime.
 */
enum class AchievementInputTime {
    Limited = SB_ACH_INPUT_LIMITED,     ///< Single-session achievement
    Unlimited = SB_ACH_INPUT_UNLIMITED, ///< Lifetime achievement (progress persists)
};

/**
 * @brief Trigger type for achievements.
 */
enum class AchievementTrigger {
    Game = SB_ACH_TRIGGER_GAME,                   ///< Triggered by game events
    Mode = SB_ACH_TRIGGER_MODE,                   ///< Triggered by mode start/complete
    Score = SB_ACH_TRIGGER_SCORE,                 ///< Triggered by score threshold
    SubAchievement = SB_ACH_TRIGGER_SUBACHIEVEMENT, ///< Triggered by other achievements
};

/**
 * @brief Mode type for mode-based achievements.
 */
enum class AchievementModeType {
    None = SB_ACH_MODE_NONE,         ///< Not a mode-based achievement
    Start = SB_ACH_MODE_START,       ///< Triggered when mode starts
    Complete = SB_ACH_MODE_COMPLETE, ///< Triggered when mode completes
    Stack = SB_ACH_MODE_STACK,       ///< Triggered when modes are stacked
};

/**
 * @brief Achievement definition from the server.
 *
 * Contains all the metadata about an achievement that can be earned.
 */
struct Achievement {
    std::string key;           ///< Unique achievement key
    std::string name;          ///< Display name
    std::string description;   ///< How to earn the achievement
    int count;                 ///< Counter threshold to unlock
    std::string imageUrl;      ///< Image URL when achievement is visible/earned
    std::string obscureImageUrl; ///< Image URL when achievement is hidden
    bool obscure;              ///< Whether to hide details until earned
    bool visible;              ///< Whether achievement is visible in lists
    bool isTrophy;             ///< Whether this is a trophy (only one holder at a time)
    bool notifyWhenAchieved;   ///< Whether to notify followers when earned
    AchievementInputTime inputTime; ///< Limited (session) or unlimited (lifetime)
    AchievementTrigger trigger;     ///< What triggers this achievement
    AchievementModeType modeType;   ///< Mode type for mode-based achievements
    std::string modeName;      ///< Mode name (for mode-based triggers)
    int64_t targetScore;       ///< Target score (for score-based triggers)
    int groupId;               ///< Group ID for leveled achievements
    int achievementId;         ///< Achievement ID within group
};

/**
 * @brief User's progress towards an achievement.
 */
struct AchievementProgress {
    std::string key;           ///< Achievement key
    int progress;              ///< Current progress count
    bool unlocked;             ///< Whether the achievement is unlocked
    std::string unlockedAt;    ///< ISO timestamp when unlocked (empty if not unlocked)
};

/**
 * @brief Result of an unlock request.
 */
struct AchievementUnlockResult {
    std::string key;           ///< Achievement key
    bool success;              ///< Whether the unlock was successful
    bool newlyUnlocked;        ///< Whether this was a new unlock (vs already unlocked)
    std::string message;       ///< Optional message from server
};

/**
 * @brief Callback for fetching achievements.
 *
 * @param error Error code (Error::Success on success)
 * @param achievements Vector of achievement definitions
 */
using AchievementsCallback = std::function<void(Error error, std::vector<Achievement> achievements)>;

/**
 * @brief Callback for fetching user achievement progress.
 *
 * @param error Error code (Error::Success on success)
 * @param progress Vector of user progress for each achievement
 */
using AchievementProgressCallback =
        std::function<void(Error error, std::vector<AchievementProgress> progress)>;

/**
 * @brief Callback for unlock/lock operations.
 *
 * @param error Error code (Error::Success on success)
 * @param result Result of the unlock operation
 */
using AchievementUnlockCallback =
        std::function<void(Error error, AchievementUnlockResult result)>;

} // namespace scorbit
