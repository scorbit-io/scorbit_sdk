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

#include "calendar.h"
#include <algorithm>
#include <utility>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

using Kind = CalendarCondition::Kind;

bool matchesDate(const CalendarCondition &condition, int64_t day)
{
    const auto civil = civilFromDays(day);
    return std::ranges::any_of(condition.dates, [&civil](const CalendarCondition::Date &date) {
        return date.month == civil.month && date.day == civil.day
            && (!date.year || *date.year == civil.year);
    });
}

/** The local instants of local @p day at which a local-time @p condition holds. */
void appendLocalMatches(const CalendarCondition &condition, int64_t day,
                        std::vector<TimeInterval> &out)
{
    const int64_t midnight = day * MS_PER_DAY;
    switch (condition.kind) {
    case Kind::Weekday:
        if (condition.weekdays.test(static_cast<size_t>(weekdayFromDays(day)))) {
            out.push_back({midnight, midnight + MS_PER_DAY});
        }
        break;

    case Kind::Date:
        if (matchesDate(condition, day)) {
            out.push_back({midnight, midnight + MS_PER_DAY});
        }
        break;

    case Kind::Time: {
        // A range that wraps past midnight is two ranges of the same day, not one into the next
        const int64_t start = midnight + condition.startMinute * MS_PER_MINUTE;
        const int64_t end = midnight + condition.endMinute * MS_PER_MINUTE;
        if (start < end) {
            out.push_back({start, end});
        } else {
            out.push_back({midnight, end});
            out.push_back({start, midnight + MS_PER_DAY});
        }
        break;
    }

    case Kind::Window:
    case Kind::Event:
        break;
    }
}

IntervalSet localMatching(const CalendarCondition &condition, const UtcOffsetTable &offsets,
                          const TimeInterval &span)
{
    std::vector<TimeInterval> utc;
    for (const auto &segment : offsets.segments(span)) {
        const TimeInterval local {segment.utc.startMs + segment.offsetMs,
                                  segment.utc.endMs + segment.offsetMs};

        std::vector<TimeInterval> matches;
        for (auto day = dayOf(local.startMs); day <= dayOf(local.endMs - 1); ++day) {
            appendLocalMatches(condition, day, matches);
        }
        for (const auto &match : matches) {
            if (const auto inside = match.clip(local); !inside.empty()) {
                utc.push_back({inside.startMs - segment.offsetMs, inside.endMs - segment.offsetMs});
            }
        }
    }
    return IntervalSet {std::move(utc)};
}

} // namespace

IntervalSet matching(const CalendarCondition &condition, const CalendarData &data,
                     const TimeInterval &span)
{
    if (span.empty()) {
        return {};
    }

    if (condition.isLocalTime()) {
        return data.offsets ? localMatching(condition, *data.offsets, span) : IntervalSet {};
    }

    const auto it = data.ranges.find(condition.key);
    if (it == data.ranges.end()) {
        return {};
    }
    std::vector<TimeInterval> inside;
    for (const auto &range : it->second) {
        inside.push_back(range.clip(span));
    }
    return IntervalSet {std::move(inside)};
}

// ---- CalendarMatcher ---------------------------------------------------------------------------

CalendarMatcher::CalendarMatcher(std::vector<const CalendarCondition *> conditions,
                                 const CalendarData &data, std::optional<IntervalSet> trusted)
    : m_conditions {std::move(conditions)}
    , m_data {data}
    , m_trusted {std::move(trusted)}
{
}

CalendarAnchor CalendarMatcher::anchor() const
{
    return m_conditions.empty() ? CalendarAnchor::Start : m_conditions.front()->anchor;
}

bool CalendarMatcher::qualifies(const TimeInterval &span) const
{
    auto inside = m_trusted ? m_trusted->intersect(IntervalSet {{span}}) : IntervalSet {{span}};
    for (const auto *condition : m_conditions) {
        inside = inside.intersect(matching(*condition, m_data, span));
        if (inside.empty()) {
            return false;
        }
    }
    return !inside.empty();
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
