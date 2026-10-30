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

#include <../source/achievements/calendar.h>
#include <catch2/catch_test_macros.hpp>
#include <string>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail::achievements;

namespace {

constexpr int64_t HOUR = 3'600'000;
constexpr int64_t MINUTE = 60'000;
constexpr int64_t CDT = -5;
constexpr int64_t CST = -6;

/** The UTC instant of local @p date @p hour:@p minute, at UTC offset @p offsetHours. */
int64_t at(CivilDate date, int64_t hour, int64_t minute, int64_t offsetHours)
{
    return daysFromCivil(date) * MS_PER_DAY + hour * HOUR + minute * MINUTE - offsetHours * HOUR;
}

/** Chicago around Halloween 2026: CDT until 1 November 07:00 UTC, then CST. */
CalendarData chicago()
{
    CalendarData data;
    data.timezone = "America/Chicago";
    data.offsets = UtcOffsetTable::create({{1767225600000, -21600},
                                           {1772956800000, -18000},
                                           {1793516400000, -21600},
                                           {1805011200000, -18000}});
    REQUIRE(data.offsets);
    return data;
}

CalendarCondition parse(const std::string &reference)
{
    auto condition = parseCalendarCondition(reference);
    REQUIRE(condition);
    return *condition;
}

int64_t total(const IntervalSet &set)
{
    int64_t ms = 0;
    for (const auto &interval : set.intervals()) {
        ms += interval.endMs - interval.startMs;
    }
    return ms;
}

} // namespace

TEST_CASE("CALENDAR conditions parse", "[achievements]")
{
    using Kind = CalendarCondition::Kind;

    const auto weekend = parse("weekday=SAT,SUN");
    CHECK(weekend.kind == Kind::Weekday);
    CHECK(weekend.anchor == CalendarAnchor::Start);
    CHECK(weekend.weekdays.test(static_cast<size_t>(Weekday::Sat)));
    CHECK(weekend.weekdays.test(static_cast<size_t>(Weekday::Sun)));
    CHECK(weekend.weekdays.count() == 2);
    CHECK(weekend.isLocalTime());

    const auto midnight = parse("active:time=23:55-00:05");
    CHECK(midnight.kind == Kind::Time);
    CHECK(midnight.anchor == CalendarAnchor::Active);
    CHECK(midnight.startMinute == 23 * 60 + 55);
    CHECK(midnight.endMinute == 5);
    CHECK(midnight.key == "time=23:55-00:05"); // the anchor is not part of the key

    const auto halloween = parse("date=10-31,2026-10-31,02-29");
    REQUIRE(halloween.dates.size() == 3);
    CHECK_FALSE(halloween.dates[0].year);
    CHECK(halloween.dates[1].year == 2026);
    CHECK(halloween.dates[2].day == 29);

    const auto window = parse("window=thanksgiving-week");
    CHECK(window.kind == Kind::Window);
    CHECK(window.slug == "thanksgiving-week");
    CHECK_FALSE(window.isLocalTime());

    CHECK(parse("event=Expo_2026-A").slug == "Expo_2026-A");
}

TEST_CASE("Only canonical CALENDAR conditions parse", "[achievements]")
{
    for (const char *reference : {
                 "",
                 "weekday",
                 "weekday=",
                 "month=10",
                 "weekday=sat",
                 "weekday=SAT, SUN",
                 "weekday=SAT,SAT",
                 "weekday=SAT,",
                 "weekday=SAT,time=10:00-11:00", // one condition per rule
                 "active:active:weekday=SAT",
                 "time=24:00-01:00",
                 "time=10:60-11:00",
                 "time=10:00-10:00",
                 "time=1:00-2:00",
                 "time=10:00",
                 "time=10:00-11:00-12:00",
                 "date=02-30",
                 "date=2027-02-29",
                 "date=0000-01-01",
                 "date=10/31",
                 "date=10-31,10-31",
                 "window=Thanksgiving",
                 "window=thanks_giving",
                 "event=expo 2026",
                 "event=expo.2026",
         }) {
        INFO(reference);
        CHECK_FALSE(parseCalendarCondition(reference));
    }

    CHECK(parseCalendarCondition("date=2028-02-29"));
    CHECK(parseCalendarCondition("date=10-31,2026-10-31")); // distinct entries, not duplicates
}

TEST_CASE("A date matches the venue's whole local day", "[achievements]")
{
    const auto data = chicago();
    const TimeInterval span {at({2026, 10, 30}, 20, 0, CDT), at({2026, 11, 2}, 4, 0, CST)};

    const auto matched = matching(parse("date=10-31"), data, span);
    REQUIRE(matched.intervals().size() == 1);
    CHECK(matched.intervals()[0]
          == TimeInterval {at({2026, 10, 31}, 0, 0, CDT), at({2026, 11, 1}, 0, 0, CDT)});

    CHECK(matching(parse("date=2025-10-31"), data, span).empty());
    CHECK(matching(parse("weekday=SAT"), data, span) == matched); // 31 October 2026 is a Saturday
}

