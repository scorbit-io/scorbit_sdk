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
 * @brief Scorbit SDK Achievements Module - C++ types
 *
 * @section achievements_overview Overview
 *
 * An achievement is a badge attached to a user profile, earned by playing pinball. Definitions and
 * unlock state are stored centrally on the Scorbit platform; evaluation is distributed.
 *
 * @section achievements_engines Two engines, one authority
 *
 * Which engine decides a given achievement is derived from its rule types - there is no flag on the
 * record that selects it:
 *
 * | Rule types present                                        | Evaluated where          |
 * |-----------------------------------------------------------|--------------------------|
 * | Only `MODE` / `MODE_START` / `MODE_STACK` / `SCORE`        | On the machine, in-session |
 * | Any `PROGRESS` or `ACHIEVEMENT` rule, or scope `global`    | Server, after the session uploads |
 * | `GAME_CODE`                                               | Neither - game code unlocks explicitly |
 *
 * The on-machine engine is **predictive only**. It exists so the machine can light a "you're close"
 * indicator and pre-load artwork. Never show "unlocked" off the back of a local match - wait for
 * the server's `AchievementUnlocked` event.
 *
 * An achievement's rules are **always ANDed**. There is no OR, in either engine.
 *
 * @section achievements_comparison Comparison operators
 *
 * Each rule carries its own operator - `">"`, `"<"` or `"="`, defaulting to `">"`. `">"` is a
 * **strict** greater-than, matching the server, so a rule of `score > 1000000` is not satisfied
 * *at* exactly 1,000,000.
 *
 * @section achievements_counters Counter achievements
 *
 * A counter achievement's unlock threshold lives on its `PROGRESS` rule's `target`. There is no
 * achievement-level counter threshold anywhere in the platform - in particular
 * @ref Achievement::ballCount is a "complete before ball N" qualifier and must never be used to
 * decide an unlock.
 *
 * @section achievements_cpp_facade C++ facade
 *
 * The public `scorbit::GameState` class exposes the whole achievements API in terms of the types
 * below - see `GameState::fetchAchievements`, `unlockAchievement`, `checkModeAchievements`,
 * `incrementAchievementProgress` and friends. Each method thin-wraps its `sb_*` counterpart in
 * @ref achievements_c.h, so the two APIs are interchangeable; C and C++ integrators get the same
 * semantics. These are also the types the SDK's internal
 * `scorbit::detail::AchievementManager` caches and matches against.
 *
 * @section achievements_realtime Real-time events
 *
 * Unlock, lock and progress events arrive over the SDK's event system on the per-session
 * Centrifugo channel. Deduplicate on key + userId: after a reconnect you may receive events for
 * unlocks that happened while disconnected. Both a progress event at 100% and an unlock event will
 * arrive for the same achievement - show one celebration, not two.
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
 * @brief Whether achievement progress is session-scoped or lifetime.
 *
 * Mirrors the server's `is_single_session` flag.
 */
enum class AchievementInputTime {
    /// Single-session achievement; progress is expected to be reset when the session ends.
    Limited = SB_ACH_INPUT_LIMITED,
    /// Lifetime achievement; progress persists across sessions.
    Unlimited = SB_ACH_INPUT_UNLIMITED,
};

/**
 * @brief Achievement trigger type.
 *
 * Derived from `rules[0]` only, and therefore lossy for multi-rule achievements. Read
 * @ref Achievement::rules rather than trusting this field.
 */
enum class AchievementTrigger {
    Game = SB_ACH_TRIGGER_GAME,                     ///< Triggered by game events
    Mode = SB_ACH_TRIGGER_MODE,                     ///< Triggered by mode start/complete/stack
    Score = SB_ACH_TRIGGER_SCORE,                   ///< Triggered by a score threshold
    SubAchievement = SB_ACH_TRIGGER_SUBACHIEVEMENT, ///< Triggered by another achievement
};

/**
 * @brief Mode type for mode-based achievements.
 *
 * Derived from `rules[0]` only, and therefore lossy for multi-rule achievements.
 */
enum class AchievementModeType {
    None = SB_ACH_MODE_NONE,         ///< Not a mode-based achievement
    Start = SB_ACH_MODE_START,       ///< Triggered when the mode starts
    Complete = SB_ACH_MODE_COMPLETE, ///< Triggered when the mode completes
    Stack = SB_ACH_MODE_STACK,       ///< Triggered when modes are stacked
};

/**
 * @brief A single rule within an achievement definition.
 *
 * An achievement's rules are always ANDed; there is no combinator field.
 */
struct AchievementRule {
    /**
     * Rule type. One of `"MODE"`, `"MODE_START"`, `"MODE_STACK"`, `"SCORE"`, `"PROGRESS"`,
     * `"ACHIEVEMENT"`, `"GAME_CODE"`, `"TIMER"`, `"EVENT"`, `"ATTEMPT"`.
     */
    std::string type;

    /**
     * Comparison operator: `">"`, `"<"` or `"="`; `">"` is the default and is a **strict**
     * greater-than.
     */
    std::string comparison;

