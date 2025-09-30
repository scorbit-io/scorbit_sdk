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

#include "event_impl.h"

using namespace scorbit::detail;

size_t EventImpl::s_orderCounter = 0;

EventImpl::EventImpl(EventType type, std::vector<std::string> &&strings,
                                      std::vector<int64_t> &&ints)
    : m_type {type}
    , m_strings {std::move(strings)}
    , m_ints {std::move(ints)}
    , m_priority {EventPriority::Normal}
{
    // Set priority based on event type
    switch (m_type) {
    case EventType::GameStartRequested:
        m_priority = EventPriority::Highest;
        break;

    case EventType::CreditsAddRequested:
    case EventType::CreditsNumberRequested:
        m_priority = EventPriority::High;
        break;

    case EventType::ConfigReceived:
    case EventType::None:
        m_priority = EventPriority::Normal;
        break;
    }
}

bool EventImpl::operator<(const EventImpl &other) const
{
    if (m_priority == other.m_priority) {
        return m_order > other.m_order; // Lower number has higher priority
    }
    return m_priority < other.m_priority; // Higher priority value has higher priority
}
