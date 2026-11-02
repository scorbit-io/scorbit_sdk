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

#include <scorbit_sdk/table_mappings_c.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

/// The opaque copy handed to C callers.
struct sb_table_mappings_struct {
    sb_table_mappings_result_t result {SB_TABLE_MAPPINGS_NONE};
    std::string etag;
    std::string serverTime;
    struct Row {
        std::string keyType;
        std::string tableKey;
        std::string venueMachine;
        bool operator==(const Row &) const = default;
    };
    std::vector<Row> rows;
};

namespace scorbit {
namespace detail {

using TableMappingsSnapshot = sb_table_mappings_struct;

/// errors[0].code of the api's error envelope, or "" when the body carries none.
std::string apiErrorCode(const std::string &body);

/// The rows and server_time of a 200 table-mappings body; nullopt when it is malformed.
std::optional<TableMappingsSnapshot> parseTableMappings(const std::string &body);

/// The result for a failed table-mappings read with @p httpStatus (0: no response).
sb_table_mappings_result_t tableMappingsFailure(int httpStatus, const std::string &body);

/// The result for a non-200 switch-machine reply with @p httpStatus (0: no response).
sb_switch_machine_result_t switchMachineFailure(int httpStatus, const std::string &body);

/// The Centrifugo token in a 200 switch-machine body, or "" when there is none.
std::string switchMachineToken(const std::string &body);

/**
 * The SDK's one copy of the table mappings, and how each read's reply changes it.
 *
 * Thread-safe. Reads that failed outright keep the last good list (SPEC-0031 REQ-8); refusals
 * that say the device may not read empty it and suspend reading.
 */
class TableMappingsStore
{
public:
    TableMappingsSnapshot snapshot() const;
    /// The ETag to send as If-None-Match, "" for none.
    std::string etag() const;

    /// Applies one read's reply. @p etag is the reply's ETag header. Returns true when the list or
    /// the result changed, which is when SB_EVT_TABLE_MAPPINGS_UPDATED is due.
    bool apply(int httpStatus, const std::string &etag, const std::string &body);

    /// Whether a refusal stopped reading; cleared by resume().
    bool suspended() const { return m_suspended; }
    void resume() { m_suspended = false; }

private:
    bool set(TableMappingsSnapshot next);

    mutable std::mutex m_mutex;
    TableMappingsSnapshot m_snapshot;
    std::atomic_bool m_suspended {false};
};

/**
 * One switch-machine run, its steps supplied by Net so the sequence is testable on its own.
 * Governing: SPEC-0009 REQ-4 (drain), REQ-5.3 (already attached), REQ-6.2 (no second request).
 */
class MachineSwitch
{
public:
    using Done = std::function<void()>;
    using Callback = std::function<void(sb_switch_machine_result_t, const std::string &)>;

    struct Steps {
        /// Holds (true) or releases (false) new session and game-data posts.
        std::function<void(bool hold)> gate;
        /// Calls @p done once every session post queued so far has gone out, or the bound passed.
        std::function<void(Done done)> drain;
        /// POSTs the switch; replies with the HTTP status (0: none) and the body.
        std::function<void(const std::string &target,
                           std::function<void(int status, const std::string &body)>)>
                post;
        /// Adopts @p token and refreshes the machine context; @p done once it is in.
        std::function<void(const std::string &target, const std::string &token, Done done)> adopt;
        /// Raises SB_EVT_MACHINE_CHANGED (and the pairing event if it changed).
        std::function<void(const std::string &target)> changed;
        /// Re-reads the table mappings after a failure.
        std::function<void()> refetch;
        /// Runs @p fn after every event pushed so far has been delivered.
        std::function<void(Done fn)> deliver;
        /// True once the SDK is shutting down: nothing more is called back.
        std::function<bool()> stopping;
    };

    explicit MachineSwitch(Steps steps)
        : m_steps {std::move(steps)}
    {
    }

    bool inFlight() const { return m_inFlight; }
    /// For tests that replace some steps of a Net's own switch.
    Steps &steps() { return m_steps; }

    /**
     * Starts a switch to @p target. @p current is the venue machine known now ("" if unpaired),
     * @p authenticated whether the device has a session. The game-active refusal is the caller's.
     */
    void start(const std::string &target, const std::string &current, bool authenticated,
               Callback callback);

private:
    void finish(sb_switch_machine_result_t result, const std::string &target, Callback callback,
                bool refetch);

    Steps m_steps;
    std::atomic_bool m_inFlight {false};
};

} // namespace detail
} // namespace scorbit
