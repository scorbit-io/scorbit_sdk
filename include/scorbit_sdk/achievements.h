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

#include "achievements_c.h"

#include <cstdint>
#include <string>
#include <vector>

namespace scorbit {

/** @brief What happened to an achievement; see @ref sb_achievement_status_t. */
enum class AchievementStatus {
    Progress = SB_ACHIEVEMENT_PROGRESS,
    UnlockedLocally = SB_ACHIEVEMENT_UNLOCKED_LOCALLY,
    Confirmed = SB_ACHIEVEMENT_CONFIRMED,
    AlreadyHeld = SB_ACHIEVEMENT_ALREADY_HELD,
    Retracted = SB_ACHIEVEMENT_RETRACTED,
};

enum class AchievementRuleType {
    Mode = SB_ACHIEVEMENT_RULE_MODE,
    ModeCompleted = SB_ACHIEVEMENT_RULE_MODE_COMPLETED,
    ModeStack = SB_ACHIEVEMENT_RULE_MODE_STACK,
    Score = SB_ACHIEVEMENT_RULE_SCORE,
    Event = SB_ACHIEVEMENT_RULE_EVENT,
    Session = SB_ACHIEVEMENT_RULE_SESSION,
    TimerSession = SB_ACHIEVEMENT_RULE_TIMER_SESSION,
    TimerBall = SB_ACHIEVEMENT_RULE_TIMER_BALL,
    TimerMode = SB_ACHIEVEMENT_RULE_TIMER_MODE,
    TimerBetween = SB_ACHIEVEMENT_RULE_TIMER_BETWEEN,
    Ball = SB_ACHIEVEMENT_RULE_BALL,
    Achievement = SB_ACHIEVEMENT_RULE_ACHIEVEMENT,
};

enum class AchievementComparison {
    Eq = SB_ACHIEVEMENT_EQ,
    Le = SB_ACHIEVEMENT_LE,
    Ge = SB_ACHIEVEMENT_GE,
    Ne = SB_ACHIEVEMENT_NE,
};

enum class AchievementEvaluation {
    InSession = SB_ACHIEVEMENT_IN_SESSION,
    Unlimited = SB_ACHIEVEMENT_UNLIMITED,
};

enum class AchievementScope {
    Game = SB_ACHIEVEMENT_SCOPE_GAME,
    Venue = SB_ACHIEVEMENT_SCOPE_VENUE,
    Event = SB_ACHIEVEMENT_SCOPE_EVENT,
    Global = SB_ACHIEVEMENT_SCOPE_GLOBAL,
};

/** @brief One rule of an achievement; all rules must be satisfied together. */
struct AchievementRule {
    AchievementRuleType type {AchievementRuleType::Mode};
    AchievementComparison comparison {AchievementComparison::Ge};
    int64_t target {0};
    std::string reference;
};

/** @brief An achievement definition, with its full rule set. */
struct Achievement {
    std::string key;
    std::string name;
    std::string description;
    AchievementScope scope {AchievementScope::Game};
    AchievementEvaluation evaluation {AchievementEvaluation::InSession};
    bool isTrophy {false};
    bool visible {true};
    bool obscure {false};
    bool notifyWhenAchieved {false};
    std::string iconUrl;
    std::string obscureImageUrl;
    bool hasGroup {false};
    int64_t groupId {0};
    int64_t displayPosition {0};
    std::vector<AchievementRule> rules;
};

/** @brief A claimed player's progress on one rule. */
struct AchievementRuleProgress {
    bool judged {false};
    int64_t value {0};
    int64_t target {0};
    bool satisfied {false};
};

/** @brief A claimed player's progress on one achievement during the current game. */
struct AchievementProgress {
    bool held {false};
    bool allSatisfied {false};
    bool confirmed {false};
    std::vector<AchievementRuleProgress> rules; ///< One entry per rule, in definition order
};

/** @brief The content of an @ref EventType::AchievementUpdated event. */
struct AchievementUpdate {
    struct Rule {
        size_t index {0};
        int64_t value {0};
        bool satisfied {false};
    };

    std::string key;
    sb_player_t player {0};
    std::string userId;
    AchievementStatus status {AchievementStatus::Progress};
    std::vector<Rule> rules; ///< Only the rules the machine could judge
};

namespace achievements_detail {

inline Achievement fromC(sb_game_handle_t handle, const sb_achievement_t &c)
{
    Achievement a;
    a.key = c.key ? c.key : "";
    a.name = c.name ? c.name : "";
    a.description = c.description ? c.description : "";
    a.scope = static_cast<AchievementScope>(c.scope);
    a.evaluation = static_cast<AchievementEvaluation>(c.evaluation);
    a.isTrophy = c.is_trophy;
    a.visible = c.visible;
    a.obscure = c.obscure;
    a.notifyWhenAchieved = c.notify_when_achieved;
    a.iconUrl = c.icon_url ? c.icon_url : "";
    a.obscureImageUrl = c.obscure_image_url ? c.obscure_image_url : "";
    a.hasGroup = c.has_group;
    a.groupId = c.group_id;
    a.displayPosition = c.display_position;

    // Copied before reading the rules: the next call may invalidate the definition's strings
    const std::string key = a.key;
    const size_t count = c.rules_count;
    for (size_t i = 0; i < count; ++i) {
        sb_achievement_rule_t rule;
        if (sb_achievement_rule_at(handle, key.c_str(), i, &rule)) {
            AchievementRule r;
            r.type = static_cast<AchievementRuleType>(rule.type);
            r.comparison = static_cast<AchievementComparison>(rule.comparison);
            r.target = rule.target;
            r.reference = rule.reference ? rule.reference : "";
            a.rules.push_back(std::move(r));
        }
    }
    return a;
}

} // namespace achievements_detail

} // namespace scorbit
