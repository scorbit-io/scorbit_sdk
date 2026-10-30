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

#include "calendar_condition.h"
#include "local_time.h"
#include "time_interval.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** What the definitions response delivers for judging `CALENDAR` rules (§10.3). */
struct CalendarData {
    /** The venue's IANA time zone, for logs only: local time comes from @ref offsets. */
    std::optional<std::string> timezone;

    /** The venue's UTC offsets; absent when the venue has no time zone. */
    std::optional<UtcOffsetTable> offsets;

    /** `window=` and `event=` conditions, by @ref CalendarCondition::key, resolved to UTC. */
    std::map<std::string, std::vector<TimeInterval>> ranges;
};

/**
 * The instants of @p span at which @p condition holds.
 *
 * - `weekday`, `time` and `date` are judged on each instant's own local date and time, from
 *   @ref CalendarData::offsets: a local time the clock skips never matches, and one it repeats
 *   matches on both passes. Without offsets they match nothing.
 * - `window` and `event` match their delivered ranges; one that was not delivered, or has no
 *   ranges, matches nothing.
 */
IntervalSet matching(const CalendarCondition &condition, const CalendarData &data,
                     const TimeInterval &span);

/** The `CALENDAR` rules of one achievement, judged together (§5.5.2). */
class CalendarMatcher
{
public:
    /**
     * Judges @p conditions with @p data. When @p trusted is given, only those instants can be
     * inside a window: the machine's clock vouched for nothing else (§5.5.2).
     */
    CalendarMatcher(std::vector<const CalendarCondition *> conditions, const CalendarData &data,
                    std::optional<IntervalSet> trusted = std::nullopt);

    /** Whether the achievement has `CALENDAR` rules at all. */
    bool empty() const { return m_conditions.empty(); }

    /** The anchor its `CALENDAR` rules share. */
    CalendarAnchor anchor() const;

    /** Whether some instant of @p span lies inside every window at once. */
    bool qualifies(const TimeInterval &span) const;

private:
    std::vector<const CalendarCondition *> m_conditions;
    const CalendarData &m_data;
    std::optional<IntervalSet> m_trusted;
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
