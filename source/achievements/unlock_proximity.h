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
#include "progress.h"

namespace scorbit {
namespace detail {
namespace achievements {

/**
 * How close a player is to unlocking an achievement, so its icon can be fetched before the unlock
 * and handed over with it at once.
 *
 * Each rule scores from 0 to 1: a satisfied rule 1, an unsatisfied `>=` rule with a target above 1
 * its share of the target, and any other unsatisfied rule 0. That other rule is yes-or-no (e.g. a
 * mode completed): how close it is can't be told. A rule not judged yet scores 0.
 */
struct UnlockProximity {
    /** The mean of the rules' scores, to rank achievements by. */
    double score {0.0};

    /**
     * Whether the unlock looks imminent: every unsatisfied rule that has a share has at least
     * @ref NEAR_SHARE of its target, and at most one yes-or-no rule remains, unless it is the only
     * rule (nothing then tells it is close).
     */
    bool near {false};

    static constexpr double NEAR_SHARE = 0.8;
};

/** How close @p rules, a player's progress, are to unlocking @p definition. */
UnlockProximity unlockProximity(const Definition &definition, const RuleProgressMap &rules);

} // namespace achievements
} // namespace detail
} // namespace scorbit
