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

#include "rule_evaluator.h"
#include <algorithm>
#include <map>
#include <optional>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

const RuleProgress *baselineEntry(const EvaluationContext &context, size_t index)
{
    if (!context.baseline) {
        return nullptr;
    }
    const auto it = context.baseline->rules.find(index);
    return it == context.baseline->rules.end() ? nullptr : &it->second;
}

/** The observed value an author would call "best" for @p rule: what the predicate looks for. */
int64_t bestValue(const std::vector<int64_t> &values, const Rule &rule)
{
    switch (rule.comparison) {
    case Comparison::Ge:
        return *std::ranges::max_element(values);
    case Comparison::Le:
        return *std::ranges::min_element(values);
    case Comparison::Eq:
    case Comparison::Ne:
        break;
    }
    const auto it = std::ranges::find_if(
            values, [&](int64_t v) { return satisfies(v, rule.comparison, rule.target); });
    return it != values.end() ? *it : values.back();
}

/**
 * Resolves an observation rule from this session's observed @p values, merged for an `unlimited`
 * window with the claim-time best value and verdict.
 */
RuleProgress resolveObservation(std::vector<int64_t> values, const Rule &rule,
                                const RuleProgress *baseline)
{
    bool satisfied = std::ranges::any_of(
            values, [&](int64_t v) { return satisfies(v, rule.comparison, rule.target); });

    if (baseline) {
        values.push_back(baseline->value);
        satisfied = satisfied || baseline->satisfied
                 || satisfies(baseline->value, rule.comparison, rule.target);
    }

    if (values.empty()) {
        // Never happened means zero; a qualifier with nothing to qualify is unsatisfied
        return RuleProgress {0,
                             !isQualifier(rule.type) && satisfies(0, rule.comparison, rule.target)};
    }

    return RuleProgress {bestValue(values, rule), satisfied};
}

RuleProgress resolveAccumulator(int64_t value, const Rule &rule)
{
    return RuleProgress {value, satisfies(value, rule.comparison, rule.target)};
}

/** Evaluates the rules of one achievement; holds what is shared between its rules. */
class AchievementEvaluator
{
public:
    AchievementEvaluator(const Definition &definition, const EvaluationContext &context)
        : m_definition {definition}
        , m_context {context}
        , m_facts {context.facts}
        , m_unlimited {definition.evaluation == EvaluationClass::Unlimited}
    {
        for (const auto &rule : definition.rules) {
            if (rule.type == RuleType::Ball) {
                m_ballRules.push_back(&rule);
            }
        }
    }

    Evaluation evaluate() const
    {
        Evaluation evaluation;
        for (size_t i = 0; i < m_definition.rules.size(); ++i) {
            if (auto progress = evaluateRule(i)) {
                evaluation.rules.emplace(i, *progress);
            }
        }

        evaluation.allSatisfied = evaluation.rules.size() == m_definition.rules.size()
                               && std::ranges::all_of(evaluation.rules, [](const auto &entry) {
                                      return entry.second.satisfied;
                                  });
        return evaluation;
    }

private:
    std::optional<RuleProgress> evaluateRule(size_t index) const
    {
        const auto &rule = m_definition.rules[index];
        const auto *baseline = m_unlimited ? baselineEntry(m_context, index) : nullptr;
        const int64_t carried = baseline ? baseline->value : 0;

        switch (rule.type) {
        case RuleType::Mode:
        case RuleType::ModeCompleted:
        case RuleType::ModeStack:
            return resolveAccumulator(carried + static_cast<int64_t>(occurrences(rule).size()),
                                      rule);

        case RuleType::Event: {
            int64_t total = carried;
            for (const auto &op : occurrences(rule)) {
                total += op.value;
            }
            return resolveAccumulator(total, rule);
        }

        case RuleType::Session:
            // The game in progress counts from the moment play begins (§5.4)
            return resolveAccumulator(carried + 1, rule);

        case RuleType::Achievement:
            if (!m_context.definitions.contains(rule.reference)) {
                return std::nullopt; // Not delivered here: the server decides (§7.7)
            }
            return resolveAccumulator(m_context.held.count(rule.reference) ? 1 : 0, rule);

        case RuleType::Score:
            return resolveObservation(scoreValues(), rule, baseline);

        case RuleType::TimerSession:
            return resolveObservation({m_facts.sessionTimeMs()}, rule, baseline);

        case RuleType::TimerBall:
            return resolveObservation(durationValues(m_facts.ballDurations()), rule, baseline);

        case RuleType::TimerMode:
            return resolveObservation(durationValues(m_facts.activationDurations(rule.reference)),
                                      rule, baseline);

        case RuleType::TimerBetween:
            return resolveObservation(spans(index), rule, baseline);

        case RuleType::Ball:
            return resolveObservation(qualifyingBalls(index), rule, baseline);
        }

        return std::nullopt;
    }

