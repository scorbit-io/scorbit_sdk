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

#include "table_mappings_internal.h"
#include "identifiers.h"
#include <logger/logger.h>
#include <nlohmann/json.hpp>
#include <new>
#include <utility>

namespace scorbit {
namespace detail {

namespace {

constexpr int HTTP_OK_FIRST = 200;
constexpr int HTTP_OK_LAST = 299;
constexpr int HTTP_OK = 200;
constexpr int HTTP_NOT_MODIFIED = 304;
constexpr int HTTP_BAD_REQUEST = 400;
constexpr int HTTP_UNAUTHORIZED = 401;
constexpr int HTTP_FORBIDDEN = 403;
constexpr int HTTP_NOT_FOUND = 404;
constexpr int HTTP_CONFLICT = 409;
constexpr int HTTP_PAYLOAD_TOO_LARGE = 413;
constexpr int HTTP_TOO_MANY_REQUESTS = 429;
constexpr int HTTP_SERVER_ERROR = 500;

std::optional<nlohmann::json> parseObject(const std::string &body)
{
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    return j;
}

/// Reads @p obj's string @p key into @p out. False if it is not a string, or is missing or null
/// while @p required.
bool stringField(const nlohmann::json &obj, const char *key, std::string &out, bool required)
{
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return !required;
    }
    if (!it->is_string()) {
        return false;
    }
    out = it->get<std::string>();
    return true;
}

/// Reads @p obj's list of strings @p key, if any, into @p out. False if it is not one.
bool stringList(const nlohmann::json &obj, const char *key, std::vector<std::string> &out)
{
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return true;
    }
    if (!it->is_array()) {
        return false;
    }
    for (const auto &item : *it) {
        if (!item.is_string()) {
            return false;
        }
        out.push_back(item.get<std::string>());
    }
    return true;
}

} // namespace

std::string apiErrorCode(const std::string &body)
{
    const auto j = parseObject(body);
    if (!j) {
        return {};
    }
    if (const auto it = j->find(JKEY_API_ERRORS);
        it != j->end() && it->is_array() && !it->empty() && it->front().is_object()) {
        const auto &first = it->front();
        if (const auto code = first.find(JKEY_API_ERROR_CODE);
            code != first.end() && code->is_string()) {
            return code->get<std::string>();
        }
    }
    if (const auto code = j->find(JKEY_API_ERROR_CODE); code != j->end() && code->is_string()) {
        return code->get<std::string>();
    }
    return {};
}

std::string apiErrorAttr(const std::string &body)
{
    const auto j = parseObject(body);
    if (!j) {
        return {};
    }
    std::string attr;
    if (const auto it = j->find(JKEY_API_ERRORS);
        it != j->end() && it->is_array() && !it->empty() && it->front().is_object()) {
        stringField(it->front(), JKEY_API_ERROR_ATTR, attr, false);
    }
    return attr;
}

std::optional<std::string> tableMatchBody(const TableMatchRequestData &request)
{
    if (request.signaturesJson && request.signatureUnavailable) {
        return std::nullopt;
    }
    if (request.signaturesJson) {
        const auto parsed = nlohmann::json::parse(*request.signaturesJson, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_array() || parsed.empty()) {
            return std::nullopt;
        }
    }

    nlohmann::json j {{JKEY_TMATCH_KEY_TYPE, request.keyType},
                      {JKEY_TMATCH_TABLE_KEY, request.tableKey}};
    if (!request.firmwareId.empty()) {
        j[JKEY_TMATCH_FIRMWARE_ID] = request.firmwareId;
    }
    if (request.signatureUnavailable) {
        j[JKEY_TMATCH_SIGNATURE_UNAVAILABLE] = *request.signatureUnavailable;
    }
    auto body = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    if (request.signaturesJson) {
        // Spliced, not re-encoded: the JSON type would sort the keys and reprint the numbers.
        body.pop_back();
        body += ",\"";
        body += JKEY_TMATCH_SIGNATURES;
        body += "\":";
        body += *request.signaturesJson;
        body += '}';
    }
    return body;
}

