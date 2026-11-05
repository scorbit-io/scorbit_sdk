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

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scorbit {

/// @copydoc sb_table_mappings_result_t
enum class TableMappingsResult {
    None = SB_TABLE_MAPPINGS_NONE,
    Ok = SB_TABLE_MAPPINGS_OK,
    NotVpinDevice = SB_TABLE_MAPPINGS_NOT_VPIN_DEVICE,
    NoOwner = SB_TABLE_MAPPINGS_NO_OWNER,
    Unavailable = SB_TABLE_MAPPINGS_UNAVAILABLE,
};

/// @copydoc sb_switch_machine_result_t
enum class SwitchMachineResult {
    Ok = SB_SWITCH_MACHINE_OK,
    AlreadyAttached = SB_SWITCH_MACHINE_ALREADY_ATTACHED,
    GameActive = SB_SWITCH_MACHINE_GAME_ACTIVE,
    Busy = SB_SWITCH_MACHINE_BUSY,
    NotAuthenticated = SB_SWITCH_MACHINE_NOT_AUTHENTICATED,
    NotVpinDevice = SB_SWITCH_MACHINE_NOT_VPIN_DEVICE,
    NoOwner = SB_SWITCH_MACHINE_NO_OWNER,
    TargetNotFound = SB_SWITCH_MACHINE_TARGET_NOT_FOUND,
    TargetNotOwned = SB_SWITCH_MACHINE_TARGET_NOT_OWNED,
    TargetNotVpin = SB_SWITCH_MACHINE_TARGET_NOT_VPIN,
    Network = SB_SWITCH_MACHINE_NETWORK,
    Failed = SB_SWITCH_MACHINE_FAILED,
};

/// @copydoc sb_table_match_result_t
enum class TableMatchResult {
    Mapped = SB_TABLE_MATCH_MAPPED,
    NeedsMapping = SB_TABLE_MATCH_NEEDS_MAPPING,
    UnknownRom = SB_TABLE_MATCH_UNKNOWN_ROM,
    UnknownSignature = SB_TABLE_MATCH_UNKNOWN_SIGNATURE,
    SignatureTooLarge = SB_TABLE_MATCH_SIGNATURE_TOO_LARGE,
    Invalid = SB_TABLE_MATCH_INVALID,
    NotVpinDevice = SB_TABLE_MATCH_NOT_VPIN_DEVICE,
    NoOwner = SB_TABLE_MATCH_NO_OWNER,
    NotAuthenticated = SB_TABLE_MATCH_NOT_AUTHENTICATED,
    Network = SB_TABLE_MATCH_NETWORK,
    Failed = SB_TABLE_MATCH_FAILED,
};

/// A table-mapping lookup; see @ref sb_match_table_mapping and the request setters.
struct TableMatchRequest {
    std::string keyType;
    std::string tableKey;
    std::string firmwareId;                          ///< Empty: left out of the request.
    std::optional<std::string> signaturesJson;       ///< A JSON array, sent as given.
    std::optional<std::string> signatureUnavailable; ///< Why there are no signatures.
};

/// The server's signature verdict; its strings are opaque to the SDK.
struct SignatureVerdict {
    std::string status;
    std::string acceptedVersion; ///< Empty when null.
    std::vector<std::string> changedSections;
    std::vector<std::string> changedProcedures;
};

/// A copy of the answer to a table-mapping lookup; see @ref sb_table_match_t.
struct TableMatch {
    int httpStatus = 0; ///< 0: no response.
    std::string errorCode;
    std::string errorAttr;
    std::string keyType;
    std::string tableKey;
    std::string mappingId;
    std::string venueMachine; ///< VenueMachine uuid: the target for a machine switch.
    std::string venueMachineName;
    std::string machine; ///< Machine uuid
    std::string machineName;
    std::optional<SignatureVerdict> verdict;

    /// Copies @p m, which stays the SDK's.
    static TableMatch fromC(sb_table_match_t m)
    {
        TableMatch out;
        out.httpStatus = sb_table_match_http_status(m);
        out.errorCode = sb_table_match_error_code(m);
        out.errorAttr = sb_table_match_error_attr(m);
        out.keyType = sb_table_match_key_type(m);
        out.tableKey = sb_table_match_table_key(m);
        out.mappingId = sb_table_match_mapping_id(m);
        out.venueMachine = sb_table_match_venuemachine(m);
        out.venueMachineName = sb_table_match_venuemachine_name(m);
        out.machine = sb_table_match_machine(m);
        out.machineName = sb_table_match_machine_name(m);
        if (sb_table_match_has_verdict(m)) {
            SignatureVerdict verdict;
            verdict.status = sb_table_match_verdict_status(m);
            verdict.acceptedVersion = sb_table_match_verdict_accepted_version(m);
            const auto sections = sb_table_match_verdict_changed_sections_count(m);
            for (std::size_t i = 0; i < sections; ++i) {
                verdict.changedSections.emplace_back(sb_table_match_verdict_changed_section(m, i));
            }
            const auto procedures = sb_table_match_verdict_changed_procedures_count(m);
            for (std::size_t i = 0; i < procedures; ++i) {
                verdict.changedProcedures.emplace_back(
                        sb_table_match_verdict_changed_procedure(m, i));
            }
            out.verdict = std::move(verdict);
        }
        return out;
    }
};

/// One VPIN table mapping, as the server sent it. @c keyType and @c tableKey are opaque.
struct TableMapping {
    std::string keyType;
    std::string tableKey;
    std::string venueMachine; ///< VenueMachine uuid
};

/// A copy of the VPIN table mappings; see @ref sb_get_table_mappings.
struct TableMappings {
    TableMappingsResult result {TableMappingsResult::None};
    std::string etag;
    std::string serverTime;
    std::vector<TableMapping> mappings;

    /// Copies @p m; the caller still frees it.
    static TableMappings fromC(sb_table_mappings_t m)
    {
        TableMappings out;
        out.result = static_cast<TableMappingsResult>(sb_table_mappings_result(m));
        out.etag = sb_table_mappings_etag(m);
        out.serverTime = sb_table_mappings_server_time(m);
        const auto count = sb_table_mappings_count(m);
        out.mappings.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            out.mappings.push_back({sb_table_mapping_key_type(m, i),
                                    sb_table_mapping_table_key(m, i),
                                    sb_table_mapping_venuemachine(m, i)});
        }
        return out;
    }
};

} // namespace scorbit
