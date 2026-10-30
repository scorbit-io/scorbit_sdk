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

#include <algorithm>
#include <cstdint>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** A span of UTC epoch milliseconds, start inclusive and end exclusive. */
struct TimeInterval {
    int64_t startMs {0};
    int64_t endMs {0};

    bool empty() const { return endMs <= startMs; }
    bool contains(int64_t ms) const { return startMs <= ms && ms < endMs; }

    /** The part of this interval inside @p other; empty if they do not overlap. */
    TimeInterval clip(const TimeInterval &other) const
    {
        return {std::max(startMs, other.startMs), std::min(endMs, other.endMs)};
    }

    bool operator==(const TimeInterval &) const = default;
};

/** A set of instants, kept as sorted, disjoint, non-empty intervals. */
class IntervalSet
{
public:
    IntervalSet() = default;

    /** Any intervals, in any order; overlapping and touching ones are merged. */
    explicit IntervalSet(std::vector<TimeInterval> intervals)
    {
        std::erase_if(intervals, [](const TimeInterval &i) { return i.empty(); });
        std::ranges::sort(intervals, {}, &TimeInterval::startMs);
        for (const auto &interval : intervals) {
            if (!m_intervals.empty() && interval.startMs <= m_intervals.back().endMs) {
                m_intervals.back().endMs = std::max(m_intervals.back().endMs, interval.endMs);
            } else {
                m_intervals.push_back(interval);
            }
        }
    }

    bool empty() const { return m_intervals.empty(); }
    const std::vector<TimeInterval> &intervals() const { return m_intervals; }

    /** The instants in both sets. */
    IntervalSet intersect(const IntervalSet &other) const
    {
        IntervalSet rv;
        for (size_t i = 0, j = 0; i < m_intervals.size() && j < other.m_intervals.size();) {
            const auto &a = m_intervals[i];
            const auto &b = other.m_intervals[j];
            if (const auto both = a.clip(b); !both.empty()) {
                rv.m_intervals.push_back(both);
            }
            (a.endMs < b.endMs ? i : j)++;
        }
        return rv;
    }

    bool operator==(const IntervalSet &) const = default;

private:
    std::vector<TimeInterval> m_intervals;
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
