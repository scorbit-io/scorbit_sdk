/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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

#include "scorbit_sdk/event_types.h"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace scorbit {
namespace detail {

enum EventPriority {
    Normal,
    High,
    Highest,
};

class EventImpl
{
public:
    EventImpl(EventType type, std::vector<std::string> &&strings = {},
              std::vector<int64_t> &&ints = {});

    EventType type() const { return m_type; }
    const std::vector<std::string> &strings() const { return m_strings; }
    const std::vector<int64_t> &ints() const { return m_ints; }

    // To use in std::priority_queue
    bool operator<(const EventImpl &other) const;

private:
    EventType m_type;
    std::vector<std::string> m_strings;
    std::vector<int64_t> m_ints;

    EventPriority m_priority;
    size_t m_order {++s_orderCounter}; // Incremental order to maintain FIFO for same priority

    static size_t s_orderCounter;
};

using EventCallback = std::function<void(const EventImpl &event)>;

} // namespace detail
} // namespace scorbit

struct sb_event_t : public scorbit::detail::EventImpl {
};
