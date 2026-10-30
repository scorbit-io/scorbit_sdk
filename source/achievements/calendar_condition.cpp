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

#include "calendar_condition.h"
#include "local_time.h"
#include <algorithm>
#include <array>
#include <set>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

constexpr std::string_view ACTIVE_PREFIX = "active:";

/** In @ref Weekday order. */
constexpr std::array<std::string_view, 7> WEEKDAY_NAMES {"MON", "TUE", "WED", "THU",
                                                         "FRI", "SAT", "SUN"};

/** A yearless date is real when it exists in a leap year, so `02-29` is one. */
constexpr int64_t LEAP_YEAR = 2000;

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

/** @p text as a number when it is exactly @p width digits. */
std::optional<unsigned> digits(std::string_view text, size_t width)
{
    if (text.size() != width || !std::ranges::all_of(text, isDigit)) {
        return std::nullopt;
    }
    unsigned rv = 0;
    for (const char c : text) {
        rv = rv * 10 + static_cast<unsigned>(c - '0');
    }
    return rv;
}

/** The comma-separated items of @p value; nullopt if one is empty or repeated. */
std::optional<std::vector<std::string_view>> items(std::string_view value)
{
    std::vector<std::string_view> rv;
    std::set<std::string_view> seen;
    for (size_t start = 0;;) {
        const auto comma = value.find(',', start);
        const auto item =
                value.substr(start, comma == std::string_view::npos ? comma : comma - start);
        if (item.empty() || !seen.insert(item).second) {
            return std::nullopt;
        }
        rv.push_back(item);
        if (comma == std::string_view::npos) {
            return rv;
        }
        start = comma + 1;
    }
}

/** `HH:MM`, 24-hour, as minutes after midnight. */
std::optional<unsigned> minuteOfDay(std::string_view text)
{
    if (text.size() != 5 || text[2] != ':') {
        return std::nullopt;
    }
    const auto hours = digits(text.substr(0, 2), 2);
    const auto minutes = digits(text.substr(3, 2), 2);
    if (!hours || !minutes || *hours > 23 || *minutes > 59) {
        return std::nullopt;
    }
    return *hours * 60 + *minutes;
}

/** `MM-DD` or `YYYY-MM-DD`, a real calendar date. */
std::optional<CalendarCondition::Date> date(std::string_view text)
{
    CalendarCondition::Date rv;
    if (text.size() == 10) {
        const auto year = digits(text.substr(0, 4), 4);
        if (!year || text[4] != '-' || *year < 1) {
            return std::nullopt;
        }
        rv.year = *year;
        text.remove_prefix(5);
    }

    const auto month =
            text.size() == 5 && text[2] == '-' ? digits(text.substr(0, 2), 2) : std::nullopt;
    const auto day = month ? digits(text.substr(3, 2), 2) : std::nullopt;
    if (!month || !day || *day < 1 || *day > daysInMonth(rv.year.value_or(LEAP_YEAR), *month)) {
        return std::nullopt;
    }
    rv.month = *month;
    rv.day = *day;
    return rv;
}

bool isSlug(std::string_view text, bool eventSlug)
{
    const auto allowed = [eventSlug](char c) {
        return (c >= 'a' && c <= 'z') || isDigit(c) || c == '-'
            || (eventSlug && ((c >= 'A' && c <= 'Z') || c == '_'));
    };
    return !text.empty() && std::ranges::all_of(text, allowed);
}

bool parseValue(CalendarCondition &condition, std::string_view value)
{
    using Kind = CalendarCondition::Kind;

    switch (condition.kind) {
    case Kind::Weekday: {
        const auto days = items(value);
        if (!days) {
            return false;
        }
        for (const auto day : *days) {
            const auto it = std::ranges::find(WEEKDAY_NAMES, day);
            if (it == WEEKDAY_NAMES.end()) {
                return false;
            }
            condition.weekdays.set(static_cast<size_t>(it - WEEKDAY_NAMES.begin()));
        }
        return true;
    }

    case Kind::Time: {
        const auto dash = value.find('-');
        const auto start = minuteOfDay(value.substr(0, dash));
        const auto end =
                dash == std::string_view::npos ? std::nullopt : minuteOfDay(value.substr(dash + 1));
        if (!start || !end || *start == *end) {
            return false;
        }
        condition.startMinute = *start;
        condition.endMinute = *end;
        return true;
    }

    case Kind::Date: {
        const auto dates = items(value);
        if (!dates) {
            return false;
        }
        for (const auto text : *dates) {
            const auto parsed = date(text);
            if (!parsed) {
                return false;
            }
            condition.dates.push_back(*parsed);
        }
        return true;
    }

    case Kind::Window:
    case Kind::Event:
        if (!isSlug(value, condition.kind == Kind::Event)) {
            return false;
        }
        condition.slug = value;
        return true;
    }
    return false;
}

} // namespace

bool CalendarCondition::isLocalTime() const
{
    return kind == Kind::Weekday || kind == Kind::Time || kind == Kind::Date;
}

std::optional<CalendarCondition> parseCalendarCondition(std::string_view reference)
{
    using Kind = CalendarCondition::Kind;
    constexpr std::array<std::pair<std::string_view, Kind>, 5> KINDS {{
            {"weekday", Kind::Weekday},
            {"time", Kind::Time},
            {"date", Kind::Date},
            {"window", Kind::Window},
            {"event", Kind::Event},
    }};

    CalendarCondition condition;
    if (reference.starts_with(ACTIVE_PREFIX)) {
        condition.anchor = CalendarAnchor::Active;
        reference.remove_prefix(ACTIVE_PREFIX.size());
    }

    const bool hasSpace = std::ranges::any_of(reference, [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    });
    const auto equals = reference.find('=');
    if (hasSpace || equals == std::string_view::npos) {
        return std::nullopt;
    }

    const auto kind = std::ranges::find(KINDS, reference.substr(0, equals),
                                        &std::pair<std::string_view, Kind>::first);
    if (kind == KINDS.end()) {
        return std::nullopt;
    }
    condition.kind = kind->second;
    condition.key = reference;

    const auto value = reference.substr(equals + 1);
    if (value.empty() || !parseValue(condition, value)) {
        return std::nullopt;
    }
    return condition;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
