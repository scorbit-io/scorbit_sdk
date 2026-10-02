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

#include "definition.h"
#include <array>
#include <utility>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

constexpr std::array<std::pair<RuleType, std::string_view>, 12> RULE_TYPE_NAMES {{
        {RuleType::Mode, "MODE"},
        {RuleType::ModeCompleted, "MODE_COMPLETED"},
        {RuleType::ModeStack, "MODE_STACK"},
        {RuleType::Score, "SCORE"},
        {RuleType::Event, "EVENT"},
        {RuleType::Session, "SESSION"},
        {RuleType::TimerSession, "TIMER_SESSION"},
        {RuleType::TimerBall, "TIMER_BALL"},
        {RuleType::TimerMode, "TIMER_MODE"},
        {RuleType::TimerBetween, "TIMER_BETWEEN"},
        {RuleType::Ball, "BALL"},
        {RuleType::Achievement, "ACHIEVEMENT"},
}};

constexpr std::array<std::pair<Comparison, std::string_view>, 4> COMPARISON_NAMES {{
        {Comparison::Eq, "EQ"},
        {Comparison::Le, "LE"},
        {Comparison::Ge, "GE"},
        {Comparison::Ne, "NE"},
}};

template<typename Enum, size_t N>
std::optional<Enum> lookup(const std::array<std::pair<Enum, std::string_view>, N> &table,
                           std::string_view str)
{
    for (const auto &[value, name] : table) {
        if (name == str) {
            return value;
        }
    }
    return std::nullopt;
}

template<typename Enum, size_t N>
std::string_view lookup(const std::array<std::pair<Enum, std::string_view>, N> &table, Enum value)
{
    for (const auto &[v, name] : table) {
        if (v == value) {
            return name;
        }
    }
    return {};
}

} // namespace

DefinitionSet::DefinitionSet(std::vector<Definition> definitions)
    : m_definitions {std::move(definitions)}
{
    m_index.reserve(m_definitions.size());
    for (size_t i = 0; i < m_definitions.size(); ++i) {
        m_index.emplace(m_definitions[i].key, i);
    }
}

const Definition *DefinitionSet::find(std::string_view key) const
{
    const auto it = m_index.find(std::string {key});
    return it == m_index.end() ? nullptr : &m_definitions[it->second];
}

bool satisfies(int64_t value, Comparison comparison, int64_t target)
{
    switch (comparison) {
    case Comparison::Eq:
        return value == target;
    case Comparison::Le:
        return value <= target;
    case Comparison::Ge:
        return value >= target;
    case Comparison::Ne:
        return value != target;
    }
    return false;
}

bool isObservation(RuleType type)
{
    switch (type) {
    case RuleType::Score:
    case RuleType::TimerSession:
    case RuleType::TimerBall:
    case RuleType::TimerMode:
    case RuleType::TimerBetween:
    case RuleType::Ball:
        return true;
    default:
        return false;
    }
}

bool isQualifier(RuleType type)
{
    return type == RuleType::TimerBetween || type == RuleType::Ball;
}

bool isInstantValued(RuleType type)
{
    switch (type) {
    case RuleType::Mode:
    case RuleType::ModeCompleted:
    case RuleType::ModeStack:
    case RuleType::Event:
        return true;
    default:
        return false;
    }
}

bool isRankingRule(RuleType type)
{
    switch (type) {
    case RuleType::Score:
    case RuleType::TimerSession:
    case RuleType::TimerBall:
    case RuleType::TimerMode:
    case RuleType::TimerBetween:
        return true;
    default:
        return false;
    }
}

bool isSingleModeRule(RuleType type)
{
    return type == RuleType::Mode || type == RuleType::ModeCompleted || type == RuleType::TimerMode;
}

std::optional<RuleType> ruleTypeFromString(std::string_view str)
{
    return lookup(RULE_TYPE_NAMES, str);
}

std::string_view toString(RuleType type)
{
    return lookup(RULE_TYPE_NAMES, type);
}

std::optional<Comparison> comparisonFromString(std::string_view str)
{
    return lookup(COMPARISON_NAMES, str);
}

std::string_view toString(Comparison comparison)
{
    return lookup(COMPARISON_NAMES, comparison);
}

std::optional<EvaluationClass> evaluationClassFromString(std::string_view str)
{
    if (str == "in_session") {
        return EvaluationClass::InSession;
    }
    if (str == "unlimited") {
        return EvaluationClass::Unlimited;
    }
    return std::nullopt;
}

std::optional<Scope> scopeFromString(std::string_view str)
{
    if (str == "game") {
        return Scope::Game;
    }
    if (str == "venue") {
        return Scope::Venue;
    }
    if (str == "event") {
        return Scope::Event;
    }
    if (str == "global") {
        return Scope::Global;
    }
    return std::nullopt;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
