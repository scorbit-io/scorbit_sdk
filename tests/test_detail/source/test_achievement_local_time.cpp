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

#include <../source/achievements/local_time.h>
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail::achievements;

namespace {

/** Chicago, as the definitions response example of §10.3 serves it. */
UtcOffsetTable chicago()
{
    auto table = UtcOffsetTable::create({{1767225600000, -21600},   // 2026-01-01, CST
                                         {1772956800000, -18000},   // 2026-03-08 08:00Z, CDT
                                         {1793516400000, -21600},   // 2026-11-01 07:00Z, CST
                                         {1805011200000, -18000},   // 2027-03-14, CDT
                                         {1825570800000, -21600}}); // 2027-11-07, CST
    REQUIRE(table);
    return *table;
}

constexpr int64_t HOUR = 3'600'000;

} // namespace

TEST_CASE("Civil dates convert both ways", "[achievements]")
{
    CHECK(daysFromCivil({1970, 1, 1}) == 0);
    CHECK(daysFromCivil({1969, 12, 31}) == -1);
    CHECK(daysFromCivil({2000, 2, 29}) == 11016);
    CHECK(daysFromCivil({2026, 10, 31}) == 20757);
    CHECK(daysFromCivil({1600, 3, 1}) == -135080);
    CHECK(daysFromCivil({2400, 2, 29}) == 157113);

    // Every day across a thousand years
    bool roundTrips = true;
    for (int64_t days = -200'000; days <= 200'000 && roundTrips; ++days) {
        roundTrips = daysFromCivil(civilFromDays(days)) == days;
    }
    CHECK(roundTrips);
    CHECK(civilFromDays(11016) == CivilDate {2000, 2, 29});
    CHECK(civilFromDays(-1) == CivilDate {1969, 12, 31});
}

TEST_CASE("Days of the week and leap years", "[achievements]")
{
    CHECK(weekdayFromDays(0) == Weekday::Thu);
    CHECK(weekdayFromDays(-1) == Weekday::Wed);
    CHECK(weekdayFromDays(daysFromCivil({2026, 10, 31})) == Weekday::Sat);
    CHECK(weekdayFromDays(daysFromCivil({2000, 2, 29})) == Weekday::Tue);

    CHECK(isLeapYear(2000));
    CHECK(isLeapYear(2028));
    CHECK_FALSE(isLeapYear(1900));
    CHECK_FALSE(isLeapYear(2026));
    CHECK(daysInMonth(2028, 2) == 29);
    CHECK(daysInMonth(2026, 2) == 28);
    CHECK(daysInMonth(2026, 4) == 30);
    CHECK(daysInMonth(2026, 13) == 0);

    CHECK(dayOf(0) == 0);
    CHECK(dayOf(MS_PER_DAY - 1) == 0);
    CHECK(dayOf(-1) == -1);
    CHECK(dayOf(-MS_PER_DAY) == -1);
}

TEST_CASE("The UTC offset is the last change at or before an instant", "[achievements]")
{
    const auto table = chicago();
    const int64_t springForward = 1772956800000;

    CHECK(table.offsetMsAt(springForward - 1) == -21600 * 1000);
    CHECK(table.offsetMsAt(springForward) == -18000 * 1000);
    CHECK(table.offsetMsAt(0) == -21600 * 1000);             // before the table: the first
    CHECK(table.offsetMsAt(1900000000000) == -21600 * 1000); // beyond it: the last
}

TEST_CASE("A span is cut where the offset changes", "[achievements]")
{
    const auto table = chicago();
    const int64_t springForward = 1772956800000;

    const auto segments = table.segments({springForward - HOUR, springForward + HOUR});
    REQUIRE(segments.size() == 2);
    CHECK(segments[0].utc == TimeInterval {springForward - HOUR, springForward});
    CHECK(segments[0].offsetMs == -21600 * 1000);
    CHECK(segments[1].utc == TimeInterval {springForward, springForward + HOUR});
    CHECK(segments[1].offsetMs == -18000 * 1000);

    // 01:59 CST is followed by 03:00 CDT: local 02:xx never happens that day
    CHECK(civilFromDays(dayOf(springForward - 1 + segments[0].offsetMs)) == CivilDate {2026, 3, 8});
    CHECK((springForward + segments[1].offsetMs) % MS_PER_DAY == 3 * HOUR);

    CHECK(table.segments({springForward + HOUR, springForward + 2 * HOUR}).size() == 1);
    CHECK(table.segments({5, 5}).empty());
}

TEST_CASE("An offset table must be ascending and non-empty", "[achievements]")
{
    CHECK_FALSE(UtcOffsetTable::create({}));
    CHECK_FALSE(UtcOffsetTable::create({{2000, 0}, {1000, 3600}}));
    CHECK_FALSE(UtcOffsetTable::create({{1000, 0}, {1000, 3600}}));

    const auto fixed = UtcOffsetTable::create({{1767225600000, 19800}}); // Kolkata, no DST
    REQUIRE(fixed);
    CHECK(fixed->offsetMsAt(0) == 19800 * 1000);
    CHECK(fixed->segments({0, MS_PER_DAY}).size() == 1);
}

TEST_CASE("Interval sets merge and intersect", "[achievements]")
{
    const IntervalSet a {{{30, 40}, {0, 10}, {5, 15}, {15, 20}, {50, 50}}};
    CHECK(a.intervals() == std::vector<TimeInterval> {{0, 20}, {30, 40}});

    const IntervalSet b {{{10, 35}}};
    CHECK(a.intersect(b).intervals() == std::vector<TimeInterval> {{10, 20}, {30, 35}});
    CHECK(a.intersect(IntervalSet {}).empty());
    CHECK(IntervalSet {{{0, 10}}}.intersect(IntervalSet {{{10, 20}}}).empty());
}
