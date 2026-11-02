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
#include <utility>

namespace scorbit {
namespace detail {

namespace {

constexpr int HTTP_OK_FIRST = 200;
constexpr int HTTP_OK_LAST = 299;
constexpr int HTTP_OK = 200;
constexpr int HTTP_NOT_MODIFIED = 304;
constexpr int HTTP_FORBIDDEN = 403;
constexpr int HTTP_NOT_FOUND = 404;
constexpr int HTTP_SERVER_ERROR = 500;

std::optional<nlohmann::json> parseObject(const std::string &body)
{
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    return j;
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
    // server_time moves on every reply; only the list and the outcome are news to the host.
    const bool changed = next.result != m_snapshot.result || next.etag != m_snapshot.etag
                      || next.rows != m_snapshot.rows;
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

            if (status < HTTP_OK_FIRST || status > HTTP_OK_LAST) {
                const auto result = switchMachineFailure(status, body);
                WRN("API switch machine to {} failed: HTTP {}, result {}", target, status,
                    static_cast<int>(result));
                m_steps.gate(false);
                finish(result, target, std::move(callback), true);
                return;
            }

            if (!current.empty() && current == target) {
                INF("API switch machine: already on {}", target);
                m_steps.gate(false);
                finish(SB_SWITCH_MACHINE_ALREADY_ATTACHED, target, std::move(callback), false);
                return;
            }

            const auto token = switchMachineToken(body);
            if (token.empty()) {
                ERR("API switch machine to {}: reply carries no token", target);
                m_steps.gate(false);
                finish(SB_SWITCH_MACHINE_FAILED, target, std::move(callback), true);
                return;
            }

            m_steps.adopt(target, token, [this, target, callback = std::move(callback)]() mutable {
                if (m_steps.stopping()) {
                    return;
                }
                INF("API switch machine: now on {}", target);
                m_steps.changed(target);
                m_steps.gate(false);
                finish(SB_SWITCH_MACHINE_OK, target, std::move(callback), false);
            });
        });
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