TEST_CASE("A time range past midnight is judged on each instant's own day", "[achievements]")
{
    // Saturday 23:00-24:00 and Saturday 00:00-01:00, not Saturday night into Sunday
    const auto data = chicago();
    const auto saturday = parse("weekday=SAT");
    const auto lateNight = parse("time=23:00-01:00");
    const CalendarMatcher matcher {{&saturday, &lateNight}, data};

    const auto instant = [](int64_t ms) { return TimeInterval {ms, ms + 1}; };
    CHECK(matcher.qualifies(instant(at({2026, 10, 31}, 0, 30, CDT))));
    CHECK(matcher.qualifies(instant(at({2026, 10, 31}, 23, 30, CDT))));
    CHECK_FALSE(matcher.qualifies(instant(at({2026, 10, 30}, 23, 30, CDT)))); // Friday night
    CHECK_FALSE(matcher.qualifies(instant(at({2026, 11, 1}, 0, 30, CST))));   // into Sunday
    CHECK_FALSE(matcher.qualifies(instant(at({2026, 10, 31}, 12, 0, CDT))));
}

TEST_CASE("Daylight saving: a skipped time never matches, a repeated one twice", "[achievements]")
{
    const auto data = chicago();

    // 8 March 2026: 01:59 CST is followed by 03:00 CDT
    const TimeInterval springDay {at({2026, 3, 8}, 0, 0, CST), at({2026, 3, 9}, 0, 0, CDT)};
    CHECK(matching(parse("time=02:00-03:00"), data, springDay).empty());
    CHECK(total(matching(parse("date=03-08"), data, springDay)) == 23 * HOUR);

    // 1 November 2026: 01:00-02:00 happens in CDT and again in CST
    const TimeInterval fallDay {at({2026, 11, 1}, 0, 0, CDT), at({2026, 11, 2}, 0, 0, CST)};
    const auto repeated = matching(parse("time=01:00-02:00"), data, fallDay);
    CHECK(total(repeated) == 2 * HOUR);
    CHECK(repeated.intervals().front().startMs == at({2026, 11, 1}, 1, 0, CDT));
    CHECK(total(matching(parse("date=11-01"), data, fallDay)) == 25 * HOUR);
}

TEST_CASE("Windows and events match their delivered ranges", "[achievements]")
{
    auto data = chicago();
    const TimeInterval expo {at({2026, 11, 6}, 9, 0, CST), at({2026, 11, 8}, 18, 0, CST)};
    data.ranges["event=expo-2026"] = {expo};
    data.ranges["window=quiet"] = {};

    const TimeInterval weekend {at({2026, 11, 7}, 0, 0, CST), at({2026, 11, 9}, 0, 0, CST)};
    const auto matched = matching(parse("active:event=expo-2026"), data, weekend);
    REQUIRE(matched.intervals().size() == 1);
    CHECK(matched.intervals()[0] == TimeInterval {weekend.startMs, expo.endMs});

    CHECK(matching(parse("window=quiet"), data, weekend).empty());
    CHECK(matching(parse("window=never-delivered"), data, weekend).empty());
}

TEST_CASE("Without UTC offsets only windows and events match", "[achievements]")
{
    CalendarData data;
    data.ranges["window=fall-back"] = {{0, 10 * MS_PER_DAY}};
    const TimeInterval span {0, MS_PER_DAY};

    CHECK(matching(parse("weekday=THU"), data, span).empty());
    CHECK(matching(parse("time=00:00-23:59"), data, span).empty());
    CHECK(matching(parse("date=01-01"), data, span).empty());
    CHECK(matching(parse("window=fall-back"), data, span) == IntervalSet {{span}});
}

TEST_CASE("The active anchor judges any instant of an occurrence", "[achievements]")
{
    // A multiball from 23:58 to 00:03 was running at midnight, though no row fell inside it
    const auto data = chicago();
    const auto midnight = parse("active:time=00:00-00:01");
    const CalendarMatcher matcher {{&midnight}, data};
    CHECK(matcher.anchor() == CalendarAnchor::Active);

    const auto start = at({2026, 10, 30}, 23, 58, CDT);
    const auto end = at({2026, 10, 31}, 0, 3, CDT);
    CHECK(matcher.qualifies({start, end}));
    CHECK_FALSE(matcher.qualifies({start, start + 1}));                     // when it started
    CHECK_FALSE(matcher.qualifies({start, at({2026, 10, 31}, 0, 0, CDT)})); // ended at midnight
}
