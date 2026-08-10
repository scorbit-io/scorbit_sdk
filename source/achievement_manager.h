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
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {

/// Maximum number of DMD frames held in the LRU cache.
constexpr auto MAX_DMD_FRAMES_CACHED = 32;

/// DMD frame binary data (PNG or raw).
using DmdFrame = std::vector<uint8_t>;

/**
 * @brief Callback for achievement progress recorded or unlocked by local evaluation.
 *
 * @param key Achievement key
 * @param userId User the event applies to - the public user UUID (the "id" exposed everywhere
 *               else in the v2 API), not an internal integer id
 * @param isUnlock true if this call crossed the unlock threshold, false for a progress update
 * @param progress Accumulated counter value after the update
 *
 * Invoked with **no AchievementManager mutex held**, so it is safe to call back into the manager
 * from here. Return promptly regardless - this runs on the caller's thread.
 */
using AchievementTriggeredCallback =
        std::function<void(const std::string &key, const std::string &userId, bool isUnlock, int progress)>;

/**
 * @brief Apply a rule's comparison operator to a live value.
 *
 * Rules carry their own operator, and the SDK must agree with the server's strict semantics:
 * `"<"` is `value < target`, `"="` is `value == target`, and anything else - including the
 * documented default `">"` and an empty string - is a **strict** `value > target`.
 *
 * In particular `">"` is not `>=`: a rule of `score > 1000000` is not satisfied at exactly
 * 1,000,000.
 */
bool satisfies(int64_t value, const std::string &comparison, int64_t target);

/**
 * @brief On-device achievement cache and local rule matcher.
 *
 * Holds three independently-locked caches - achievement definitions, per-user progress, and DMD
 * frames - and evaluates cached definitions against live game state on demand.
 *
 * @section am_predictive Predictive only
 *
 * Nothing here unlocks anything. The server is the authority; a local match means "this looks like
 * it qualifies, ask the server". Callers should post the unlock and wait for the server's
 * `AchievementUnlocked` event before presenting anything to the player.
 *
 * @section am_conservative Conservative matching
 *
 * Every match path reports an achievement only when all of the following hold:
 *  - at least one rule was evaluable in the calling context, and
 *  - every evaluable rule was satisfied, and
 *  - no rule that the context could *not* judge was encountered.
 *
 * `"PROGRESS"` and `"ACHIEVEMENT"` rules are the sole exception: they are owned by the server (and,
 * for `"PROGRESS"`, by incrementProgress()) and are skipped everywhere without blocking a match.
 * Every other rule type the context cannot judge - including a `"SCORE"` rule when no score was
 * supplied, a mode rule in a score-only context, and the unimplemented `"GAME_CODE"` / `"TIMER"` /
 * `"EVENT"` / `"ATTEMPT"` types - withholds the match rather than being treated as satisfied.
 * Since rules are ANDed, treating an unjudged rule as passing would report achievements the player
 * has not earned.
 *
 * @section am_scope Global-scoped achievements are never local
 *
 * An achievement's engine is derived from its rule types, with one exception: `scope ==
 * "global"` is always server-evaluated, regardless of what rules it carries. All three
 * check*() entry points skip a `"global"`-scoped achievement outright, the same way they skip
 * an already-unlocked one.
 *
 * @section am_norules Achievements with no rules
 *
 * An achievement with an empty rule list never matches, mirroring the server, which refuses to
 * auto-unlock a rule-less achievement.
 *
 * @section am_threads Thread safety
 *
 * All public methods are safe to call concurrently. User callbacks are always invoked with no mutex
 * held; see the mutex members for the acquisition order.
 */
class AchievementManager
{
public:
    AchievementManager() = default;

    // ---- Achievement definition cache ----

    /** Replace the cached definitions wholesale, as returned by a fetch. */
    void setAchievements(std::vector<Achievement> achievements);

    /** True if any definitions are cached. */
    bool hasAchievements() const;

    /** Copy of every cached definition. */
    std::vector<Achievement> getAchievements() const;

    /** Copy of one cached definition, or nullopt if the key is unknown. */
    std::optional<Achievement> getAchievement(const std::string &key) const;

    /** Drop all cached definitions. */
    void clearAchievements();

    // ---- Per-user progress cache ----

    /** Replace one user's cached progress wholesale, as returned by a fetch. */
    void setUserProgress(const std::string &userId, std::vector<AchievementProgress> progress);

    /** Copy of one user's cached progress, or nullopt if nothing is cached for that user. */
    std::optional<std::vector<AchievementProgress>> getUserProgress(const std::string &userId) const;

    /** Copy of one progress entry, or nullopt if absent. */
    std::optional<AchievementProgress> getProgress(const std::string &userId, const std::string &key) const;

    /** Set one progress entry outright, e.g. on receiving a server progress event. */
    void updateProgress(const std::string &userId, const std::string &key, int progress, bool unlocked);

    /**
     * @brief Drop one user's accumulated progress.
     *
     * Call this when a player leaves the session. Session-scoped achievements
     * (AchievementInputTime::Limited) are re-checked after they unlock, so if their counters are
     * not reset the progress from earlier sessions carries forward and they unlock early - a
     * `Limited` counter would behave as a lifetime one.
     */
    void clearUserProgress(const std::string &userId);

    /**
     * @brief Drop every user's accumulated progress.
     *
     * Call this on game end or session teardown, for the same reason as clearUserProgress().
     */
    void clearAllProgress();

    // ---- Local matching ----

    /**
     * @brief Achievements matched by a mode event, with no score available.
     *
     * Conservative: see @ref am_conservative. Because no score is supplied, a `"SCORE"` rule is
     * *unevaluable* here rather than being compared against zero, so an achievement combining mode
     * and score conditions will never match through this entry point. Use
     * checkModeAchievementsWithScore() for those.
     *
     * @param modeName The mode that started, completed, or stacked.
     * @param modeType One of `"start"`, `"complete"`, `"stack"`.
     * @param userId User whose cached progress gates re-checking; see isAlreadyUnlocked().
     * @return Keys of matched achievements.
     */
    std::vector<std::string> checkModeAchievements(const std::string &modeName,
                                                  const std::string &modeType,
                                                  const std::string &userId) const;

    /**
     * @brief Achievements matched by a mode event, judging score rules against @p score.
     *
     * Conservative: see @ref am_conservative. Identical to checkModeAchievements() except that
     * `"SCORE"` rules become evaluable. Use this for any achievement that mixes mode and score
     * rules.
     */
    std::vector<std::string> checkModeAchievementsWithScore(const std::string &modeName,
                                                            const std::string &modeType,
                                                            const std::string &userId, int64_t score) const;

    /**
     * @brief Achievements matched by the current score alone.
     *
     * Conservative: see @ref am_conservative. A mode rule cannot be judged without a mode event, so
     * an achievement combining a mode rule with a `"SCORE"` rule is **not** reported here even once
     * the score threshold is crossed - it is reported by checkModeAchievementsWithScore() when the
     * mode event arrives. Only achievements carrying at least one `"SCORE"` rule are ever returned.
     */
    std::vector<std::string> checkScoreAchievements(int64_t score, const std::string &userId) const;

    /**
     * @brief Add to a counter achievement's locally accumulated progress.
     *
     * The unlock threshold is taken from the achievement's `"PROGRESS"` rule `target` and applied
     * with that rule's `comparison` operator. This is deliberate and worth stating plainly: the
     * platform has **no achievement-level counter threshold**. Achievement::ballCount is a
     * "complete before ball N" qualifier that nothing evaluates, and reading it as a threshold
     * makes every counter achievement unlock on its first increment. It must never drive an unlock
     * decision.
     *
     * If no matching `"PROGRESS"` rule can be located, the progress is still recorded and the
     * callback still fires, but false is returned - the manager does not guess a threshold, and
     * the unlock decision is left to the server.
     *
     * The triggered callback is invoked after the progress mutex is released, so it may call back
     * into the manager.
     *
     * @param key Achievement key whose counter is being bumped.
     * @param userId User the progress belongs to.
     * @param increment Amount to add.
     * @param metricKey The `"PROGRESS"` rule `reference` identifying which counter this is. Leave
     *                  empty when the achievement has exactly one `"PROGRESS"` rule; with several,
     *                  an empty key is ambiguous and no threshold is applied.
     * @return true only if this call crossed the threshold of a located `"PROGRESS"` rule.
     */
    bool incrementProgress(const std::string &key, const std::string &userId, int increment = 1,
                           const std::string &metricKey = {});

    // ---- DMD frame cache (internal, for scorbitd) ----

    /** Cache a DMD frame, evicting the least recently used once at capacity. */
    void setDmdFrame(const std::string &key, DmdFrame frame);

    /** True if a frame is cached for @p key. */
    bool hasDmdFrame(const std::string &key) const;

    /** Cached frame for @p key, or an empty vector if absent. */
    DmdFrame getDmdFrame(const std::string &key) const;

    /** (key, imageUrl) for every cached definition that has an image but no cached frame. */
    std::vector<std::pair<std::string, std::string>> getFramesToDownload() const;

    // ---- Callback registration ----

    /** Set (or clear, by passing nullptr) the local progress/unlock callback. */
    void setTriggeredCallback(AchievementTriggeredCallback callback);

private:
    /**
     * @brief Whether @p ach should be skipped as already earned.
     *
     * True only when the achievement is unlocked **and** lifetime
     * (AchievementInputTime::Unlimited). Session-scoped (`Limited`) achievements stay eligible so
     * they can be earned again in a later session.
     *
     * Must be called with m_progressMutex held.
     */
    bool isAlreadyUnlocked(const Achievement &ach, const std::string &userId) const;

    /**
     * @brief Evaluate one achievement's rules against a mode event and an optional score.
     *
     * @param score The live score, or nullopt when the caller has none - in which case `"SCORE"`
     *              rules are unevaluable and withhold the match.
     */
    bool evaluateForMode(const Achievement &ach, const std::string &modeName,
                         const std::string &modeType, std::optional<int64_t> score) const;

    /** Evaluate one achievement's rules against a score, with no mode event available. */
    bool evaluateForScore(const Achievement &ach, int64_t score) const;

    /** Copy the callback out, then invoke it with no mutex held. */
    void notifyTriggered(const std::string &key, const std::string &userId, bool isUnlock, int progress) const;

private:
    // Mutex acquisition order, where more than one is needed:
    //     m_achievementsMutex -> m_progressMutex -> m_framesMutex -> m_callbackMutex
    // Never acquire them in any other order, and never invoke a caller-supplied callback while
    // holding any of them - copy what the callback needs out of the locked section, release, then
    // call. A callback that reads progress back out of the manager must not deadlock.

    mutable std::mutex m_achievementsMutex;
    std::vector<Achievement> m_achievements;
    std::map<std::string, size_t> m_achievementIndex; // key -> index into m_achievements

    mutable std::mutex m_progressMutex;
    // userId (UUID string) -> (achievement key -> progress)
    std::map<std::string, std::map<std::string, AchievementProgress>> m_userProgress;

    mutable std::mutex m_framesMutex;
    mutable LRUCache<std::string, DmdFrame> m_framesCache {MAX_DMD_FRAMES_CACHED};

    mutable std::mutex m_callbackMutex;
    AchievementTriggeredCallback m_triggeredCallback;
};

} // namespace detail
} // namespace scorbit
