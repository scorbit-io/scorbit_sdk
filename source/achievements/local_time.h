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

#include "time_interval.h"
#include <cstdint>
#include <optional>
#include <vector>

/**
 * The venue's wall clock, without a time zone database (§5.5.2): the API delivers the venue's UTC
 * offsets as a table, and civil dates are computed here with the proleptic Gregorian calendar.
 */
namespace scorbit {
namespace detail {
namespace achievements {

constexpr int64_t MS_PER_MINUTE = 60'000;
constexpr int64_t MS_PER_DAY = 86'400'000;

/** Days of the week, Monday first. */
enum class Weekday { Mon, Tue, Wed, Thu, Fri, Sat, Sun };

struct CivilDate {
    int64_t year {1970};
    unsigned month {1}; ///< 1–12
    unsigned day {1};   ///< 1–31

    bool operator==(const CivilDate &) const = default;
};

bool isLeapYear(int64_t year);
unsigned daysInMonth(int64_t year, unsigned month);

/** Days since 1970-01-01 of @p date, negative before it. */
int64_t daysFromCivil(const CivilDate &date);

/** The date @p days after 1970-01-01. */
CivilDate civilFromDays(int64_t days);

/** The day of the week @p days after 1970-01-01, a Thursday. */
Weekday weekdayFromDays(int64_t days);

/** The day number that @p ms falls on, counting days from the epoch on the same clock. */
int64_t dayOf(int64_t ms);

/**
 * A venue's UTC offset over time: each change gives the offset in force from that instant on
 * (§10.3, `utc_offsets`). An instant before the first change takes the first offset, and one
 * beyond the last change the last offset.
 */
class UtcOffsetTable
{
public:
    struct Change {
        int64_t fromMs {0};
        int64_t offsetSeconds {0};

        bool operator==(const Change &) const = default;
    };

    /** A span of UTC instants over which the offset does not change. */
    struct Segment {
        TimeInterval utc;
        int64_t offsetMs {0};
    };

    /** @return std::nullopt unless @p changes is non-empty and strictly ascending. */
    static std::optional<UtcOffsetTable> create(std::vector<Change> changes);

    /** The offset in force at @p utcMs, in milliseconds to add to it for local time. */
    int64_t offsetMsAt(int64_t utcMs) const;

    /** @p utc cut where the offset changes, in order. */
    std::vector<Segment> segments(const TimeInterval &utc) const;

    const std::vector<Change> &changes() const { return m_changes; }

private:
    explicit UtcOffsetTable(std::vector<Change> changes);

    std::vector<Change> m_changes;
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
