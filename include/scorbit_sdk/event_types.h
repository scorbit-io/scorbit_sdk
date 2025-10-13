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

#include "event_types_c.h"
#include <scorbit_sdk/event_types_c.h>
#include <fmt/format.h>

namespace scorbit {

enum class EventType {
    GameStartRequested = SB_EVT_GAME_START_REQUESTED,
    CreditsAddRequested = SB_EVT_CREDITS_ADD_REQUESTED,
    CreditsNumberRequested = SB_EVT_CREDITS_NUMBER_REQUESTED,

    // ---------------- OEM providers can ignore the events below ----------------

    None = SB_EVT_NONE, // This event shoud not be used
    ConfigReceived = SB_EVT_CONFIG_RECEIVED,
};

} // namespace scorbit

template<>
struct fmt::formatter<scorbit::EventType> : fmt::formatter<std::string_view> {
    auto format(scorbit::EventType c, fmt::format_context &ctx) const
    {
        using namespace scorbit;
        std::string_view name = "unknown";
        switch (c) {
        case EventType::GameStartRequested:
            name = "GameStartRequested";
            break;
        case EventType::CreditsAddRequested:
            name = "CreditsAddRequested";
            break;
        case EventType::CreditsNumberRequested:
            name = "CreditsNumberRequested";
            break;
        case EventType::None:
            name = "None";
            break;
        case EventType::ConfigReceived:
            name = "ConfigReceived";
            break;
        }
        return fmt::formatter<std::string_view>::format(name, ctx);
    }
};
