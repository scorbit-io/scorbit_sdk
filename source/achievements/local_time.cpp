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

#include "local_time.h"
#include <limits>
#include <utility>

namespace scorbit {
namespace detail {
namespace achievements {

// The civil calendar conversions are Howard Hinnant's days_from_civil / civil_from_days
// (http://howardhinnant.github.io/date_algorithms.html): exact for every proleptic Gregorian
// date, in eras of 400 years that start on 1 March.

bool isLeapYear(int64_t year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

unsigned daysInMonth(int64_t year, unsigned month)
{
    constexpr unsigned DAYS[] {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    return month == 2 && isLeapYear(year) ? 29 : DAYS[month - 1];
}

int64_t daysFromCivil(const CivilDate &date)
{
    const int64_t month = date.month;
    const int64_t year = date.year - (month <= 2 ? 1 : 0);
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yearOfEra = year - era * 400;
    const int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + date.day - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

CivilDate civilFromDays(int64_t days)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t dayOfEra = days - era * 146097;
    const int64_t yearOfEra =
            (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t shiftedMonth = (5 * dayOfYear + 2) / 153;
    const auto day = static_cast<unsigned>(dayOfYear - (153 * shiftedMonth + 2) / 5 + 1);
    const auto month =
            static_cast<unsigned>(shiftedMonth < 10 ? shiftedMonth + 3 : shiftedMonth - 9);
    return {yearOfEra + era * 400 + (month <= 2 ? 1 : 0), month, day};
}

Weekday weekdayFromDays(int64_t days)
{
    constexpr int64_t THURSDAY = 3;
    return static_cast<Weekday>(((days % 7) + 7 + THURSDAY) % 7);
}

int64_t dayOf(int64_t ms)
{
    return ms >= 0 ? ms / MS_PER_DAY : -((-ms + MS_PER_DAY - 1) / MS_PER_DAY);
}

// ---- UtcOffsetTable ----------------------------------------------------------------------------

UtcOffsetTable::UtcOffsetTable(std::vector<Change> changes)
    : m_changes {std::move(changes)}
{
}

std::optional<UtcOffsetTable> UtcOffsetTable::create(std::vector<Change> changes)
{
    if (changes.empty()) {
        return std::nullopt;
    }
    for (size_t i = 1; i < changes.size(); ++i) {
        if (changes[i].fromMs <= changes[i - 1].fromMs) {
            return std::nullopt;
        }
    }
    return UtcOffsetTable {std::move(changes)};
}

int64_t UtcOffsetTable::offsetMsAt(int64_t utcMs) const
{
    auto offset = m_changes.front().offsetSeconds;
    for (const auto &change : m_changes) {
        if (change.fromMs > utcMs) {
            break;
        }
        offset = change.offsetSeconds;
    }
    return offset * 1000;
}

std::vector<UtcOffsetTable::Segment> UtcOffsetTable::segments(const TimeInterval &utc) const
{
    constexpr auto MIN = std::numeric_limits<int64_t>::min();
    constexpr auto MAX = std::numeric_limits<int64_t>::max();

    std::vector<Segment> rv;
    for (size_t i = 0; i < m_changes.size(); ++i) {
        const TimeInterval inForce {i == 0 ? MIN : m_changes[i].fromMs,
                                    i + 1 < m_changes.size() ? m_changes[i + 1].fromMs : MAX};
        if (const auto piece = utc.clip(inForce); !piece.empty()) {
            rv.push_back({piece, m_changes[i].offsetSeconds * 1000});
        }
    }
    return rv;
}

int64_t nextLocalMidnight(const std::optional<UtcOffsetTable> &offsets, int64_t utcMs)
{
    const auto offsetAt = [&offsets](int64_t ms) { return offsets ? offsets->offsetMsAt(ms) : 0; };

    const int64_t midnight = (dayOf(utcMs + offsetAt(utcMs)) + 1) * MS_PER_DAY;
    // The offset in force at midnight decides, should it change before then
    const int64_t guess = midnight - offsetAt(utcMs);
    return midnight - offsetAt(guess);
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
