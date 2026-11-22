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

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {

/// One position of a session, as the app sees it in a SessionPlayersChanged event.
struct SessionPlayerState {
    bool claimed {false};
    std::string playerId;
    std::string displayName; ///< The API's player display_name, the lobby's name

    bool operator==(const SessionPlayerState &) const = default;
};

/// Each position of a session's scores array, in order: index 0 is position 1. Positions the array
/// skips stay unclaimed; an entry without a usable position is ignored.
std::vector<SessionPlayerState> sessionPlayersFromScores(const nlohmann::json &scores);

/// Decides which session replies the app hears about (SPEC-0034 REQ-9.1): each session's first
/// reply is its baseline, and later ones only when a position's claim or display name changed.
class SessionPlayersTracker
{
public:
    struct Change {
        bool isFirst {false};
        std::vector<SessionPlayerState> players;
    };

    /// @return What to report for this reply, or nothing when it changes nothing.
    std::optional<Change> update(const std::string &sessionUuid,
                                 std::vector<SessionPlayerState> players);

    /// Forget every session, as when the device changes machine.
    void clear();

private:
    // A late reply for the previous game must not count as a new baseline, so a few are kept.
    static constexpr std::size_t MAX_SESSIONS {4};

    std::mutex m_mutex;
    std::deque<std::pair<std::string, std::vector<SessionPlayerState>>> m_sessions;
};

/// At most one fallback session fetch queued or in flight: during an outage a fetch can outlast
/// the refresh interval, and stacking them would hold up the serialized session queue.
class RefreshGate
{
public:
    /// A token for the fetch to hold until its task is gone (run, failed or dropped), or empty
    /// while the previous token is still held.
    std::shared_ptr<void> tryAcquire();

private:
    // Shared with the token, so a token outliving the gate stays safe
    std::shared_ptr<std::atomic_bool> m_busy {std::make_shared<std::atomic_bool>(false)};
};

} // namespace detail
} // namespace scorbit