std::optional<TableMatchAnswer> parseTableMatch(const std::string &body)
{
    const auto j = parseObject(body);
    if (!j) {
        return std::nullopt;
    }
    const auto mapping = j->find(JKEY_TMATCH_MAPPING);
    const auto target = j->find(JKEY_TMATCH_VENUEMACHINE);
    if (mapping == j->end() || !mapping->is_object() || target == j->end()
        || !target->is_object()) {
        return std::nullopt;
    }

    TableMatchAnswer out;
    if (!stringField(*mapping, JKEY_TMATCH_KEY_TYPE, out.keyType, true)
        || !stringField(*mapping, JKEY_TMATCH_TABLE_KEY, out.tableKey, true)
        || !stringField(*mapping, JKEY_TMATCH_ID, out.mappingId, true)
        || !stringField(*target, JKEY_TMATCH_ID, out.venueMachine, true) || out.venueMachine.empty()
        || !stringField(*target, JKEY_TMATCH_NAME, out.venueMachineName, false)) {
        return std::nullopt;
    }
    if (const auto machine = target->find(JKEY_TMATCH_MACHINE);
        machine != target->end() && !machine->is_null()) {
        if (!machine->is_object() || !stringField(*machine, JKEY_TMATCH_ID, out.machine, false)
            || !stringField(*machine, JKEY_TMATCH_NAME, out.machineName, false)) {
            return std::nullopt;
        }
    }
    if (const auto verdict = j->find(JKEY_TMATCH_SIGNATURE);
        verdict != j->end() && !verdict->is_null()) {
        if (!verdict->is_object()
            || !stringField(*verdict, JKEY_TMATCH_VERDICT_STATUS, out.verdictStatus, true)
            || !stringField(*verdict, JKEY_TMATCH_ACCEPTED_VERSION, out.verdictAcceptedVersion,
                            false)
            || !stringList(*verdict, JKEY_TMATCH_CHANGED_SECTIONS, out.changedSections)
            || !stringList(*verdict, JKEY_TMATCH_CHANGED_PROCEDURES, out.changedProcedures)) {
            return std::nullopt;
        }
        out.hasVerdict = true;
    }
    return out;
}

sb_table_match_result_t tableMatchFailure(int httpStatus, const std::string &body)
{
    if (httpStatus == 0 || httpStatus == HTTP_TOO_MANY_REQUESTS
        || httpStatus >= HTTP_SERVER_ERROR) {
        return SB_TABLE_MATCH_NETWORK;
    }
    if (httpStatus == HTTP_UNAUTHORIZED) {
        return SB_TABLE_MATCH_NOT_AUTHENTICATED;
    }
    if (httpStatus == HTTP_BAD_REQUEST) {
        return SB_TABLE_MATCH_INVALID;
    }
    // A proxy's body limit answers 413 with no code: the set is too large either way
    if (httpStatus == HTTP_PAYLOAD_TOO_LARGE) {
        return SB_TABLE_MATCH_SIGNATURE_TOO_LARGE;
    }

    // Otherwise the code decides; without a known one (an api that lacks the route answers 404
    // or 405 with none), it failed.
    const auto code = apiErrorCode(body);
    if (httpStatus == HTTP_FORBIDDEN && code == JVAL_ERR_NOT_VPIN_DEVICE) {
        return SB_TABLE_MATCH_NOT_VPIN_DEVICE;
    }
    if (httpStatus == HTTP_FORBIDDEN && code == JVAL_ERR_NO_OWNER) {
        return SB_TABLE_MATCH_NO_OWNER;
    }
    if (httpStatus == HTTP_NOT_FOUND && code == JVAL_ERR_UNKNOWN_ROM) {
        return SB_TABLE_MATCH_UNKNOWN_ROM;
    }
    if (httpStatus == HTTP_NOT_FOUND && code == JVAL_ERR_UNKNOWN_SIGNATURE) {
        return SB_TABLE_MATCH_UNKNOWN_SIGNATURE;
    }
    if (httpStatus == HTTP_CONFLICT && code == JVAL_ERR_VENUEMACHINE_NEEDED) {
        return SB_TABLE_MATCH_NEEDS_MAPPING;
    }
    return SB_TABLE_MATCH_FAILED;
}

std::optional<TableMappingsSnapshot> parseTableMappings(const std::string &body)
{
    const auto j = parseObject(body);
    if (!j) {
        return std::nullopt;
    }
    const auto rows = j->find(JKEY_TMAP_MAPPINGS);
    if (rows == j->end() || !rows->is_array()) {
        return std::nullopt;
    }

    TableMappingsSnapshot out;
    if (const auto t = j->find(JKEY_TMAP_SERVER_TIME); t != j->end() && t->is_string()) {
        out.serverTime = t->get<std::string>();
    }
    out.rows.reserve(rows->size());
    for (const auto &row : *rows) {
        if (!row.is_object()) {
            return std::nullopt;
        }
        const auto keyType = row.find(JKEY_TMAP_KEY_TYPE);
        const auto tableKey = row.find(JKEY_TMAP_TABLE_KEY);
        const auto venueMachine = row.find(JKEY_TMAP_VENUEMACHINE);
        if (keyType == row.end() || !keyType->is_string() || tableKey == row.end()
            || !tableKey->is_string() || venueMachine == row.end() || !venueMachine->is_string()) {
            return std::nullopt;
        }
        out.rows.push_back({keyType->get<std::string>(), tableKey->get<std::string>(),
                            venueMachine->get<std::string>()});
    }
    return out;
}

