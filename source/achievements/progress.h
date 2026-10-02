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
 * with no stored measurement has no entry in @ref rules; the API's device progress read carries no
 * per-rule values today, so a fetched baseline has only @ref achieved.
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

} // namespace achievements
} // namespace detail
} // namespace scorbit
