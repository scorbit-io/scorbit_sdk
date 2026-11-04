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
#include <limits>
#include <map>
#include <string>
#include <unordered_map>

namespace scorbit {
namespace detail {
namespace achievements {

/** One rule's measurement and verdict — a `rule_progress` entry (§10.2). */
struct RuleProgress {
    int64_t value {0};
    bool satisfied {false};
};

inline bool operator==(const RuleProgress &lhs, const RuleProgress &rhs)
{
    return lhs.value == rhs.value && lhs.satisfied == rhs.satisfied;
}

/** Rule progress keyed by rule index. A missing index means "not reported / cannot judge". */
using RuleProgressMap = std::map<size_t, RuleProgress>;

/**
 * A player's stored state for one achievement, as fetched when the player claims a slot. A rule
 * with no stored measurement (never reported, or a response without `rule_progress`) has no entry
 * in @ref rules.
 */
struct Baseline {
    bool achieved {false};
    RuleProgressMap rules;
};

/**
 * The claim-time state fetch, keyed by achievement key. It is sparse: an achievement absent from
 * it has never been started and all its rule values are zero (§7.7).
 */
using Baselines = std::unordered_map<std::string, Baseline>;

/**
 * @p lhs + @p rhs, held at the `int64` bounds rather than overflowing: a stored lifetime value may
 * be anywhere in the range (§10.2), and an accumulator adds this session's play to it.
 */
inline int64_t addSaturating(int64_t lhs, int64_t rhs)
{
    if (rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) {
        return std::numeric_limits<int64_t>::max();
    }
    if (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs) {
        return std::numeric_limits<int64_t>::min();
    }
    return lhs + rhs;
}

/** @p lhs - @p rhs, held at the `int64` bounds. */
inline int64_t subtractSaturating(int64_t lhs, int64_t rhs)
{
    if (rhs == std::numeric_limits<int64_t>::min()) {
        return lhs >= 0 ? std::numeric_limits<int64_t>::max() : lhs - rhs;
    }
    return addSaturating(lhs, -rhs);
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