sb_table_mappings_result_t tableMappingsFailure(int httpStatus, const std::string &body)
{
    if (httpStatus == HTTP_FORBIDDEN) {
        const auto code = apiErrorCode(body);
        if (code == JVAL_ERR_NOT_VPIN_DEVICE) {
            return SB_TABLE_MAPPINGS_NOT_VPIN_DEVICE;
        }
        if (code == JVAL_ERR_NO_OWNER) {
            return SB_TABLE_MAPPINGS_NO_OWNER;
        }
    }
    return SB_TABLE_MAPPINGS_UNAVAILABLE;
}

sb_switch_machine_result_t switchMachineFailure(int httpStatus, const std::string &body)
{
    if (httpStatus == 0 || httpStatus >= HTTP_SERVER_ERROR) {
        return SB_SWITCH_MACHINE_NETWORK;
    }

    const auto code = apiErrorCode(body);
    if (code == JVAL_ERR_NO_OWNER) {
        return SB_SWITCH_MACHINE_NO_OWNER;
    }
    if (code == JVAL_ERR_NOT_VPIN_DEVICE) {
        return SB_SWITCH_MACHINE_NOT_VPIN_DEVICE;
    }
    if (code == JVAL_ERR_VENUEMACHINE_NOT_FOUND) {
        return SB_SWITCH_MACHINE_TARGET_NOT_FOUND;
    }
    if (code == JVAL_ERR_VENUEMACHINE_NOT_OWNED) {
        return SB_SWITCH_MACHINE_TARGET_NOT_OWNED;
    }
    if (code == JVAL_ERR_VENUEMACHINE_NOT_VPIN) {
        return SB_SWITCH_MACHINE_TARGET_NOT_VPIN;
    }

    // Before the api names its codes, the status alone still tells the target errors apart.
    if (httpStatus == HTTP_NOT_FOUND) {
        return SB_SWITCH_MACHINE_TARGET_NOT_FOUND;
    }
    if (httpStatus == HTTP_FORBIDDEN) {
        return SB_SWITCH_MACHINE_TARGET_NOT_OWNED;
    }
    return SB_SWITCH_MACHINE_FAILED;
}

std::string switchMachineToken(const std::string &body)
{
    const auto j = parseObject(body);
    if (!j) {
        return {};
    }
    if (const auto t = j->find(JKEY_SWITCH_TOKEN); t != j->end() && t->is_string()) {
        return t->get<std::string>();
    }
    return {};
}

