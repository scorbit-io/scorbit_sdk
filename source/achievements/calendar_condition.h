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

#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** When an occurrence is inside a `CALENDAR` window (§5.5.2). */
enum class CalendarAnchor {
    /** The instant it occurred. */
    Start,
    /** Any instant while it lasted: the `active:` prefix. */
    Active,
};

/** One condition of a `CALENDAR` rule, from its `reference` (§5.5.2). */
struct CalendarCondition {
    enum class Kind { Weekday, Time, Date, Window, Event };

    /** A listed date: every year when @ref year is absent. */
    struct Date {
        std::optional<int64_t> year;
        unsigned month {1};
        unsigned day {1};
    };

    CalendarAnchor anchor {CalendarAnchor::Start};
    Kind kind {Kind::Weekday};

    /** `weekday=`: the listed days, indexed by @ref Weekday. */
    std::bitset<7> weekdays;

    /** `time=`: minutes after local midnight; the end is exclusive and may wrap past midnight. */
    unsigned startMinute {0};
    unsigned endMinute {0};

    /** `date=`: the listed dates. */
    std::vector<Date> dates;

    /** `window=` / `event=`: the named window or event. */
    std::string slug;

    /** The condition without its anchor, which keys the definitions response's `calendar`. */
    std::string key;

    /** Judged on the venue's wall clock, so it needs the venue's UTC offsets. */
    bool isLocalTime() const;
};

/**
 * Parses a `CALENDAR` rule's reference: an optional `active:`, then exactly one of
 * `weekday=<days>`, `time=<HH:MM>-<HH:MM>`, `date=<dates>`, `window=<slug>` or `event=<slug>`.
 * Only the canonical spelling parses — no spaces, no lower-case days, no duplicates — as the API
 * validates it.
 */
std::optional<CalendarCondition> parseCalendarCondition(std::string_view reference);

} // namespace achievements
} // namespace detail
} // namespace scorbit
