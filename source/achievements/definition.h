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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/**
 * Achievement definitions: the rule set an achievement is earned by, as delivered by the API
 * (Achievements Platform Contract v1, §3, §5, §10.2). Pure data, no behaviour beyond predicates.
 */
namespace scorbit {
namespace detail {
namespace achievements {

/** The twelve rule types (§5.1). */
enum class RuleType {
    Mode,          ///< Times the referenced mode was activated
    ModeCompleted, ///< Times the referenced mode was completed
    ModeStack,     ///< Times the referenced modes became simultaneously active
    Score,         ///< The player's score
    Event,         ///< The value of the referenced event register
    Session,       ///< Sessions played within the achievement's scope
    TimerSession,  ///< Milliseconds the player spent at the machine in one session
    TimerBall,     ///< Milliseconds one ball lasted
    TimerMode,     ///< Milliseconds one activation of the referenced mode lasted
    TimerBetween,  ///< Milliseconds to bring the sibling rules' facts together within one ball
    Ball,          ///< The ball number on which the sibling rules' facts occurred
    Achievement,   ///< Whether the referenced prerequisite achievement is held
};

/** The four inclusive predicates (§6.1). */
enum class Comparison { Eq, Le, Ge, Ne };

/** The measurement window (§3.4). */
enum class EvaluationClass { InSession, Unlimited };

/** What the achievement attaches to (§3.2). */
enum class Scope { Game, Venue, Event, Global };

struct Rule {
    RuleType type {RuleType::Mode};
    Comparison comparison {Comparison::Ge};
    int64_t target {0};

    /** What the rule measures (§5.3): mode or event name, stack list, or prerequisite key. */
    std::string reference;

    /** For @ref RuleType::ModeStack, the parsed, de-duplicated mode names of @ref reference. */
    std::vector<std::string> stackModes;
};

struct Definition {
    std::string key;
    std::string name;
    std::string description;
    Scope scope {Scope::Game};
    EvaluationClass evaluation {EvaluationClass::InSession};
    bool isTrophy {false};
    bool visible {true};
    bool obscure {false};
    bool notifyWhenAchieved {false};
    std::string iconUrl;
    std::string obscureImageUrl;
    std::optional<int64_t> groupId;
    std::optional<int64_t> level;
    std::optional<int64_t> displayPosition;

    /** In definition order; the order fixes each rule's `index` (§10.2). */
    std::vector<Rule> rules;
};

/** The definitions delivered to this machine (§10.3), searchable by key. */
class DefinitionSet
{
public:
    DefinitionSet() = default;
    DefinitionSet(std::string game, int64_t framesVersion, std::vector<Definition> definitions);

    const std::string &game() const { return m_game; }
    int64_t framesVersion() const { return m_framesVersion; }

    const std::vector<Definition> &all() const { return m_definitions; }
    bool empty() const { return m_definitions.empty(); }
    size_t size() const { return m_definitions.size(); }

    /** The definition with @p key, or nullptr if it was not delivered. */
    const Definition *find(std::string_view key) const;
    bool contains(std::string_view key) const { return find(key) != nullptr; }

private:
    std::string m_game;
    int64_t m_framesVersion {0};
    std::vector<Definition> m_definitions;
    std::unordered_map<std::string, size_t> m_index;
};

/** Applies @p comparison to @p value against @p target (§6.1; all predicates inclusive). */
bool satisfies(int64_t value, Comparison comparison, int64_t target);

/** Observation rules resolve to a set of observed values (§5.4). */
bool isObservation(RuleType type);

/** Qualifiers constrain their sibling rules rather than measuring anything (§5.5). */
bool isQualifier(RuleType type);

/** Accumulators whose occurrences happen at an identifiable instant in a session (§6.4). */
bool isInstantValued(RuleType type);

/** Rules a trophy may be ranked by: `SCORE` and the timers (§7.5). */
bool isRankingRule(RuleType type);

/** Rule types whose `reference` names a single mode. */
bool isSingleModeRule(RuleType type);

std::optional<RuleType> ruleTypeFromString(std::string_view str);
std::string_view toString(RuleType type);

std::optional<Comparison> comparisonFromString(std::string_view str);
std::string_view toString(Comparison comparison);

std::optional<EvaluationClass> evaluationClassFromString(std::string_view str);
std::optional<Scope> scopeFromString(std::string_view str);

} // namespace achievements
} // namespace detail
} // namespace scorbit