std::optional<std::string> scorbitronMachine(const std::string &body)
{
    const auto j = parseObject(body);
    if (!j) {
        return std::nullopt;
    }
    const auto machine = j->find(JKEY_SOBJ_MACHINE_OBJ);
    if (machine == j->end() || !machine->is_object()) {
        return std::string {};
    }
    if (const auto id = machine->find(JKEY_SOBJ_MACHINE_UUID);
        id != machine->end() && id->is_string()) {
        return id->get<std::string>();
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------

TableMappingsSnapshot TableMappingsStore::snapshot() const
{
    std::scoped_lock lock(m_mutex);
    return m_snapshot;
}

std::string TableMappingsStore::etag() const
{
    std::scoped_lock lock(m_mutex);
    return m_snapshot.etag;
}

bool TableMappingsStore::set(TableMappingsSnapshot next)
{
    std::scoped_lock lock(m_mutex);
    // server_time and the ETag may move with nothing else; only the list and the outcome are
    // news to the host. The new ETag is still kept, for the next If-None-Match.
    const bool changed = next.result != m_snapshot.result || next.rows != m_snapshot.rows;
    m_snapshot = std::move(next);
    return changed;
}

bool TableMappingsStore::apply(int httpStatus, const std::string &etag, const std::string &body)
{
    if (httpStatus == HTTP_OK) {
        if (auto parsed = parseTableMappings(body)) {
            parsed->result = SB_TABLE_MAPPINGS_OK;
            parsed->etag = etag;
            return set(std::move(*parsed));
        }
        WRN("API table mappings reply is malformed, keeping the last list");
        auto next = snapshot();
        next.result = SB_TABLE_MAPPINGS_UNAVAILABLE;
        return set(std::move(next));
    }

    if (httpStatus == HTTP_NOT_MODIFIED) {
        auto next = snapshot();
        next.result = SB_TABLE_MAPPINGS_OK;
        return set(std::move(next));
    }

    const auto result = tableMappingsFailure(httpStatus, body);
    if (result == SB_TABLE_MAPPINGS_NOT_VPIN_DEVICE || result == SB_TABLE_MAPPINGS_NO_OWNER) {
        m_suspended = true;
        TableMappingsSnapshot next;
        next.result = result;
        return set(std::move(next));
    }

    auto next = snapshot();
    next.result = SB_TABLE_MAPPINGS_UNAVAILABLE;
    return set(std::move(next));
}

// ---------------------------------------------------------------------------------------------

void MachineSwitch::finish(sb_switch_machine_result_t result, const std::string &target,
                           Callback callback, bool refetch)
{
    // Cleared before the callback, so a host may start another switch from inside it.
    m_inFlight = false;
    if (refetch) {
        m_steps.refetch();
    }
    m_steps.deliver([result, target, callback = std::move(callback)] {
        if (callback) {
            callback(result, target);
        }
    });
}

void MachineSwitch::start(const std::string &target, const std::string &current, bool authenticated,
                          Callback callback)
{
    if (m_inFlight.exchange(true)) {
        m_steps.deliver([target, callback = std::move(callback)] {
            if (callback) {
                callback(SB_SWITCH_MACHINE_BUSY, target);
            }
        });
        return;
    }

    if (!authenticated) {
        finish(SB_SWITCH_MACHINE_NOT_AUTHENTICATED, target, std::move(callback), false);
        return;
    }

    INF("API switch machine to {} started", target);

    // Held before draining, so a post made while the old ones drain waits for the new machine
    // instead of slipping out to the old one.
    m_steps.gate(true);
    m_steps.drain([this, target, current, callback = std::move(callback)]() mutable {
        if (m_steps.stopping()) {
            return;
        }
        m_steps.post(target, [this, target, current, callback = std::move(callback)](
                                     int status, const std::string &body) mutable {
            if (m_steps.stopping()) {
                return;
            }

            using Kind = Outcome::Kind;
            if (status < HTTP_OK_FIRST || status > HTTP_OK_LAST) {
                const auto result = switchMachineFailure(status, body);
                WRN("API switch machine to {} failed: HTTP {}, result {}", target, status,
                    static_cast<int>(result));
                // No answer or a server error: the switch may still have been made.
                const auto kind =
                        result == SB_SWITCH_MACHINE_NETWORK ? Kind::Unknown : Kind::Refused;
                resolveAndFinish({kind, result}, target, current, std::move(callback));
                return;
            }

            if (!current.empty() && current == target) {
                INF("API switch machine: already on {}", target);
                resolveAndFinish({Kind::AlreadyAttached, SB_SWITCH_MACHINE_ALREADY_ATTACHED},
                                 target, current, std::move(callback));
                return;
            }

            const auto token = switchMachineToken(body);
            if (token.empty()) {
                // The server accepted the switch; only its reply is unusable.
                ERR("API switch machine to {}: reply carries no token", target);
                resolveAndFinish({Kind::Unknown, SB_SWITCH_MACHINE_FAILED}, target, current,
                                 std::move(callback));
                return;
            }

            m_steps.adopt(target, token,
                          [this, target, current, callback = std::move(callback)]() mutable {
                              if (m_steps.stopping()) {
                                  return;
                              }
                              INF("API switch machine: now on {}", target);
                              resolveAndFinish({Kind::Switched, SB_SWITCH_MACHINE_OK}, target,
                                               current, std::move(callback));
                          });
        });
    });
}

void MachineSwitch::resolveAndFinish(const Outcome &outcome, const std::string &target,
                                     const std::string &start, Callback callback)
{
    m_steps.resolve(outcome, target, start,
                    [this, target,
                     callback = std::move(callback)](sb_switch_machine_result_t result) mutable {
                        if (m_steps.stopping()) {
                            return;
                        }
                        m_steps.gate(false);
                        const bool onTarget = result == SB_SWITCH_MACHINE_OK
                                           || result == SB_SWITCH_MACHINE_ALREADY_ATTACHED;
                        finish(result, target, std::move(callback), !onTarget);
                    });
}

} // namespace detail
} // namespace scorbit

// --------------------------------------- C accessors ---------------------------------------

namespace {

const char *rowField(sb_table_mappings_t m, size_t index,
                     std::string sb_table_mappings_struct::Row::*field)
{
    if (!m || index >= m->rows.size()) {
        return "";
    }
    return (m->rows[index].*field).c_str();
}

} // namespace

