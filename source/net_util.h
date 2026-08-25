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

#include "game_data.h"
#include <scorbit_sdk/net_types.h>
#include <cpr/cpr.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scorbit {

struct DeviceInfo;

namespace detail {

struct UrlInfo {
    std::string protocol;
    std::string hostname;
    std::string port;
};

UrlInfo exctractHostAndPort(const std::string &url);

std::string removeSymbols(std::string_view str, std::string_view symbols);

std::string deriveUuid(const std::string &source);

std::string parseUuid(const std::string &str);

std::string gameHistoryToCsv(const GameHistory &history);

std::string to_iso8601(std::chrono::system_clock::time_point tp);

auto parseUrlUuid(const std::string &url, const std::string_view key) -> std::string;

cpr::SslOptions makeSslOptions();

/** True when @p url and @p hostname refer to the same host (scheme/port ignored for host compare).
 */
bool isHostMatching(const std::string &url, const std::string &hostname);

/**
 * Whether SDK auth headers should be attached to a download of @p resolvedUrl.
 * Uses API host match only. @p deviceInfo is intentionally not used for gating - updater and
 * non-Scorbitron integrations download authenticated artifacts from the configured API host.
 */
bool isInternalDownloadForAuth(const std::string &resolvedUrl, const std::string &apiHostname,
                               const ::scorbit::DeviceInfo &deviceInfo);

/** @return terminal error for leaderboard fetch, or nullopt if the request may proceed or defer. */
std::optional<Error> leaderboardRequestTerminalError(AuthStatus status);

/// What the authentication-status gate says about a request.
enum class AuthGate {
    Ready,    ///< The status is allowed; run the request now.
    Pending,  ///< Not allowed yet, but the status may still become allowed. Wait.
    Terminal, ///< It never will. Fail the request rather than waiting for something that
              ///< cannot happen.
};

/**
 * Decide whether a request restricted to @p allowedStatuses can run while the SDK is in @p status.
 *
 * Only the statuses that authentication is still working through count as Pending. Getting this
 * wrong in the Pending direction is what makes a request wait forever, so anything that is not
 * actively on its way to an allowed status is Terminal.
 *
 * @param stopping True once the SDK is shutting down; nothing is worth waiting for then.
 */
AuthGate authGate(AuthStatus status, const std::vector<AuthStatus> &allowedStatuses, bool stopping);

bool isLeaderboardContextReady(AuthStatus status, LeaderboardScope scope,
                               const std::string &machineUuid,
                               const std::optional<std::string> &variantUuid,
                               const std::optional<std::string> &gameSlug);

/// The Centrifugo client asks for a new token this long before the current one expires.
constexpr auto CF_CLIENT_REFRESH_BEFORE_EXPIRY = std::chrono::minutes {3};
/// Floor for the refresh delay, so a short-lived or malformed token can't spin the timer.
constexpr auto CF_TOKEN_REFRESH_MIN_DELAY = std::chrono::seconds {10};

/**
 * How long to wait before refreshing a cached Centrifugo JWT that expires in @p expiresIn.
 *
 * The result stays ahead of CF_CLIENT_REFRESH_BEFORE_EXPIRY whenever there is room for it, so the
 * client's synchronous token callback finds a usable token already in the cache instead of having
 * to fetch one on its own strand. Never returns less than CF_TOKEN_REFRESH_MIN_DELAY.
 */
std::chrono::seconds centrifugoTokenRefreshDelay(std::chrono::seconds expiresIn);

} // namespace detail
} // namespace scorbit
