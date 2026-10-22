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

#include "net_util.h"
#include "device_info.h"
#include "identifiers.h"
#include "fmt/format.h"
#include <logger/logger.h>
#include <boost/uuid.hpp>
#include <boost/url/url_view.hpp>
#include <boost/url/parse.hpp>
#include <cmrc/cmrc.hpp>
#include <nfc/probes_manager.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <regex>

CMRC_DECLARE(scorbit);

namespace scorbit {
namespace detail {

constexpr auto ABSOLUTE_MAX_PLAYERS_NUM = 6;

// Function to extract protocol, hostname, and port
UrlInfo exctractHostAndPort(const std::string &url)
{
    const auto r = boost::urls::parse_uri(url);
    if (!r) {
        ERR("Invalid URL: {}, {}", url, r.error().message());
        return {};
    }

    const auto u = *r;
    UrlInfo result;
    result.protocol = u.scheme();
    result.hostname = u.host();

    if (u.has_port()) {
        result.port = u.port();
    } else if (result.protocol == "https" || result.protocol == "wss") {
        result.port = "443"; // Default port for HTTPS and WSS
    } else if (result.protocol == "http" || result.protocol == "ws") {
        result.port = "80"; // Default port for HTTP (if not specified, we assume HTTP)
    } else {
        // Invalid scheme: accepted only http(s) or ws(s)
        result = {};
    }

    return result;
}

std::string removeSymbols(std::string_view str, std::string_view symbols)
{
    std::string result {str};
    for (const auto &symbol : symbols) {
        std::erase(result, symbol);
    }

    return result;
}

// Returns a name-based UUID version 5
std::string deriveUuid(const std::string &source)
{
    using namespace boost::uuids;
    name_generator_sha1 gen(ns::dns());
    return to_string(gen(source));
}

std::string parseUuid(const std::string &str)
{
    using namespace boost::uuids;
    string_generator gen;
    uuid u1;
    try {
        u1 = gen(str);
    } catch (const std::exception &e) {
        return {};
    }

    return to_string(u1);
}

std::string gameHistoryToCsv(const GameHistory &history)
{
    std::string rv;
    rv.reserve(50 * 1024);

    // CSV header: "time,p1,p2,p3,p4,p5,p6,player,ball,game_modes,completed_modes\n";
    rv.append("time");
    for (sb_player_t playerNum = 1; playerNum <= ABSOLUTE_MAX_PLAYERS_NUM; ++playerNum) {
        rv.append(fmt::format(",p{}", playerNum));
    }
    rv.append(",player,ball,game_modes,completed_modes\n");

    // CSV body
    for (const auto &data : history) {
        auto timestamp =
                std::chrono::duration_cast<std::chrono::seconds>(data.timestamp.time_since_epoch())
                        .count();

        std::string scores;
        for (sb_player_t playerNum = 1; playerNum <= ABSOLUTE_MAX_PLAYERS_NUM; ++playerNum) {
            if (data.players.count(playerNum) && data.players.at(playerNum).score() >= 0) {
                scores.append(fmt::format("{}", data.players.at(playerNum).score()));
            }
            scores.append(",");
        }

        std::string modes;
        if (!data.modes.isEmpty()) {
            modes = fmt::format("\"{}\"", data.modes.str());
        }

        // Modes completed at this very update; unlike game_modes it's not a state, but a one-shot
        // list of events, so it is normally empty.
        std::string completedModes;
        if (!data.completedModes.isEmpty()) {
            completedModes = fmt::format("\"{}\"", data.completedModes.str());
        }

        rv.append(fmt::format("{},{}{},{},{},{}\n", timestamp, scores, data.activePlayer, data.ball,
                              modes, completedModes));
    }

    return rv;
}

// Convert chrono timepoint to ISO 8601 string in UTC (e.g. "2023-10-05T14:48:00Z")
std::string to_iso8601(std::chrono::system_clock::time_point tp)
{
    // Convert to time_t (seconds since epoch)
    std::time_t t = std::chrono::system_clock::to_time_t(tp);

    // Convert to UTC
    std::tm tm = *std::gmtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

auto parseUrlUuid(const std::string &url, const std::string_view key) -> std::string
{
    // Example URL:
    // .../api/v2/scorbitrons/7a16ea98-48e8-4b2e-a1eb-cf282e3b81cc/sessions/da9e568d-ce3b-4493-9d5c-10cfe47a96de/
    // Regex to capture "v2/sessions" and the UUID
    std::regex re(fmt::format(R"(\/({})\/([0-9a-fA-F\-]+)\/)", key));
    std::smatch match;

    std::string uuid;
    if (std::regex_search(url, match, re)) {
        uuid = match[2]; // UUID
    }

    return uuid;
}

cpr::SslOptions makeSslOptions()
{
    auto fs = cmrc::scorbit::get_filesystem();
    auto certFile = fs.open("cacert.pem");
    cpr::SslOptions ssl;
    ssl.SetOption(cpr::ssl::CaBuffer {std::string(certFile.begin(), certFile.end())});
    ssl.SetOption(cpr::ssl::VerifyHost {true});
    return ssl;
}

bool isHostMatching(const std::string &url, const std::string &hostname)
{
    const auto urlResult = boost::urls::parse_uri(url);
    const auto hostResult = boost::urls::parse_uri(hostname);
    if (!urlResult || !hostResult) {
        return false;
    }
    return urlResult->host() == hostResult->host();
}

bool isInternalDownloadForAuth(const std::string &resolvedUrl, const std::string &apiHostname,
                               const scorbit::DeviceInfo &deviceInfo)
{
    (void)deviceInfo;
    return isHostMatching(resolvedUrl, apiHostname);
}

std::optional<Error> leaderboardRequestTerminalError(AuthStatus status)
{
    switch (status) {
    case AuthStatus::AuthenticationFailed:
        return Error::AuthFailed;
    case AuthStatus::AuthenticatedUnpaired:
        return Error::NotPaired;
    default:
        return std::nullopt;
    }
}

bool isLeaderboardContextReady(AuthStatus status, LeaderboardScope scope,
                               const std::string &machineUuid,
                               const std::optional<std::string> &variantUuid,
                               const std::optional<std::string> &gameSlug)
{
    if (status != AuthStatus::AuthenticatedPaired) {
        return false;
    }

    switch (scope) {
    case LeaderboardScope::Machine:
        return !machineUuid.empty();
    case LeaderboardScope::Variant:
        return variantUuid.has_value();
    case LeaderboardScope::Game:
        return gameSlug.has_value();
    default:
        return false;
    }
}

AuthGate authGate(AuthStatus status, const std::vector<AuthStatus> &allowedStatuses, bool stopping)
{
    if (std::ranges::find(allowedStatuses, status) != allowedStatuses.end()) {
        return AuthGate::Ready;
    }

    if (stopping) {
        return AuthGate::Terminal;
    }

    switch (status) {
    case AuthStatus::NotAuthenticated:
    case AuthStatus::Authenticating:
    case AuthStatus::AuthenticatedCheckingPairing:
        // Authentication is still in progress, so an allowed status may yet arrive.
        return AuthGate::Pending;

    case AuthStatus::AuthenticationFailed:
    case AuthStatus::AuthenticatedPaired:
    case AuthStatus::AuthenticatedUnpaired:
        // Settled on something this request does not allow. Waiting would achieve nothing: an
        // unpaired machine only becomes paired through a status change, which re-evaluates the
        // gate anyway.
        return AuthGate::Terminal;
    }

    return AuthGate::Terminal;
}

std::chrono::seconds centrifugoTokenRefreshDelay(std::chrono::seconds expiresIn)
{
    // One extra minute of headroom: refreshing exactly when the client asks would still leave it
    // reading a token that is about to expire.
    constexpr auto headroom = std::chrono::minutes {1};
    return std::max<std::chrono::seconds>(expiresIn - CF_CLIENT_REFRESH_BEFORE_EXPIRY - headroom,
                                          CF_TOKEN_REFRESH_MIN_DELAY);
}

std::optional<std::chrono::steady_clock::time_point>
diagProbeDeadline(int deadlineSeconds, std::chrono::steady_clock::time_point receivedAt)
{
    if (deadlineSeconds <= 0) {
        return std::nullopt;
    }
    return receivedAt + std::chrono::seconds {deadlineSeconds};
}

bool diagProbeDeadlinePassed(const std::optional<std::chrono::steady_clock::time_point> &deadline,
                             std::chrono::steady_clock::time_point now)
{
    return deadline.has_value() && now >= *deadline;
}

bool wifiIngestStatusEndsRun(int httpStatus)
{
    return httpStatus == HTTP_STATUS_GONE || httpStatus == HTTP_STATUS_NOT_FOUND;
}

bool isValidHardwareProbeRunId(std::string_view runId)
{
    constexpr size_t MAX_RUN_ID_LENGTH = 64;
    return !runId.empty() && runId.size() <= MAX_RUN_ID_LENGTH
        && std::all_of(runId.begin(), runId.end(),
                       [](char c) {
                           // Explicit ASCII: std::isalnum follows the host's locale.
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                               || (c >= '0' && c <= '9') || c == '-';
                       });
}

bool hardwareProbeReplyToMatches(std::string_view replyTo, const std::string &runId)
{
    const auto expected = fmt::format(fmt::runtime(URL_DIAGNOSTICS_HARDWARE_PROBE_RESULT_PATH),
                                      fmt::arg(ARG_RUN_ID, runId));
    if (replyTo.starts_with('/')) {
        replyTo.remove_prefix(1);
    }
    return replyTo == expected;
}

namespace {

// The pad hardware's capacitance scale factor in tenths of a femtofarad per raw count (2.8 fF).
// Kept integral so the reported value is exact and serializes without float noise.
constexpr int NFC_CAPACITANCE_DECI_FF_PER_COUNT {28};

// One name per firmware boot reason value
static_assert(std::size(JVAL_HWP_BOOT_REASONS) == nfc::NFC_BOOT_REASON_COUNT);

template <typename Bytes>
std::string toLowerHex(const Bytes &bytes)
{
    static constexpr char DIGITS[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(std::size(bytes) * 2);
    for (const uint8_t byte : bytes) {
        hex.push_back(DIGITS[byte >> 4]);
        hex.push_back(DIGITS[byte & 0x0F]);
    }
    return hex;
}

} // namespace

nlohmann::json probeFirmwareEntry(const std::optional<ProbeFirmware> &firmware)
{
    if (!firmware) {
        return nullptr;
    }
    // The API stores a signed 64-bit build and refuses the whole PATCH beyond it; a value that
    // large is a bad read anyway, so report the build as unknown.
    const auto build =
            firmware->timestampUtc <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
                    ? firmware->timestampUtc
                    : 0;
    // Named as in the hardware probe sections; a byte beyond the known reasons is unknown.
    const auto bootReason = size_t {firmware->bootReason} < std::size(JVAL_HWP_BOOT_REASONS)
                                  ? JVAL_HWP_BOOT_REASONS[firmware->bootReason]
                                  : JVAL_HWP_BOOT_REASONS[0];
    return {{JKEY_PFW_TIMESTAMP_UTC, build},
            {JKEY_PFW_UID, firmware->uid},
            {JKEY_PFW_BOOT_REASON, bootReason},
            {JKEY_PFW_WATCHDOG_COUNT, firmware->watchdogCount}};
}

ProbeFirmware nfcProbeFirmware(const nfc::ProbeInformation &probe)
{
    // As in the hardware probe section: legacy firmware (a string timestamp) reports no build
    // and no boot info.
    if (probe.timestampHeader != 0) {
        return {0, probe.uid, 0, 0};
    }
    return {probe.timestampUtc, probe.uid, probe.bootReason, probe.watchdogCount};
}

std::optional<nlohmann::json> nfcProbeFirmwareEntry(const nfc::NfcProbeInfoResult &result)
{
    using Status = nfc::NfcProbeInfoResult::Status;
    switch (result.status) {
    case Status::Ok:
        return probeFirmwareEntry(nfcProbeFirmware(result.probe));
    case Status::NotPresent:
        return probeFirmwareEntry(std::nullopt);
    case Status::ReadFailed:
        break;
    }
    return std::nullopt;
}

nlohmann::json buildHardwareProbeSdkPayload(const nfc::NfcProbeInfoResult &result,
                                            std::string_view sdkVersion)
{
    using Status = nfc::NfcProbeInfoResult::Status;

    nlohmann::json payload = nlohmann::json::object();
    payload[JKEY_HWP_SDK_VERSION] = sdkVersion;

    // Always present, for every status; 0 means no tap seen since the SDK started
    nlohmann::json extended = nlohmann::json::object();
    extended[JKEY_HWP_LAST_SEEN_TAP_ID] = result.lastSeenTapId;
    extended[JKEY_HWP_LAST_SEEN_TAP_UNIX_MS] = result.lastSeenTapUnixMs;

    if (result.status != Status::Ok) {
        payload[JKEY_HWP_NFC_STATUS] = result.status == Status::NotPresent
                                               ? JVAL_HWP_NFC_STATUS_NOT_PRESENT
                                               : JVAL_HWP_NFC_STATUS_READ_FAILED;
        payload[JKEY_HWP_NFC_EXTENDED] = std::move(extended);
        return payload;
    }

    const auto &probe = result.probe;
    const auto &nfcInfo = result.nfc;
    const bool isTag = (nfcInfo.flags & nfc::NfcFlagTag) != 0;

    payload[JKEY_HWP_NFC_STATUS] = JVAL_HWP_NFC_STATUS_OK;
    payload[JKEY_HWP_NFC_ID] = probe.id;
    payload[JKEY_HWP_NFC_NAME] = probe.name;
    payload[JKEY_HWP_NFC_VERSION_MAJOR] = probe.versionMajor;
    payload[JKEY_HWP_NFC_VERSION_MINOR] = probe.versionMinor;
    payload[JKEY_HWP_NFC_REVISION] = probe.versionRevision;
    payload[JKEY_HWP_NFC_TIMESTAMP] = probe.timestamp;
    payload[JKEY_HWP_NFC_MODE] = isTag ? JVAL_HWP_NFC_MODE_TAG : JVAL_HWP_NFC_MODE_READER;

    // Legacy firmware (string timestamp header) reports no boot reason or watchdog count
    const bool bootInfoValid = probe.timestampHeader == 0;
    const uint8_t bootReasonRaw = bootInfoValid ? probe.bootReason : 0;
    extended[JKEY_HWP_BOOT_INFO_VALID] = bootInfoValid;
    extended[JKEY_HWP_BOOT_REASON] = size_t {bootReasonRaw} < std::size(JVAL_HWP_BOOT_REASONS)
                                             ? JVAL_HWP_BOOT_REASONS[bootReasonRaw]
                                             : JVAL_HWP_BOOT_REASONS[0];
    extended[JKEY_HWP_BOOT_REASON_RAW] = bootReasonRaw;
    extended[JKEY_HWP_WATCHDOG_COUNT] = bootInfoValid ? probe.watchdogCount : 0;

    extended[JKEY_HWP_UPTIME_MS] = probe.timeMs;
    extended[JKEY_HWP_SYS_CLOCK_HZ] = probe.sysClockFrequency;
    extended[JKEY_HWP_USB_FLAGS] = probe.usbFlags;
    extended[JKEY_HWP_UID] = probe.uid;
    extended[JKEY_HWP_TIMESTAMP_UTC] = probe.timestampUtc;

    extended[JKEY_HWP_TPM_TYPE] = probe.tpmType;
    extended[JKEY_HWP_TPM_SERIAL] = toLowerHex(probe.tpmSerial);
    extended[JKEY_HWP_TPM_UUID] = toLowerHex(probe.tpmUuid);
    extended[JKEY_HWP_TPM_SCORBIT_SERIAL] = probe.tpmScorbitSerial;

    // RSSI is unmeasured only in tag mode with neither amplitude measurement enabled
    const bool measuresAmplitude = (nfcInfo.flags & nfc::NfcFlagMeasureAmplitude) != 0;
    const bool detectsField = (nfcInfo.flags & nfc::NfcFlagFieldDetector) != 0;
    extended[JKEY_HWP_FLAGS] = nfcInfo.flags;
    extended[JKEY_HWP_BRIGHTNESS_PCT] = nfcInfo.brightness;
    extended[JKEY_HWP_CAPACITANCE_RAW] = nfcInfo.capacitance;
    // Current firmware does not measure capacitance, so both read 0
    extended[JKEY_HWP_CAPACITANCE_FF] =
            nfcInfo.capacitance * NFC_CAPACITANCE_DECI_FF_PER_COUNT / 10.0;
    extended[JKEY_HWP_RSSI] = nfcInfo.rssi;
    extended[JKEY_HWP_RSSI_VALID] = !(isTag && !measuresAmplitude && !detectsField);

    // last_id is read-and-clear and polled by the SDK, so it is almost always 0
    extended[JKEY_HWP_LAST_ID] = nfcInfo.lastId;

    payload[JKEY_HWP_NFC_EXTENDED] = std::move(extended);
    return payload;
}

RecentIds::RecentIds(size_t capacity)
    : m_capacity(capacity)
{
}

bool RecentIds::insert(const std::string &id)
{
    if (!m_ids.insert(id).second) {
        return false;
    }
    m_order.push_back(id);
    if (m_order.size() > m_capacity) {
        m_ids.erase(m_order.front());
        m_order.pop_front();
    }
    return true;
}

nlohmann::json buildWifiSamplePayload(const wifi::Sample &sample)
{
    using nlohmann::json;

    const auto addInt = [](json &j, const char *key, const std::optional<int> &value) {
        if (value) {
            j[key] = *value;
        }
    };
    const auto addDouble = [](json &j, const char *key, const std::optional<double> &value) {
        if (value) {
            j[key] = *value;
        }
    };
    const auto addProbe = [&](json &j, const std::optional<wifi::ProbeResult> &probe,
                              const char *rttKey, const char *lossKey) {
        if (!probe) {
            return;
        }
        addInt(j, rttKey, probe->rttMs);
        addDouble(j, lossKey, probe->lossPct);
    };

    // `source` is a closed ChoiceField server-side and defaults to "wifi" when absent. Sending it
    // explicitly is what lets SB-3465's Ethernet sampler be distinguishable at all -- until this
    // field was sent, every sample was recorded as wifi regardless of the interface it came from.
    json j {{JKEY_DIAG_TS, to_iso8601(sample.ts)},
            {JKEY_DIAG_IS_FINAL, sample.isFinal},
            {JKEY_DIAG_SOURCE, sample.link.kind == wifi::InterfaceKind::Ethernet
                                       ? JVAL_DIAG_SOURCE_ETHERNET
                                       : JVAL_DIAG_SOURCE_WIFI}};

    addInt(j, JKEY_DIAG_RSSI_DBM, sample.link.rssiDbm);
    addInt(j, JKEY_DIAG_NOISE_DBM, sample.link.noiseDbm);
    addInt(j, JKEY_DIAG_LINK_RATE_MBPS, sample.link.linkRateMbps);
    addDouble(j, JKEY_DIAG_TX_RETRY_PCT, sample.link.txRetryPct);
    addInt(j, JKEY_DIAG_BEACON_LOSS_COUNT, sample.link.beaconLossCount);
    addInt(j, JKEY_DIAG_FREQ_MHZ, sample.link.freqMhz);
    addInt(j, JKEY_DIAG_CHANNEL, sample.link.channel);
    addProbe(j, sample.gateway, JKEY_DIAG_GATEWAY_RTT_MS, JKEY_DIAG_GATEWAY_LOSS_PCT);
    addProbe(j, sample.scorbit, JKEY_DIAG_SCORBIT_RTT_MS, JKEY_DIAG_SCORBIT_LOSS_PCT);
    addProbe(j, sample.publicInternet, JKEY_DIAG_PUBLIC_RTT_MS, JKEY_DIAG_PUBLIC_LOSS_PCT);

    // Omitted entirely when empty: the serializer defaults it to {} and the panel renders every
    // missing key as "unknown", so an empty dict and an absent one mean the same thing.
    if (!sample.dependencyChecks.empty()) {
        j[JKEY_DIAG_DEPENDENCY_CHECKS] = sample.dependencyChecks;
    }

    return j;
}

} // namespace detail
} // namespace scorbit
