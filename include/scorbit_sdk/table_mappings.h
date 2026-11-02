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
#include <string>
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
