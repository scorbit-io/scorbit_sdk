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

#include <boost/flyweight.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {

/**
 * One operation on a named event register: game code added @ref value to register @ref name.
 * Recorded in the `events` column of the session timeline as `name+=value`.
 */
struct EventOp {
    EventOp(std::string eventName, int64_t eventValue)
        : name {std::move(eventName)}
        , value {eventValue}
    {
    }

    boost::flyweight<std::string> name;
    int64_t value {0};
};

inline bool operator==(const EventOp &lhs, const EventOp &rhs)
{
    return lhs.name == rhs.name && lhs.value == rhs.value;
}

/** Event operations applied since the previous update; a one-shot edge, like completed modes. */
using EventOps = std::vector<EventOp>;

/**
 * Whether @p name can be used as a mode or event name in the session timeline. The characters
 * `;` `=` `,` `"` and newlines are structural in the timeline encoding and in achievement rule
 * references, so a name containing any of them, or an empty one, is rejected.
 */
inline bool isValidTimelineName(std::string_view name)
{
    return !name.empty() && name.find_first_of(";=,\"\r\n") == std::string_view::npos;
}

/** Formats @p ops as the timeline `events` cell body, e.g. `spins+=3;ramps+=1` (unquoted). */
std::string eventOpsStr(const EventOps &ops);

} // namespace detail
} // namespace scorbit
