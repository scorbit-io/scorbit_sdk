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

#include "device_info.h"
#include "download_options.h"
#include "game_data.h"
#include <scorbit_sdk/net_types.h>
#include <cpr/cpr.h>
#include <deque>
#include <diagnostics/wifi/wifi_diagnostics.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <ostream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace nfc {
struct NfcProbeInfoResult;
struct ProbeInformation;
} // namespace nfc

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

/**
 * Turn a diag_probe's @p deadlineSeconds into an absolute monotonic deadline measured from
 * @p receivedAt, or nullopt when the probe carries no deadline.
 *
 * The API sends deadline_seconds as a BUDGET (1..60), not an instant -- the payload has no send
 * timestamp. Measuring it from receipt on steady_clock is what makes device clock skew irrelevant
 * here: nothing is ever compared against a server wall-clock value. Zero, negative or absent means
 * "no deadline", so a device keeps working against an API build that does not send the field.
 */
std::optional<std::chrono::steady_clock::time_point>
diagProbeDeadline(int deadlineSeconds, std::chrono::steady_clock::time_point receivedAt);

/**
 * Whether @p deadline has elapsed by @p now. A probe with no deadline never expires.
 */
bool diagProbeDeadlinePassed(const std::optional<std::chrono::steady_clock::time_point> &deadline,
                             std::chrono::steady_clock::time_point now);

/**
 * Whether a capture ingest POST that came back with @p httpStatus ends the run on the device.
 *
 * 410 means the server closed the run. 404 means the server does not know the run, and it never
 * will -- a run minted elsewhere, or purged. Treating 404 as transient left a device posting every
 * sample and event into it for 3h45m on a 300 s capture (SB-4938). Everything else, including a
 * transport failure with no status at all, is transient and does not end the run.
 */
bool wifiIngestStatusEndsRun(int httpStatus);

/**
 * Whether a hardware probe run_id is safe to use as a URL path segment: alphanumerics and '-'
 * only, at most 64 characters. It arrives over the wire and ends up in the result POST path.
 */
bool isValidHardwareProbeRunId(std::string_view runId);

/**
 * Whether @p replyTo is the result path for @p runId, with or without a leading '/'. The SDK only
 * ever POSTs to that path, so a firmware_probe naming any other reply_to is refused.
 */
bool hardwareProbeReplyToMatches(std::string_view replyTo, const std::string &runId);

/// Whether @p id is an 8-4-4-4-12 hex UUID, the only run_id a probe_flash request may carry.
bool isUuid(std::string_view id);

/// @p text with ASCII letters lower-cased, so a UUID compares the same in either case.
std::string toLowerAscii(std::string_view text);

/**
 * Why a probe_flash payload is malformed, or nullopt when every field is well formed: probe is
 * cpu, dmd, nfc or link; target_build a 14-digit build; sha256 64 hex digits; size a positive
 * integer; deadline an ISO-8601 UTC time. run_id is checked separately.
 */
std::optional<std::string> probeFlashPayloadError(const nlohmann::json &payload);

/// @p deadline (as probeFlashPayloadError() accepts it) as a time point; nullopt if malformed.
std::optional<std::chrono::system_clock::time_point>
parseProbeFlashDeadline(std::string_view deadline);

/**
 * Whether a probe_flash result POST that ended with @p error and @p httpStatus (0: no response)
 * is resent. Only an API answer can be final: an attempt the auth gate stopped (e.g. NotPaired
 * while pairing is re-checked after a 401) is resent, whatever status it last saw.
 */
bool isProbeFlashResultRetryable(Error error, int httpStatus);

/// The wait before resending a probe_flash result after @p failures failed attempts (1-based).
std::chrono::seconds probeFlashResultBackoff(int failures);

/**
 * The SDK's section of a hardware probe result: the NFC probe read in @p result. On any status
 * other than Ok only sdk_version, nfc_status and nfc_extended's last_seen_tap_* are sent;
 * last_seen_tap_* are present for every status (0 = no tap seen).
 */
/**
 * A probe's probe_firmware entry: its build, UID, boot reason (by name) and watchdog count, or
 * null when it is not attached.
 */
nlohmann::json probeFirmwareEntry(const std::optional<ProbeFirmware> &firmware);

/** The NFC pad's firmware from its identity read; legacy firmware has no build or boot info. */
ProbeFirmware nfcProbeFirmware(const nfc::ProbeInformation &probe);

/**
 * The NFC pad's probe_firmware entry from a probe read: null when no pad is attached, nothing
 * when the read failed. Legacy firmware, which reports no build number, gets 0.
 */
std::optional<nlohmann::json> nfcProbeFirmwareEntry(const nfc::NfcProbeInfoResult &result);

nlohmann::json buildHardwareProbeSdkPayload(const nfc::NfcProbeInfoResult &result,
                                            std::string_view sdkVersion);

/**
 * The last @p capacity ids in arrival order, to refuse a Centrifugo history replay. Evicts the
 * oldest, never the id just added. Not thread-safe.
 */
class RecentIds
{
public:
    explicit RecentIds(size_t capacity);

    /// False if @p id is still remembered.
    bool insert(const std::string &id);

private:
    size_t m_capacity;
    std::deque<std::string> m_order;
    std::unordered_set<std::string> m_ids;
};

/**
 * Build the wifi-sample ingest body for @p sample.
 *
 * Extracted from Net purely so the payload SHAPE can be tested. The server's serializer silently
 * DROPS unknown keys -- a misspelled field is stored as NULL behind a 202, with no error on
 * either side. That is how noise_dbm went unsent for four months while every test passed, so the
 * key set is asserted in test_net_util rather than trusted.
 *
 * Optional metrics are omitted rather than sent as null: the serializer treats absent and null
 * alike, and omitting keeps the body under the 4 KiB cap.
 */
nlohmann::json buildWifiSamplePayload(const wifi::Sample &sample);

/// Whether an HTTP status is a redirect when it carries a Location: any 3xx but 304, as for curl.
bool isRedirectStatus(int httpStatus);

/// @p location from a redirect, resolved against the URL it came from; nullopt when unusable.
std::optional<std::string> resolveRedirect(std::string_view from, std::string_view location);

/// Whether @p options let a download redirected from @p from follow to @p to (both absolute).
bool redirectAllowed(const DownloadOptions &options, std::string_view from, std::string_view to);

/// Whether @p a and @p b share an origin: scheme, host (either in any case) and effective port.
/// The same URL always shares its own, parsed or not.
bool isSameOrigin(std::string_view a, std::string_view b);

/// @p url's host, or @p url itself when it has none, for naming a refused redirect.
std::string hostForReply(std::string_view url);

/**
 * Writes a download's body to @p out, at most @p maxBytes of it. Fed from curl's write callback,
 * it counts the bytes as written, after curl has decoded any Content-Encoding, so a compressed
 * body can't slip past the limit, and nothing beyond the limit is ever written.
 */
class DownloadSink
{
public:
    DownloadSink(std::ostream &out, uint64_t maxBytes);

    /// Writes @p data; false, which aborts the transfer, if that would pass the limit or the
    /// stream has failed.
    bool write(std::string_view data);
    bool limitHit() const { return m_limitHit; }
    uint64_t written() const { return m_written; }

private:
    std::ostream &m_out;
    uint64_t m_maxBytes;
    uint64_t m_written {0};
    bool m_limitHit {false};
};

} // namespace detail
} // namespace scorbit