void sb_table_mappings_free(sb_table_mappings_t mappings)
{
    delete mappings;
}

sb_table_mappings_result_t sb_table_mappings_result(sb_table_mappings_t mappings)
{
    return mappings ? mappings->result : SB_TABLE_MAPPINGS_NONE;
}

const char *sb_table_mappings_etag(sb_table_mappings_t mappings)
{
    return mappings ? mappings->etag.c_str() : "";
}

const char *sb_table_mappings_server_time(sb_table_mappings_t mappings)
{
    return mappings ? mappings->serverTime.c_str() : "";
}

size_t sb_table_mappings_count(sb_table_mappings_t mappings)
{
    return mappings ? mappings->rows.size() : 0;
}

const char *sb_table_mapping_key_type(sb_table_mappings_t mappings, size_t index)
{
    return rowField(mappings, index, &sb_table_mappings_struct::Row::keyType);
}

const char *sb_table_mapping_table_key(sb_table_mappings_t mappings, size_t index)
{
    return rowField(mappings, index, &sb_table_mappings_struct::Row::tableKey);
}

const char *sb_table_mapping_venuemachine(sb_table_mappings_t mappings, size_t index)
{
    return rowField(mappings, index, &sb_table_mappings_struct::Row::venueMachine);
}

// ------------------------------------ table-mapping lookup ---------------------------------

namespace {

const char *matchField(sb_table_match_t m, std::string sb_table_match_struct::*field)
{
    return m ? (m->*field).c_str() : "";
}

const char *listItem(sb_table_match_t m, std::vector<std::string> sb_table_match_struct::*list,
                     size_t index)
{
    if (!m || index >= (m->*list).size()) {
        return "";
    }
    return (m->*list)[index].c_str();
}

std::optional<std::string> optionalCopy(const char *p)
{
    return p ? std::optional<std::string> {p} : std::nullopt;
}

} // namespace

sb_table_match_request_t sb_table_match_request_create(void)
{
    return new (std::nothrow) sb_table_match_request_struct();
}

void sb_table_match_request_free(sb_table_match_request_t request)
{
    delete request;
}

void sb_table_match_request_set_key(sb_table_match_request_t request, const char *key_type,
                                    const char *table_key)
{
    if (request) {
        request->keyType = key_type ? key_type : "";
        request->tableKey = table_key ? table_key : "";
    }
}

void sb_table_match_request_set_firmware_id(sb_table_match_request_t request,
                                            const char *firmware_id)
{
    if (request) {
        request->firmwareId = firmware_id ? firmware_id : "";
    }
}

void sb_table_match_request_set_signatures_json(sb_table_match_request_t request, const char *json)
{
    if (request) {
        request->signaturesJson = optionalCopy(json);
    }
}

void sb_table_match_request_set_signature_unavailable(sb_table_match_request_t request,
                                                      const char *code)
{
    if (request) {
        request->signatureUnavailable = optionalCopy(code);
    }
}

int sb_table_match_http_status(sb_table_match_t match)
{
    return match ? match->httpStatus : 0;
}

const char *sb_table_match_error_code(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::errorCode);
}

const char *sb_table_match_error_attr(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::errorAttr);
}

const char *sb_table_match_key_type(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::keyType);
}

const char *sb_table_match_table_key(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::tableKey);
}

const char *sb_table_match_mapping_id(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::mappingId);
}

const char *sb_table_match_venuemachine(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::venueMachine);
}

const char *sb_table_match_venuemachine_name(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::venueMachineName);
}

const char *sb_table_match_machine(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::machine);
}

const char *sb_table_match_machine_name(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::machineName);
}

bool sb_table_match_has_verdict(sb_table_match_t match)
{
    return match && match->hasVerdict;
}

const char *sb_table_match_verdict_status(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::verdictStatus);
}

const char *sb_table_match_verdict_accepted_version(sb_table_match_t match)
{
    return matchField(match, &sb_table_match_struct::verdictAcceptedVersion);
}

size_t sb_table_match_verdict_changed_sections_count(sb_table_match_t match)
{
    return match ? match->changedSections.size() : 0;
}

const char *sb_table_match_verdict_changed_section(sb_table_match_t match, size_t index)
{
    return listItem(match, &sb_table_match_struct::changedSections, index);
}

size_t sb_table_match_verdict_changed_procedures_count(sb_table_match_t match)
{
    return match ? match->changedProcedures.size() : 0;
}

const char *sb_table_match_verdict_changed_procedure(sb_table_match_t match, size_t index)
{
    return listItem(match, &sb_table_match_struct::changedProcedures, index);
}