    /**
     * Target value the live value is compared against.
     *
     * `int64_t` because a `"SCORE"` rule's target is a pinball score, and scores routinely run
     * past 2,147,483,647. The server's `Rule.target` is still a 32-bit
     * `PositiveIntegerField`, so today nothing above that ceiling reaches the device - the wider
     * type is here so that when the API widens the field the SDK does not silently truncate, and
     * so that locally seeded definitions can express a real score target.
     */
    int64_t target {0};

    /**
     * Context-dependent reference: the mode name for `"MODE"` / `"MODE_START"` / `"MODE_STACK"`,
     * the metric key for `"PROGRESS"`, otherwise unused.
     */
    std::string reference;

    /// Sub-achievement FK id for `"ACHIEVEMENT"` rules; 0 otherwise.
    int subachievementId {0};
};

/**
 * @brief Achievement definition as delivered by the server.
 */
struct Achievement {
    std::string key;             ///< Unique, immutable achievement key
    std::string name;            ///< Display name
    std::string description;     ///< How to earn the achievement

    /**
     * What this achievement attaches to: `"game"`, `"venue"`, `"event"`, or `"global"`.
     *
     * `"global"` achievements are **always** server-evaluated, regardless of which rule types
     * they carry - unlike every other scope, where routing is derived purely from rule types.
     * @ref scorbit::detail::AchievementManager never locally matches a `"global"`-scoped
     * achievement even if its rules are otherwise all `MODE`/`SCORE`.
     */
    std::string scope;

    std::string imageUrl;        ///< Image URL shown when visible/earned
    std::string obscureImageUrl; ///< Placeholder image URL shown while still hidden
    bool obscure {false};        ///< Listed, but details replaced by a placeholder until earned
    bool visible {true};         ///< Whether the achievement appears in player-facing lists
    bool isTrophy {false};       ///< Trophy: held by exactly one player at a time
    bool notifyWhenAchieved {false}; ///< Whether followers are notified when earned

    /// Session-scoped or lifetime; mirrors the server's `is_single_session`.
    AchievementInputTime inputTime {AchievementInputTime::Unlimited};

    /// Derived from `rules[0]` only - lossy for multi-rule achievements.
    AchievementTrigger trigger {AchievementTrigger::Game};

    /// Derived from `rules[0]` only - lossy for multi-rule achievements.
    AchievementModeType modeType {AchievementModeType::None};

    /// Derived from `rules[0]` only - lossy for multi-rule achievements.
    std::string modeName;

    /// Derived from `rules[0]` only - lossy for multi-rule achievements.
    int64_t targetScore {0};

    /// Group id for tier ladders. Groups are display-only and carry no evaluation semantics.
    int groupId {0};

    /// Sparse level within the group, ordering the tier ladder.
    int level {0};

    /**
     * The server's `ball_count`: a "complete this before ball N" qualifier.
     *
     * This is **not** a counter threshold, and nothing evaluates it in either engine today. A
     * counter achievement's threshold lives on its `"PROGRESS"` rule's `target`. Never use this
     * field to decide whether an achievement is unlocked.
     */
    int ballCount {0};

    /// The rules, ANDed. Authoritative - prefer these over the derived flat fields above.
    std::vector<AchievementRule> rules;
};

/**
 * @brief A user's progress towards one achievement.
 */
struct AchievementProgress {
    std::string key;         ///< Achievement key
    int progress {0};        ///< Accumulated counter value
    bool unlocked {false};   ///< Whether the achievement is currently held
    std::string unlockedAt;  ///< ISO timestamp when unlocked; empty if not unlocked
};

/**
 * @brief Result of an unlock or lock request.
 */
struct AchievementUnlockResult {
    std::string key;            ///< Achievement key
    bool success {false};       ///< Whether the request succeeded
    bool newlyUnlocked {false}; ///< Whether this call was the one that unlocked it
    std::string message;        ///< Optional message from the server
};

/**
 * @brief Callback delivering fetched achievement definitions.
 *
 * @param error Error::Success on success
 * @param achievements The definitions, empty on failure
 */
using AchievementsCallback =
        std::function<void(Error error, std::vector<Achievement> achievements)>;

/**
 * @brief Callback delivering a user's fetched achievement progress.
 *
 * @param error Error::Success on success
 * @param progress The progress entries, empty on failure
 */
using AchievementProgressCallback =
        std::function<void(Error error, std::vector<AchievementProgress> progress)>;

/**
 * @brief Callback delivering the result of an unlock or lock request.
 *
 * @param error Error::Success on success
 * @param result The result of the operation
 */
using AchievementUnlockCallback = std::function<void(Error error, AchievementUnlockResult result)>;

/**
 * @brief Callback invoked when local matching records progress or a local unlock.
 *
 * The C++ counterpart of @ref sb_achievement_triggered_callback_t.
 *
 * @param key Achievement key
 * @param userId User the event applies to - the public user UUID, not an internal integer id
 * @param isUnlock true if this call crossed the unlock threshold, false for a progress update
 * @param progress Accumulated counter value after the update
 *
 * The SDK holds no internal lock while this runs, so calling back into the achievement API from
 * here is safe. Still return promptly - queue artwork and animation rather than blocking.
 */
using AchievementTriggeredCallback = std::function<void(
        const std::string &key, const std::string &userId, bool isUnlock, int progress)>;

} // namespace scorbit
