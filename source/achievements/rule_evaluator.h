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

#include "definition.h"
#include "player_facts.h"
#include "progress.h"
#include <string>
#include <unordered_set>

namespace scorbit {
namespace detail {
namespace achievements {

using HeldSet = std::unordered_set<std::string>;

/** Everything one achievement is evaluated against, for one player. */
struct EvaluationContext {
    /** This session's facts for the player. */
    const PlayerFacts &facts;

    /** The player's claim-time state for this achievement; nullptr if never started. */
    const Baseline *baseline {nullptr};

    /** Keys the player holds: claim-time achieved ones plus those decided this session. */
    const HeldSet &held;

    /** The definitions delivered to this machine; decides which prerequisites can be judged. */
    const DefinitionSet &definitions;
};

/** The machine's measurement of one achievement. */
struct Evaluation {
    /**
     * One entry per rule the machine could judge, keyed by rule index. A rule it cannot judge —
     * an `ACHIEVEMENT` rule whose prerequisite was never delivered — has no entry (§10.5).
     */
    RuleProgressMap rules;

    /** Every rule has an entry and every entry is satisfied (rules are ANDed, §5). */
    bool allSatisfied {false};
};

/**
 * Evaluates @p definition against @p context, implementing the rule semantics of §5:
 *
 * - accumulators resolve to one `int64` — this session's count for `in_session`, and the
 *   claim-time baseline plus this session's delta for `unlimited` (`SESSION` counts the game in
 *   progress, so it is baseline + 1);
 * - observation rules resolve to the set of values observed and are satisfied when one of them
 *   satisfies the predicate; the reported value is the best one for the predicate;
 * - a fact that never happened is `0`, with the predicate applied to it (§5.4.1), except for the
 *   qualifiers `TIMER_BETWEEN` and `BALL`, which are unsatisfied when nothing qualifies (§5.5);
 * - `BALL` filters: sibling accumulators and `TIMER_BETWEEN` spans only consider occurrences on
 *   a ball that satisfies every `BALL` rule of the achievement;
 * - everything but `TIMER_SESSION` is confined to a single ball.
 */
Evaluation evaluate(const Definition &definition, const EvaluationContext &context);

} // namespace achievements
} // namespace detail
} // namespace scorbit
