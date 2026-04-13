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

#include "net_types_c.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace scorbit {

constexpr auto DIGEST_LENGTH = SB_DIGEST_LENGTH;
constexpr auto UUID_LENGTH = SB_UUID_LENGTH;
constexpr auto SIGNATURE_MAX_LENGTH = SB_SIGNATURE_MAX_LENGTH;
constexpr auto KEY_LENGTH = SB_KEY_LENGTH;

// IMPORTANT: always add new error codes at the end of the list and sync with sb_error_t
enum class Error {
    Success = SB_EC_SUCCESS,        // Success
    Unknown = SB_EC_UNKNOWN,        // Unknown error
    AuthFailed = SB_EC_AUTH_FAILED, // Authentication failed
    NotPaired = SB_EC_NOT_PAIRED,   // Device is not paired
    ApiError = SB_EC_API_ERROR,     // API call error (e.g., HTTP error code != 200)
    FileError = SB_EC_FILE_ERROR,   // File error (e.g., file not found)
};

enum class AuthStatus {
    NotAuthenticated = SB_NET_NOT_AUTHENTICATED,
    Authenticating = SB_NET_AUTHENTICATING,
    AuthenticatedCheckingPairing = SB_NET_AUTHENTICATED_CHECKING_PAIRING,
    AuthenticatedUnpaired = SB_NET_AUTHENTICATED_UNPAIRED,
    AuthenticatedPaired = SB_NET_AUTHENTICATED_PAIRED,
    AuthenticationFailed = SB_NET_AUTHENTICATION_FAILED,
};

enum class GameStartOrigin {
    StartButton = SB_GAME_STARTED_BY_BUTTON, // started by machine when player press Start button
    FromLobby = SB_GAME_STARTED_FROM_LOBBY,  // started explicitly via Scorbit app request
};

enum Capability : sb_capabilities_t {
    StartGame = SB_CAPABILITY_START_GAME,   // Game can be started remotely
    CreditDrop = SB_CAPABILITY_CREDIT_DROP, // Machine can accept coin drop events
};
using Capabilities = sb_capabilities_t;

using StringCallback = std::function<void(Error error, const std::string &reply)>;
using VectorCallback = std::function<void(Error error, const std::vector<uint8_t> &data)>;

/// HTTP header list for download functions.
using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

// Key persistence callbacks
using SaveKeyCallback = std::function<void(const std::string &key)>;
using LoadKeyCallback = std::function<std::string()>;

} // namespace scorbit