    /** A ball qualifies when it satisfies every `BALL` rule; with none, every ball does. */
    bool qualifies(BallNumber ball) const
    {
        return std::ranges::all_of(m_ballRules, [ball](const Rule *rule) {
            return satisfies(ball, rule->comparison, rule->target);
        });
    }

    /** The occurrences an instant-valued rule counts, restricted to qualifying balls. */
    std::vector<Occurrence> occurrences(const Rule &rule) const
    {
        std::vector<Occurrence> all;
        switch (rule.type) {
        case RuleType::Mode:
            all = m_facts.activations(rule.reference);
            break;
        case RuleType::ModeCompleted:
            all = m_facts.completions(rule.reference);
            break;
        case RuleType::ModeStack:
            all = m_facts.stacks(rule.stackModes);
            break;
        case RuleType::Event:
            all = m_facts.eventOps(rule.reference);
            break;
        default:
            return {};
        }

        std::erase_if(all, [this](const Occurrence &o) { return !qualifies(o.ball); });
        return all;
    }

    /** Sibling rules of a qualifier: every other rule, all instant-valued by construction. */
    std::vector<std::vector<Occurrence>> siblingOccurrences(size_t qualifierIndex) const
    {
        std::vector<std::vector<Occurrence>> rv;
        for (size_t i = 0; i < m_definition.rules.size(); ++i) {
            const auto &rule = m_definition.rules[i];
            if (i != qualifierIndex && isInstantValued(rule.type)) {
                rv.push_back(occurrences(rule));
            }
        }
        return rv;
    }

    std::vector<int64_t> scoreValues() const
    {
        if (m_unlimited) {
            // One value per session, its final score: for the session in progress, the latest
            if (const auto latest = m_facts.latestScore()) {
                return {*latest};
            }
            return {};
        }
        const auto &values = m_facts.scoreValues();
        return {values.begin(), values.end()};
    }

    static std::vector<int64_t> durationValues(const std::vector<Duration> &durations)
    {
        std::vector<int64_t> rv;
        rv.reserve(durations.size());
        for (const auto &d : durations) {
            rv.push_back(d.ms);
        }
        return rv;
    }

    /**
     * `TIMER_BETWEEN`: per ball, the shortest span containing an occurrence of every sibling's
     * fact, in any order. With a single sibling, the gaps between its consecutive occurrences.
     */
    std::vector<int64_t> spans(size_t index) const
    {
        const auto siblings = siblingOccurrences(index);
        if (siblings.empty()) {
            return {};
        }

        // (time, sibling) per ball span
        std::map<size_t, std::vector<std::pair<int64_t, size_t>>> byBall;
        for (size_t s = 0; s < siblings.size(); ++s) {
            for (const auto &o : siblings[s]) {
                byBall[o.span].emplace_back(o.timeMs, s);
            }
        }

        std::vector<int64_t> rv;
        for (auto &[span, events] : byBall) {
            std::ranges::sort(events);

            if (siblings.size() == 1) {
                for (size_t i = 1; i < events.size(); ++i) {
                    rv.push_back(events[i].first - events[i - 1].first);
                }
                continue;
            }

            if (auto shortest = shortestCoveringWindow(events, siblings.size())) {
                rv.push_back(*shortest);
            }
        }
        return rv;
    }

    /** Sliding window over time-sorted @p events: the shortest one covering all @p kinds. */
    static std::optional<int64_t>
    shortestCoveringWindow(const std::vector<std::pair<int64_t, size_t>> &events, size_t kinds)
    {
        std::vector<size_t> inWindow(kinds, 0);
        size_t covered = 0;
        std::optional<int64_t> best;

        for (size_t left = 0, right = 0; right < events.size(); ++right) {
            if (inWindow[events[right].second]++ == 0) {
                ++covered;
            }
            while (covered == kinds) {
                const auto width = events[right].first - events[left].first;
                best = best ? std::min(*best, width) : width;
                if (--inWindow[events[left].second] == 0) {
                    --covered;
                }
                ++left;
            }
        }
        return best;
    }

    /** `BALL`: the ball numbers of the sibling occurrences that qualified. */
    std::vector<int64_t> qualifyingBalls(size_t index) const
    {
        std::vector<int64_t> rv;
        for (const auto &occurrencesOfSibling : siblingOccurrences(index)) {
            for (const auto &o : occurrencesOfSibling) {
                rv.push_back(o.ball);
            }
        }
        return rv;
    }

    const Definition &m_definition;
    const EvaluationContext &m_context;
    const PlayerFacts &m_facts;
    const bool m_unlimited;
    std::vector<const Rule *> m_ballRules;
};

} // namespace

Evaluation evaluate(const Definition &definition, const EvaluationContext &context)
{
    return AchievementEvaluator {definition, context}.evaluate();
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
