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

#include "definition_check.h"
#include "../event_ops.h"
#include <fmt/format.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

std::optional<std::string> findRuleProblem(const Definition &definition, size_t index)
{
    const auto &rule = definition.rules[index];

    if (isSingleModeRule(rule.type) || rule.type == RuleType::Event) {
        if (!isValidTimelineName(rule.reference)) {
            return fmt::format("rule {} ({}) has invalid reference '{}'", index,
                               toString(rule.type), rule.reference);
        }
    }

    if (rule.type == RuleType::ModeStack) {
        if (rule.stackModes.size() < 2) {
            return fmt::format("rule {} (MODE_STACK) must name two or more modes, got '{}'", index,
                               rule.reference);
        }
        for (const auto &mode : rule.stackModes) {
            if (!isValidTimelineName(mode)) {
                return fmt::format("rule {} (MODE_STACK) has invalid mode name '{}'", index, mode);
            }
        }
    }

    // The API names which score in the reference; blank and "score" are the total score, the
    // only one the timeline carries (e.g. "ball_score" is not evaluable here)
    if (rule.type == RuleType::Score && !rule.reference.empty() && rule.reference != "score") {
        return fmt::format("rule {} (SCORE) has unsupported reference '{}'", index, rule.reference);
    }

    if (rule.type == RuleType::Achievement && rule.reference.empty()) {
        return fmt::format("rule {} (ACHIEVEMENT) has no prerequisite key", index);
    }

    // A prerequisite rule asks that the prerequisite is held, nothing else (§6.4)
    if (rule.type == RuleType::Achievement
        && ((rule.comparison != Comparison::Ge && rule.comparison != Comparison::Eq)
            || rule.target != 1)) {
        return fmt::format("rule {} (ACHIEVEMENT) must be GE 1 or EQ 1, got {} {}", index,
                           toString(rule.comparison), rule.target);
    }

    if (isQualifier(rule.type)) {
        if (definition.rules.size() < 2) {
            return fmt::format("rule {} ({}) needs at least one sibling rule", index,
                               toString(rule.type));
        }
        for (size_t i = 0; i < definition.rules.size(); ++i) {
            if (i != index && !isInstantValued(definition.rules[i].type)) {
                return fmt::format("rule {} ({}) has a sibling that is not instant-valued: "
                                   "rule {} ({})",
                                   index, toString(rule.type), i,
                                   toString(definition.rules[i].type));
            }
        }
    }

    return std::nullopt;
}

} // namespace

std::optional<std::string> findEvaluationProblem(const Definition &definition)
{
    if (definition.key.empty()) {
        return "achievement has no key";
    }

    if (definition.rules.empty()) {
        return "achievement has no rules";
    }

    for (size_t i = 0; i < definition.rules.size(); ++i) {
        if (auto problem = findRuleProblem(definition, i)) {
            return problem;
        }
    }

    return std::nullopt;
}

std::vector<std::string> removeCyclicChains(std::vector<Definition> &definitions)
{
    std::unordered_map<std::string, const Definition *> byKey;
    for (const auto &definition : definitions) {
        byKey.emplace(definition.key, &definition);
    }

    // Depth-first search; a key is "bad" when it is on, or reaches, a cycle.
    enum class Mark { Visiting, Good, Bad };
    std::unordered_map<std::string, Mark> marks;

    auto visit = [&](auto &&self, const std::string &key) -> bool {
        if (const auto it = marks.find(key); it != marks.end()) {
            return it->second != Mark::Good; // Visiting means we closed a cycle
        }

        const auto defIt = byKey.find(key);
        if (defIt == byKey.end()) {
            return false; // Not delivered: not part of any local cycle
        }

        marks[key] = Mark::Visiting;
        bool bad = false;
        for (const auto &rule : defIt->second->rules) {
            if (rule.type == RuleType::Achievement && self(self, rule.reference)) {
                bad = true;
            }
        }
        marks[key] = bad ? Mark::Bad : Mark::Good;
        return bad;
    };

    std::vector<std::string> removed;
    for (const auto &definition : definitions) {
        if (visit(visit, definition.key)) {
            removed.push_back(definition.key);
        }
    }

    const std::unordered_set<std::string> removedSet(removed.begin(), removed.end());
    std::erase_if(definitions,
                  [&](const Definition &definition) { return removedSet.count(definition.key); });

    return removed;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
