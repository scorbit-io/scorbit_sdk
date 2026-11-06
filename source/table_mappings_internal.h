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

/// The C request builder; copied whole when a lookup is started.
struct sb_table_match_request_struct {
    std::string keyType;
    std::string tableKey;
    std::string firmwareId;
    std::optional<std::string> signaturesJson;
    std::optional<std::string> signatureUnavailable;
};

/// The answer a lookup's callback reads.
struct sb_table_match_struct {
    int httpStatus {0};
    std::string errorCode;
    std::string errorAttr;
    std::string keyType;
    std::string tableKey;
    std::string mappingId;
    std::string venueMachine;
    std::string venueMachineName;
    std::string machine;
    std::string machineName;
    bool hasVerdict {false};
    std::string verdictStatus;
    std::string verdictAcceptedVersion;
    std::vector<std::string> changedSections;
    std::vector<std::string> changedProcedures;
};

namespace scorbit {
namespace detail {

using TableMappingsSnapshot = sb_table_mappings_struct;
using TableMatchRequestData = sb_table_match_request_struct;
using TableMatchAnswer = sb_table_match_struct;
/// Receives a lookup's result and its answer, which is valid only during the call.
using TableMatchCallback = std::function<void(sb_table_match_result_t, const TableMatchAnswer &)>;

/// errors[0].code of the api's error envelope, or "" when the body carries none.
std::string apiErrorCode(const std::string &body);

/// errors[0].attr of the api's error envelope, or "" when it is null or absent.
std::string apiErrorAttr(const std::string &body);

/// The JSON body of a table-mapping lookup, the caller's signatures text spliced in as it is;
/// nullopt when the request must be refused (both kinds of evidence, or signatures that are not
/// a non-empty JSON array).
std::optional<std::string> tableMatchBody(const TableMatchRequestData &request);

/// The mapping, target and verdict of a 200 lookup body; nullopt when it is malformed.
std::optional<TableMatchAnswer> parseTableMatch(const std::string &body);

/// The result for a lookup reply other than a 200, with @p httpStatus (0: no response).
sb_table_match_result_t tableMatchFailure(int httpStatus, const std::string &body);

/// The rows and server_time of a 200 table-mappings body; nullopt when it is malformed.
std::optional<TableMappingsSnapshot> parseTableMappings(const std::string &body);

/// The result for a failed table-mappings read with @p httpStatus (0: no response).
sb_table_mappings_result_t tableMappingsFailure(int httpStatus, const std::string &body);

/// The result for a non-200 switch-machine reply with @p httpStatus (0: no response).
sb_switch_machine_result_t switchMachineFailure(int httpStatus, const std::string &body);

/// The Centrifugo token in a 200 switch-machine body, or "" when there is none.
std::string switchMachineToken(const std::string &body);

/// The venue machine in a scorbitron object reply: machine.id, "" when unpaired (no machine),
/// nullopt when the reply is malformed.
std::optional<std::string> scorbitronMachine(const std::string &body);

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

    /// How the switch request itself ended, before anything that raced it is settled.
    struct Outcome {
        enum class Kind {
            Switched,        ///< the server switched; the reply's context was adopted
            AlreadyAttached, ///< the device was already on the target
            Refused,         ///< the server refused; nothing changed there
            Unknown,         ///< no answer, a server error, or an unusable reply
        } kind;
        sb_switch_machine_result_t result;
    };
    /// Receives the switch's final result once the machine context is settled.
    using Resolved = std::function<void(sb_switch_machine_result_t)>;

    struct Steps {
        /// Holds (true) or releases (false) new session and game-data posts.
        std::function<void(bool hold)> gate;
        /// Calls @p done once every session post queued so far has gone out, or the bound passed.
        std::function<void(Done done)> drain;
        /// POSTs the switch; replies with the HTTP status (0: none) and the body.
        std::function<void(const std::string &target,
                           std::function<void(int status, const std::string &body)>)>
                post;
        /// Adopts @p token and refreshes the machine context; @p done once it is in. An empty
        /// @p token means the switch reply had none: a fresh one is fetched instead.
        std::function<void(const std::string &target, const std::string &token, Done done)> adopt;
        /// Re-reads the table mappings after a failure.
        std::function<void()> refetch;
        /// Settles the machine context after @p outcome and reports the final result. The only
        /// way a switch ends once posts are held: whatever raced it (a pairing signal, a reply
        /// naming another machine) or an unknown outcome is resolved with the server first,
        /// and MACHINE_CHANGED is raised if the device ends on another machine than @p start.
        std::function<void(const Outcome &, const std::string &target, const std::string &start,
                           Resolved)>
                resolve;
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
    /// The only way out once posts are held: resolves the context, releases the posts, calls back.
    void resolveAndFinish(const Outcome &outcome, const std::string &target,
                          const std::string &start, Callback callback);

    Steps m_steps;
    std::atomic_bool m_inFlight {false};
};

} // namespace detail
} // namespace scorbit
