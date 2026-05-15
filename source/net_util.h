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

#include "game_data.h"
#include <scorbit_sdk/net_types.h>
#include <cpr/cpr.h>
#include <optional>
#include <string>
#include <string_view>

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

bool isLeaderboardContextReady(AuthStatus status, LeaderboardScope scope,
                               const std::string &machineUuid,
                               const std::optional<std::string> &variantUuid,
                               const std::optional<std::string> &gameSlug);

} // namespace detail
} // namespace scorbit
