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

#include "net.h"
#include "net_util.h"
#include "soft_key_resolver.h"
#include "fmt_formatters.h"
#include <logger/logger.h>
#include "updater.h"
#include "safe_multipart.h"
#include "utils/machine_fingerprint.h"
#include "utils/date_time_parser.h"
#include "utils/jwt_parser.h"
#include "utils/archiver.h"
#include <diagnostics/lan_ip.h>
#include <utils/bytearray.h>
#include <scorbit_sdk/net_types.h>
#include <scorbit_sdk/version.h>
#include <nfc/probes_manager.h>
#include <cmrc/cmrc.hpp>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <openssl/sha.h>
#include <cpr/cpr.h>
#include <boost/uuid.hpp>
#include <boost/filesystem.hpp>
#include <boost/url/url_view.hpp>
#include <boost/url/parse.hpp>
#include <algorithm>
#include <ranges>
#include <fstream>
#include <optional>
#include <future>

CMRC_DECLARE(scorbit);

using namespace std;
using namespace std::chrono_literals;
using namespace std::chrono;
namespace fs = boost::filesystem;
using json = nlohmann::json;

namespace scorbit {
namespace detail {

// constexpr auto HEARTBEAT_URL {"heartbeat/"};
constexpr auto SESSION_CSV_URL {"session_log/"};

constexpr auto PAIRING_DEEPLINK {"https://scorbit.link/"
                                 "qrcode?$deeplink_path={manufacturer_prefix}"
                                 "&machineid={scorbit_machine_id}&uuid={scorbitron_uuid}"};

constexpr auto NET_TIMEOUT = 14s;
/// Connect-phase limit for large uploads/downloads (TLS + TCP); separate from transfer duration.
constexpr auto NET_CONNECT_TIMEOUT = 60s;
/// No hard cap on total transfer time; stalled transfers abort via NET_TRANSFER_LOW_SPEED_*.
constexpr auto NET_TRANSFER_TOTAL_TIMEOUT = 0ms;
constexpr std::int32_t NET_TRANSFER_LOW_SPEED_BPS = 32;
constexpr auto NET_TRANSFER_LOW_SPEED_STALL_TIME = 120s;
constexpr auto HEARTBEAT_TIME = 10s;
constexpr auto NUM_RETRIES = 3;

constexpr auto REFRESH_TOKEN_BEFORE_EXPIRY = 5min; // Refresh token when 5 minutes remain

constexpr size_t DIAG_MAX_LOGS = 5;
constexpr size_t DIAG_MAX_RECORDINGS = 2;
constexpr uintmax_t DIAG_MAX_LOG_SIZE = 10 * 1024 * 1024 + 100;       // 10 MB
constexpr uintmax_t DIAG_MAX_RECORDING_SIZE = 20 * 1024 * 1024 + 100; // 20 MB
constexpr size_t DIAG_MAX_LOG_STRING_SIZE = 10 * 1024 * 1024 + 100;   // 10 MB

constexpr auto GAME_DATA_UPDATE_INTERVAL = 2000ms;
constexpr auto SESSION_UPDATE_ADD_PLAYER_DEBOUNCE = 300ms;
constexpr auto SESSION_UPDATE_NO_UUID_RETRY = 1000ms;
constexpr auto TOP_SCORES_DEFER_RETRY = 1000ms;
constexpr int TOP_SCORES_DEFER_MAX_ATTEMPTS = 30;
constexpr auto CF_RETIRED_CLIENT_GRACE_PERIOD = 30s;

constexpr auto NFC_CHECK_TIME = 2000ms;    // Check NFC nonces every 1000 milliseconds
constexpr auto NFC_BOOT_REASON_DELAY = 5s; // Check NFC boot reason every 5 seconds

constexpr auto MAX_BUFFER_DOWNLOAD_SIZE = 10 * 1024 * 1024; // 10 MB max size to download to memory
constexpr auto PICTURE_BUFFER_RESERVE = 300 * 1024;         // 300 KB reserve for picture download

constexpr auto MAX_SYSTEM_TIME_DRIFT_SECONDS = 20;

auto noop_task = []() { };

std::optional<std::string_view> leaderboardPeriodParam(LeaderboardPeriod period)
{
    switch (period) {
    case LeaderboardPeriod::AllTime:
        return std::string_view {};
    case LeaderboardPeriod::Days14:
        return "14d";
    case LeaderboardPeriod::Days30:
        return "30d";
    case LeaderboardPeriod::Days90:
        return "90d";
    case LeaderboardPeriod::Days180:
        return "180d";
    case LeaderboardPeriod::Days365:
        return "365d";
    }

    return std::nullopt;
}

std::string elideUrl(const std::string &url, size_t keep = 20)
{
    if (url.size() <= keep * 2 + 3) {
        return url;
    }
    return url.substr(0, keep) + "..." + url.substr(url.size() - keep);
}

string getSignature(const SignerCallback &signer, const std::string &uuid,
                    const std::string &timestamp)
{
    utils::ByteArray message(removeSymbols(uuid, "-{}"));
    utils::ByteArray timestampBytes(timestamp.cbegin(), timestamp.cend());
    message.insert(message.end(), timestampBytes.cbegin(), timestampBytes.cend());

    Digest digest;
    SHA256(message.data(), message.size(), digest.data());

    Signature signature = signer(digest);
    if (signature.empty()) {
        ERR("Can't sign message, signer callback returned error");
        return string {};
    }

    return utils::ByteArray(signature).hex();
}

std::string getJwtToken(const std::string &url, const std::string &authToken,
                        const cpr::SslOptions &&sslOptions)
{
    INF("API-CF getting JWT token from: {}", url);

    // Note: This is synchronous as required by centrifugo library callback
    const auto r = cpr::Get(cpr::Url {url},
                            cpr::Header {{HDR_KEY_AUTHORIZATION, HDR_VAL_BEARER + authToken}},
                            cpr::Timeout {NET_TIMEOUT}, sslOptions);

    if (r.status_code != 200) {
        ERR("API-CF failed to get JWT token: HTTP {} - {}", r.status_code, r.error.message);
        return {};
    }

    try {
        const auto json = json::parse(r.text);
        if (const auto it = json.find(JKEY_CF_TOKEN); it != json.end() && it->is_string()) {
            const auto token = it->get<std::string>();
            INF("API-CF received JWT token successfully");
            return token;
        }
        ERR("API-CF JWT token not found in response");
    } catch (const std::exception &e) {
        ERR("API-CF failed to parse JWT token response: {}", e.what());
    }

    return {};
}

// --------------------------------------------------------------------------------

Net::Net(DeviceInfo deviceInfo, std::vector<std::unique_ptr<IKeyResolver>> resolvers)
    : m_keyResolvers(std::move(resolvers))
    , m_deviceInfo(std::move(deviceInfo))
    , m_updater(*this, m_deviceInfo.usesEncryptedKey(), m_deviceInfo.scorbitdVersion,
                m_deviceInfo.scorbitdPlatformId)
    , m_worker(m_deviceInfo.threadsNice)
    , m_eventManager(std::make_shared<EventManager>(m_worker.eventsStrand(),
                                                    std::move(m_deviceInfo.m_eventCallback)))
{
    setHostname(m_deviceInfo.hostname, m_deviceInfo.cfHostname);

    if (!validateDeviceInfo()) {
        return;
    }

    // Collect fingerprints once for use in provisioning and authentication
    m_fingerprint = collectFingerprints(m_deviceInfo.extraFingerprint);
    m_fingerprintHash = m_fingerprint.computeHash();
    INF("API fingerprint hash: {}", m_fingerprintHash);

    initScorbitronObject();
    centrifugoSetup();
    m_worker.start();
}

bool Net::validateDeviceInfo() const
{
    if (m_deviceInfo.provider.empty()) {
        ERR("Provider is not set");
        return false;
    }

    if (m_deviceInfo.provider != PROVIDER_SCORBITRON
        && m_deviceInfo.provider != PROVIDER_VSCORBITRON) {
        if (m_deviceInfo.machineId == 0) {
            ERR("Machine ID not set");
            return false;
        }
    }

    return true;
}

bool Net::resolveKeys(const std::string &serverTimestamp)
{
    // Provide the normalized hostname to resolvers so provisioning can use it directly
    m_deviceInfo.hostname = m_hostname;

    for (auto &resolver : m_keyResolvers) {
        if (resolver->tryResolve(m_deviceInfo, serverTimestamp)) {
            m_signer = resolver->createSigner();
            m_keyResolvers.clear();

            if (!m_deviceInfo.uuid.empty()) {
                const auto originalUuid = m_deviceInfo.uuid;
                m_deviceInfo.uuid = parseUuid(originalUuid);
                if (m_deviceInfo.uuid.empty()) {
                    ERR("Key resolver set invalid UUID: {}", originalUuid);
                    return false;
                }
            }

            INF("Key resolution succeeded, serial: {}, uuid: {}", m_deviceInfo.serialNumber,
                m_deviceInfo.uuid);
            return true;
        }
    }

    ERR("No authentication resolver succeeded");
    m_keyResolvers.clear();
    return false;
}

bool Net::reprovisionSoftKey(const std::string &serverTimestamp)
{
    if (!m_deviceInfo.hasSoftKeyProvisioning()) {
        return false;
    }

    INF("Scorbitron not found in API, clearing stale key and re-provisioning...");

    // Clear the stale saved key so the next load returns empty
    m_deviceInfo.saveKeyCallback("");

    m_deviceInfo.hostname = m_hostname;
    m_signer = {};

    SoftKeyResolver resolver;
    if (!resolver.tryResolve(m_deviceInfo, serverTimestamp)) {
        ERR("Re-provisioning failed");
        return false;
    }

    if (!m_deviceInfo.uuid.empty()) {
        const auto originalUuid = m_deviceInfo.uuid;
        m_deviceInfo.uuid = parseUuid(originalUuid);
        if (m_deviceInfo.uuid.empty()) {
            ERR("Re-provisioned key resolver set invalid UUID: {}", originalUuid);
            return false;
        }
    }

    m_signer = resolver.createSigner();

    INF("Re-provisioning succeeded, new uuid={}", m_deviceInfo.uuid);
    return true;
}

Net::~Net()
{
    if (m_networkMonitor) {
        m_networkMonitor->stop("shutdown");
        m_networkMonitor.reset();
    }

    if (!m_stop.exchange(true)) {
        stopHeartbeatTimer();
        stopTokenRefreshTimer();
        m_eventManager->stop();
        m_authCV.notify_all();
        m_shortCodeCV.notify_all();
    }

    // Disconnect centrifugo on its strand to stop new I/O but do NOT destroy it here.
    // Transport must stay alive so pending async handlers (async_close, timer cancels,
    // async_read completion) don't access freed memory.
    if (m_worker.isRunning() && m_centrifugo) {
        auto done = std::make_shared<std::promise<void>>();
        auto wait = done->get_future();
        m_worker.post([this, done]() {
            if (m_centrifugo) {
                m_centrifugo->disconnect();
            }
            done->set_value();
        });
        wait.wait();
    }

    // Drains all queued handlers while Transport is still alive.
    m_worker.stop();

    // After worker stops, io_context is idle and all threads joined.
    // m_centrifugo is destroyed safely by member destruction order.
}

AuthStatus Net::status() const
{
    return m_status;
}

const string &Net::hostname() const
{
    return m_hostname;
}

const string &Net::cfHostname() const
{
    return m_cfHostname;
}

void Net::setHostname(std::string hostname, std::string cfHostname)
{
    if (hostname == PRODUCTION_LABEL || hostname.empty()) {
        hostname = PRODUCTION_HOSTNAME;
        if (cfHostname.empty()) {
            cfHostname = PRODUCTION_CENTRIFUGO;
        }
    } else if (hostname == STAGING_LABEL) {
        hostname = STAGING_HOSTNAME;
        if (cfHostname.empty()) {
            cfHostname = STAGING_CENTRIFUGO;
        }
    } else if (cfHostname.empty()) {
        // Default centrifugo URL if not specified
        cfHostname = hostname;
    }

    {
        const auto host = exctractHostAndPort(hostname);
        m_hostname = fmt::format("{}://{}:{}", host.protocol, host.hostname, host.port);
    }

    {
        auto host = exctractHostAndPort(cfHostname);
        if (cfHostname == hostname) {
            if (host.protocol == PROTO_HTTPS) {
                host.protocol = PROTO_WSS;
            } else if (host.protocol == PROTO_HTTP) {
                host.protocol = PROTO_WS;
            }
        }
        m_cfHostname = fmt::format("{}://{}:{}", host.protocol, host.hostname, host.port);
    }
}

bool Net::isAuthenticated() const
{
    return m_status == AuthStatus::AuthenticatedPaired
        || m_status == AuthStatus::AuthenticatedUnpaired;
}

void Net::authenticate()
{
    INF("API post authentication");
    m_worker.post(createAuthenticateTask());
}

void Net::updateConfig(const std::string &type, const std::string &version, bool installed,
                       std::optional<string> log)
{
    INF("API post queue installed, type: {}, version: {}", type, version);
    m_worker.postQueue(updateConfigTask(type, version, std::move(installed), std::move(log)));
}

void Net::sessionCreate(const GameData &data, GameStartOrigin origin,
                        std::function<void()> onCreated)
{
    {
        std::scoped_lock lock(m_gameSessionsMutex);
        auto &session = m_gameSessions[data.id];
        session.gameData = data;
        session.history.push_back(data);
    }

    INF("API post create session, id: {}", data.id);
    m_worker.postSessionQueue(createSessionCreateTask(data.id, origin, std::move(onCreated)));
}

void Net::submitGameData(const GameData &data, SessionFlags flags)
{
    // Queue in worker, so that it will not block the caller while waiting for lock
    m_worker.post([this, data, flags]() {
        int sessionCounterAfterUpdate = 0;
        {
            std::scoped_lock lock(m_gameSessionsMutex);
            auto &session = m_gameSessions[data.id];
            session.gameData = data;
            session.history.push_back(data);
            sessionCounterAfterUpdate = session.sessionCounter;
        }

        const auto sessionId = data.id;

        // If this is first data submission or it's finished send data right away
        if (!data.isGameActive || sessionCounterAfterUpdate == 1) {
            sendLatestGameData(sessionId);
        }

        if (flags) {
            sessionUpdate(sessionId, flags);
        }
    });
}

void Net::sendHeartbeat()
{
    return; // FIXME: disable heartbeat for now
    // Ensure that only single task in the queue (while another can be running)
    if (!m_isHeartbeatInQueue.exchange(true)) {
        INF("API post heartbeat");
        m_worker.postHeartbeatQueue(createHeartbeatTask());
    }
}

void Net::getConfig()
{
    INF("API get config");
    m_worker.post(createGetRequestTask(
            [this](Error error, const std::string &reply) {
                if (error != Error::Success) {
                    WRN("API get config error: {}", static_cast<int>(error));
                    return;
                }
                INF("API get config: {}", reply);

                try {
                    json json = json::parse(reply);

                    if (const auto it = json.find(JKEY_SCFG_VARIANT_ID);
                        it != json.end() && it->is_string()) {
                        m_machineInfo.variantUuid = it->get<std::string>();
                    }

                    if (const auto it = json.find(JKEY_SCFG_VENUE_ID);
                        it != json.end() && it->is_string()) {
                        m_machineInfo.venueUuid = it->get<std::string>();
                    }

                    if (const auto configIt = json.find(JKEY_SCFG_CONFIG);
                        configIt != json.end() && configIt->is_object()) {
                        if (const auto it = configIt->find(JKEY_SCFG_OPDB_ID);
                            it != configIt->end() && it->is_string()) {
                            it->get_to(m_machineInfo.opdbId);
                        }

                        if (const auto it = configIt->find(JKEY_SCFG_MACHINE_ID);
                            it != configIt->end() && it->is_number_integer()) {
                            it->get_to(m_deviceInfo.machineId);
                        }
                    }

                    m_eventManager->push(std::make_shared<ConfigReceivedEvent>(json));

                    if (const auto pricingIt = json.find(JKEY_SCFG_PRICING);
                        pricingIt != json.end() && pricingIt->is_object()) {
                        m_eventManager->push(std::make_shared<PricingReceivedEvent>(*pricingIt));
                    }

                } catch (const std::exception &e) {
                    ERR("API error parsing config reply: {}", e.what());
                }
            },
            [this]() {
                const auto endpoint = url(URL_SCORBITRON_CONFIG);
                cpr::Parameters parameters;
                return make_tuple(endpoint, parameters);
            },
            {
                    AuthStatus::AuthenticatedUnpaired,
                    AuthStatus::AuthenticatedPaired,
            }));
}

void Net::requestPairCode(StringCallback callback)
{
    std::string shortCodeCopy;
    {
        std::scoped_lock lock(m_shortCodeMutex);
        shortCodeCopy = m_cachedShortCode;
    }

    if (!shortCodeCopy.empty()) {
        callback(Error::Success, shortCodeCopy);
        return;
    }

    // If shortcode is not cached, wait for it to be received
    m_worker.postQueue([this, callback = std::move(callback)]() {
        std::unique_lock lock(m_shortCodeMutex);
        m_shortCodeCV.wait(lock, [this] { return !m_cachedShortCode.empty() || m_stop; });

        if (m_stop) {
            callback(Error::ApiError, "");
            return;
        }

        if (!m_cachedShortCode.empty()) {
            callback(Error::Success, m_cachedShortCode);
        } else {
            callback(Error::ApiError, "");
        }
    });
}

const string &Net::getMachineUuid() const
{
    return m_deviceInfo.uuid;
}

std::uint64_t Net::getMachineSerial() const
{
    return m_deviceInfo.serialNumber;
}

const string &Net::getPairDeeplink() const
{
    m_cachedPairDeeplink =
            fmt::format(PAIRING_DEEPLINK, fmt::arg("manufacturer_prefix", m_deviceInfo.provider),
                        fmt::arg("scorbit_machine_id", m_deviceInfo.machineId),
                        fmt::arg("scorbitron_uuid", m_deviceInfo.uuid));
    return m_cachedPairDeeplink;
}

const DeviceInfo &Net::deviceInfo() const
{
    return m_deviceInfo;
}

bool Net::isLeaderboardContextReady(LeaderboardScope scope) const
{
    return detail::isLeaderboardContextReady(m_status, scope, m_machineInfo.machineUuid,
                                             m_machineInfo.variantUuid, m_machineInfo.gameSlug);
}

std::optional<Error> Net::leaderboardRequestTerminalError() const
{
    return detail::leaderboardRequestTerminalError(m_status);
}

void Net::requestTopScores(LeaderboardScope scope, LeaderboardPeriod period,
                           const std::string &since, LeaderboardVpinFilter vpinFilter,
                           LeaderboardHandleCallback callback)
{
    requestTopScoresImpl(scope, period, since, vpinFilter, std::move(callback), 0);
}

void Net::requestTopScoresImpl(LeaderboardScope scope, LeaderboardPeriod period,
                               const std::string &since, LeaderboardVpinFilter vpinFilter,
                               LeaderboardHandleCallback callback, int deferAttempt)
{
    m_worker.postQueue([this, scope, period, since, vpinFilter, deferAttempt,
                        callback = std::move(callback)]() mutable {
        if (scope != LeaderboardScope::Machine && scope != LeaderboardScope::Variant
            && scope != LeaderboardScope::Game) {
            callback(Error::ApiError, nullptr);
            return;
        }

        std::string_view periodParam;
        if (since.empty()) {
            const auto mappedPeriod = leaderboardPeriodParam(period);
            if (!mappedPeriod.has_value()) {
                callback(Error::ApiError, nullptr);
                return;
            }
            periodParam = *mappedPeriod;
        }

        if (vpinFilter != LeaderboardVpinFilter::Any
            && vpinFilter != LeaderboardVpinFilter::VpinOnly
            && vpinFilter != LeaderboardVpinFilter::RealOnly) {
            callback(Error::ApiError, nullptr);
            return;
        }

        if (const auto terminalError = leaderboardRequestTerminalError()) {
            callback(*terminalError, nullptr);
            return;
        }

        if (!isLeaderboardContextReady(scope)) {
            if (deferAttempt >= TOP_SCORES_DEFER_MAX_ATTEMPTS) {
                ERR("API request top scores gave up after {} defer attempts (scope={})",
                    TOP_SCORES_DEFER_MAX_ATTEMPTS, static_cast<int>(scope));
                callback(Error::ApiError, nullptr);
                return;
            }

            INF("API request top scores deferred (scope={}), attempt {}/{}, retry in {}ms...",
                static_cast<int>(scope), deferAttempt + 1, TOP_SCORES_DEFER_MAX_ATTEMPTS,
                std::chrono::duration_cast<std::chrono::milliseconds>(TOP_SCORES_DEFER_RETRY)
                        .count());
            m_worker.startTimer(Worker::Timer::LeaderboardDeferred, TOP_SCORES_DEFER_RETRY,
                                [this, scope, period, since, vpinFilter, deferAttempt,
                                 callback = std::move(callback)]() mutable {
                                    requestTopScoresImpl(scope, period, since, vpinFilter,
                                                         std::move(callback), deferAttempt + 1);
                                });
            return;
        }

        auto deferredSetup = [this, scope, periodParam, since,
                              vpinFilter]() -> std::tuple<cpr::Url, cpr::Parameters> {
            cpr::Parameters parameters;
            if (!since.empty()) {
                parameters.Add({"since", since});
            } else if (!periodParam.empty()) {
                parameters.Add({"period", std::string {periodParam}});
            }

            switch (vpinFilter) {
            case LeaderboardVpinFilter::Any:
                break;
            case LeaderboardVpinFilter::VpinOnly:
                parameters.Add({"vpin", "true"});
                break;
            case LeaderboardVpinFilter::RealOnly:
                parameters.Add({"vpin", "false"});
                break;
            }

            switch (scope) {
            case LeaderboardScope::Machine:
                return make_tuple(url(URL_MACHINE_LEADERS), std::move(parameters));
            case LeaderboardScope::Variant:
                return make_tuple(url(URL_VARIANT_LEADERS,
                                      fmt::arg(ARG_VARIANT_UUID, *m_machineInfo.variantUuid)),
                                  std::move(parameters));
            case LeaderboardScope::Game:
                return make_tuple(
                        url(URL_GAME_LEADERS, fmt::arg(ARG_GAME_SLUG, *m_machineInfo.gameSlug)),
                        std::move(parameters));
            default:
                return make_tuple(url(URL_MACHINE_LEADERS), std::move(parameters));
            }
        };

        auto replyCallback = [callback = std::move(callback)](Error error,
                                                              std::string reply) mutable {
            if (error != Error::Success) {
                callback(error, nullptr);
                return;
            }

            std::unique_ptr<sb_leaderboard_t> leaderboard;
            try {
                leaderboard.reset(parseLeaderboardJson(reply));
            } catch (const std::exception &e) {
                ERR("API request top scores parse error: {}, reply: {}", e.what(), reply);
                callback(Error::ApiError, nullptr);
                return;
            }

            if (!leaderboard) {
                ERR("API request top scores parse failed, reply: {}", reply);
                callback(Error::ApiError, nullptr);
                return;
            }

            callback(Error::Success, leaderboard.release());
        };

        createGetRequestTask(std::move(replyCallback), std::move(deferredSetup))();
    });
}

void Net::requestUnpair(StringCallback callback)
{
    json j {
            {JKEY_SCFG_SCORBITRON_MACHINE, nullptr},
    };

    patchScorbitron(j.dump(),
                    [this, callback = std::move(callback)](Error error, std::string reply) {
                        if (error == Error::Success || error == Error::NotPaired) {
                            m_status = AuthStatus::AuthenticatedUnpaired;
                            error = Error::Success;
                        }
                        callback(error, reply);
                    });
}

void Net::download(bool isAsync, StringCallback callback, const std::string &url,
                   const std::string &filename, const HttpHeaders &headers)
{
    if (isAsync) {
        m_worker.postQueue(
                createDownloadFileTask(std::move(callback), url, filename, HttpHeaders(headers)));
    } else {
        std::invoke(
                createDownloadFileTask(std::move(callback), url, filename, HttpHeaders(headers)));
    }
}

void Net::downloadBuffer(bool isAsync, VectorCallback callback, const std::string &url,
                         size_t reserveBufferSize, const HttpHeaders &headers)
{
    if (isAsync) {
        m_worker.postQueue(createDownloadBufferTask(std::move(callback), url, reserveBufferSize,
                                                    HttpHeaders(headers)));
    } else {
        std::invoke(createDownloadBufferTask(std::move(callback), url, reserveBufferSize,
                                             HttpHeaders(headers)));
    }
}

PlayerProfilesManager &Net::playersManager()
{
    return m_playersManager;
}

void Net::patchScorbitron(std::string body, StringCallback callback,
                          std::vector<AuthStatus> allowedStatuses)
{
    m_worker.post(createPatchRequestTask(
            [this, callback = std::move(callback)](Error error, std::string reply) {
                parseScorbitronObject(error, reply);
                callback(error, reply);
            },
            [this, body = std::move(body)]() {
                const auto endpoint = url(URL_SCORBITRON_OBJECT);
                INF("API patching Scorbitron with {}", body);

                return make_tuple(endpoint, cpr::Body {body});
            },
            std::move(allowedStatuses)));
}

std::string Net::consumeNonce()
{
    std::scoped_lock lock(m_noncesMutex);

    if (m_nonces.empty()) {
        WRN("No NFC nonces available");
        createNfcNonces();
        return {}; // RVO applies here
    }

    // 1. If there are 10 nonces, then create new ones. This is normal path, so we avoid double
    //    creating nonces if it was 10 and then quickly 9, while already creating nonces
    // 2. Safety net, if there are 5 or less nonces, create more even if it's double creating
    if (m_nonces.size() == 10 || m_nonces.size() <= 5) {
        INF("NFC nonces running low, creating more");
        createNfcNonces();
    }

    auto nonce = std::move(m_nonces.back());
    m_nonces.pop_back();
    return nonce;
}

void Net::setProbesManager(std::shared_ptr<nfc::ProbesManager> manager)
{
    m_probesManager = manager;
    m_isNfcCapable = (m_probesManager && m_probesManager->nfc());

    {
        std::scoped_lock lock(m_scorbitronObjectMutex);
        m_scorbitronObject[JKEY_SOBJ_NFC_CAPABLE] = m_isNfcCapable;
    }

    if (m_isNfcCapable) {
        startNfcCheckTimer();
    }

    checkNfcBootReason();
    updateDiscoveryDescription();
}

void Net::requestPairMachine(const std::string &machineUuid, const std::string &ownerUuid,
                             StringCallback callback)
{
    json j {
            {JKEY_SCFG_SCORBITRON_MACHINE, machineUuid},
            {JKEY_SCFG_OWNER, ownerUuid},
    };

    patchScorbitron(j.dump(),
                    [this, callback = std::move(callback)](Error error, const std::string &reply) {
                        if (error == Error::Success) {
                            initializeConnectionState();
                        }
                        if (callback) {
                            callback(error, reply);
                        }
                    },
                    {AuthStatus::AuthenticatedUnpaired, AuthStatus::AuthenticatedPaired});
}

void Net::setCapabilities(Capabilities capabilities)
{
    bool startGame = capabilities & Capability::StartGame;
    bool creditDrop = capabilities & Capability::CreditDrop;

    {
        // Update scorbitron object, so it can be sent once authenticated
        std::scoped_lock lock(m_scorbitronObjectMutex);
        m_scorbitronObject[JKEY_SOBJ_START_GAME_CAPABLE] = startGame;
        m_scorbitronObject[JKEY_SOBJ_CREDIT_DROP_CAPABLE] = creditDrop;
    }

    if (!isAuthenticated()) {
        return;
    }

    // Send updated capabilities right away, because it's already authenticated
    json j {
            {JKEY_SOBJ_START_GAME_CAPABLE, startGame},
            {JKEY_SOBJ_CREDIT_DROP_CAPABLE, creditDrop},
    };

    patchScorbitron(j.dump(),
                    [](Error error, std::string reply) {
                        if (error == Error::Success) {
                            INF("API set capabilities: ok, {}", reply);
                        } else {
                            ERR("API set capabilities: failed, error code: {}, reply: {}",
                                static_cast<int>(error), reply);
                        }
                    },
                    {
                            AuthStatus::AuthenticatedUnpaired,
                            AuthStatus::AuthenticatedPaired,
                    });
}

void Net::setCreditsDropped(int credits, const std::string &transaction, bool success)
{
    json j {
            {JKEY_CREDITS_DROPPED, credits},
            {JKEY_CREDITS_TRANSACTION, transaction},
            {JKEY_CREDITS_SUCCESS, success},
    };

    m_worker.post(createPostRequestTask(
            [](Error error, std::string reply) {
                if (error == Error::Success) {
                    INF("API credits dropped: ok, {}", reply);
                } else {
                    ERR("API credits dropped: failed, error code: {}, reply: {}",
                        static_cast<int>(error), reply);
                }
            },
            [this, body = j.dump()]() {
                INF("API sending credits dropped: {}", body);
                return std::make_tuple(url(URL_SCORBITRON_CREDIT_DROP_CREATE), cpr::Body {body});
            }));
}

void Net::setCreditsStatus(bool freePlay, int credits, int maxCredits, const char * /*pricing*/)
{
    m_worker.post([this, freePlay, credits, maxCredits]() {
        if (m_stop || !m_centrifugo) {
            return;
        }

        const auto createdAt = to_iso8601(chrono::system_clock::now());

        json j {{JKEY_CHN_TYPE, JVAL_CURRENT_MACHINE_STATE},
                {JKEY_CHN_PAYLOAD,
                 {
                         {JKEY_CREDITS_FREE_PLAY, freePlay},
                 }},
                {JKEY_SCR_METADATA,
                 {
                         {JKEY_SCR_MACHINE, m_machineInfo.machineUuid},
                         {JKEY_SCR_VARIANT, m_machineInfo.variantUuid},
                         {JKEY_SCR_VENUE, m_machineInfo.venueUuid},
                         {JKEY_SCR_CREATED_AT, createdAt},
                         {JKEY_SCR_UPDATED_AT, createdAt},
                 }}};

        if (!freePlay) {
            j[JKEY_CHN_PAYLOAD][JKEY_CREDITS_CURRENT] = credits;
            if (maxCredits > 0) {
                j[JKEY_CHN_PAYLOAD][JKEY_CREDITS_MAX] = maxCredits;
            }
        }

        INF("API-CF sending credits status: {}", j.dump());
        const auto r = m_centrifugo->publish(m_machineChannel, j);
        if (!r) {
            WRN("API-CF failed to send credits status: code:{}, error: {}", r.error().ec.value(),
                r.error().message);
        }
    });
}

void Net::handleDiagnosticProbe(const nlohmann::json &payload)
{
    // SB-3363 — see eng_docs/plans/scorbitd_diagnostic_implementation_plan.md.
    // Inbound shape: {"type":"diag_probe","payload":{"trace_id":"...",
    // "deadline_seconds":15}}.  Validate, dedupe defensively (a Centrifugo
    // history replay after reconnect could deliver the same probe twice),
    // then hand the publish + ack off to the worker strand so the centrifugo
    // dispatcher thread is never blocked on network I/O.
    std::string traceId;
    try {
        traceId = payload.value(JKEY_DIAG_TRACE_ID, std::string {});
    } catch (const std::exception &e) {
        WRN("DIAG: probe payload error: {}", e.what());
        return;
    }
    if (traceId.empty()) {
        WRN("DIAG: probe missing trace_id, dropping");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_seenDiagTraceIdsMutex);
        const auto [_, inserted] = m_seenDiagTraceIds.insert(traceId);
        if (!inserted) {
            WRN("DIAG: probe trace_id={} already seen, refusing duplicate", traceId);
            return;
        }
        // Cap the dedupe set so it cannot grow unbounded across long process
        // lifetimes. Real-world cadence is at most a few traces per device
        // per day; clearing the half at 10 keeps memory trivial.
        if (m_seenDiagTraceIds.size() > 10) {
            const auto half = m_seenDiagTraceIds.size() / 2;
            auto it = m_seenDiagTraceIds.begin();
            for (size_t i = 0; i < half && it != m_seenDiagTraceIds.end(); ++i) {
                it = m_seenDiagTraceIds.erase(it);
            }
        }
    }

    const auto sequence = m_diagProbeSequence.fetch_add(1, std::memory_order_relaxed);
    const auto createdAt = to_iso8601(chrono::system_clock::now());

    INF("DIAG: received probe trace_id={} sequence={} channel={}", traceId, sequence,
        m_machineChannel);

    publishDiagnosticPacket(traceId, sequence, createdAt);
    postDiagnosticAck(traceId, sequence, createdAt);
}

void Net::publishDiagnosticPacket(const std::string &traceId, uint64_t sequence,
                                  const std::string &createdAt)
{
    m_worker.post([this, traceId, sequence, createdAt]() {
        if (m_stop || !m_centrifugo) {
            return;
        }
        if (m_machineChannel.empty()) {
            WRN("DIAG: machine channel not ready, dropping publish for trace_id={}", traceId);
            return;
        }

        json j {{JKEY_CHN_TYPE, JVAL_CHN_TYPE_DIAG_PROBE},
                {JKEY_CHN_PAYLOAD,
                 {
                         {JKEY_DIAG_TRACE_ID, traceId},
                 }},
                {JKEY_SCR_METADATA,
                 {
                         {JKEY_SCR_MACHINE, m_machineInfo.machineUuid},
                         {JKEY_SCR_VARIANT, m_machineInfo.variantUuid},
                         {JKEY_SCR_VENUE, m_machineInfo.venueUuid},
                         {JKEY_SCR_SEQUENCE, sequence},
                         {JKEY_SCR_CREATED_AT, createdAt},
                         {JKEY_SCR_UPDATED_AT, createdAt},
                 }}};

        INF("API-CF sending diag probe: {}", j.dump());
        const auto r = m_centrifugo->publish(m_machineChannel, j);
        if (!r) {
            WRN("API-CF failed to send diag probe: code:{}, error: {}", r.error().ec.value(),
                r.error().message);
        }
    });
}

void Net::postDiagnosticAck(const std::string &traceId, uint64_t sequence,
                            const std::string &createdAt)
{
    m_worker.post(createPostRequestTask(
            [traceId](Error error, std::string reply) {
                if (error == Error::Success) {
                    INF("API diag ack: ok, trace_id={}, {}", traceId, reply);
                } else {
                    // Ack is best-effort — the synthetic subscriber's primary
                    // signal is the machine: channel publish above. Log and
                    // move on so a failing ack POST never throws or blocks
                    // subsequent dispatches.
                    WRN("API diag ack: failed, trace_id={}, error code: {}, reply: {}", traceId,
                        static_cast<int>(error), reply);
                }
            },
            [this, traceId, sequence, createdAt]() {
                json j {{JKEY_DIAG_TRACE_ID, traceId},
                        {JKEY_DIAG_HOP, JVAL_DIAG_HOP_DEVICE_EGRESS},
                        {JKEY_DIAG_TS, createdAt},
                        {JKEY_DIAG_PAYLOAD, {{JKEY_DIAG_SEQUENCE, sequence}}}};
                const auto endpoint = url(URL_DIAGNOSTICS_ACK_PATH);
                INF("API sending diag ack: {}", j.dump());
                return std::make_tuple(endpoint, cpr::Body {j.dump()});
            }));
}

void Net::handleDiagnosticCaptureStart(const nlohmann::json &payload)
{
    std::string runId;
    int requestedDuration = 0;
    try {
        runId = payload.value(JKEY_DIAG_RUN_ID, std::string {});
        requestedDuration = payload.value(JKEY_DIAG_REQUESTED_DURATION,
                                          payload.value(JKEY_DIAG_DEADLINE_SECONDS, 0));
    } catch (const std::exception &e) {
        WRN("DIAG: capture start payload error: {}", e.what());
        return;
    }

    if (runId.empty() || requestedDuration <= 0) {
        WRN("DIAG: capture start missing run_id or duration, dropping");
        return;
    }

    if (m_networkMonitor && m_networkMonitor->isActive()) {
        WRN("DIAG: capture start refused: run_id={} active_run_id={}", runId,
            m_networkMonitor->runId());
        return;
    }

    wifi::NetworkMonitor::Options options;
    options.runId = runId;
    options.requestedDuration = std::chrono::seconds {requestedDuration};

    wifi::NetworkMonitor::Callbacks callbacks;
    callbacks.onSample = [this, runId](const wifi::Sample &sample) {
        postWifiCaptureSample(runId, sample);
    };
    callbacks.onEvent = [this, runId](const wifi::Event &event) {
        postWifiCaptureEvent(runId, event);
    };

    m_networkMonitor = std::make_unique<wifi::NetworkMonitor>(std::move(options), std::move(callbacks));
    if (!m_networkMonitor->start()) {
        WRN("DIAG: capture start failed: run_id={}", runId);
        m_networkMonitor.reset();
        return;
    }

    INF("DIAG: capture started: run_id={} duration={}s", runId, requestedDuration);
}

void Net::handleDiagnosticCaptureStop(const nlohmann::json &payload)
{
    const auto runId = payload.value(JKEY_DIAG_RUN_ID, std::string {});
    if (!m_networkMonitor || !m_networkMonitor->isActive() || m_networkMonitor->runId() != runId) {
        WRN("DIAG: capture stop ignored: no matching active capture run_id={}", runId);
        return;
    }

    m_networkMonitor->stop("manual_stop");
    m_networkMonitor.reset();
    INF("DIAG: capture stopped: run_id={}", runId);
}

void Net::postWifiCaptureSample(const std::string &runId, const wifi::Sample &sample)
{
    m_worker.post(createPostRequestTask(
            [runId](Error error, std::string reply) {
                if (error == Error::Success) {
                    INF("API wifi capture sample: ok, run_id={}", runId);
                } else {
                    WRN("API wifi capture sample: failed, run_id={}, error code: {}, reply: {}",
                        runId, static_cast<int>(error), reply);
                }
            },
            [this, runId, sample]() {
                auto addInt = [](json &j, const char *key, const std::optional<int> &value) {
                    if (value) {
                        j[key] = *value;
                    }
                };
                auto addDouble = [](json &j, const char *key, const std::optional<double> &value) {
                    if (value) {
                        j[key] = *value;
                    }
                };
                auto addProbe = [&](json &j, const std::optional<wifi::ProbeResult> &probe,
                                    const char *rttKey, const char *lossKey) {
                    if (!probe) {
                        return;
                    }
                    addInt(j, rttKey, probe->rttMs);
                    addDouble(j, lossKey, probe->lossPct);
                };

                json j {{JKEY_DIAG_TS, to_iso8601(sample.ts)},
                        {JKEY_DIAG_SSID, sample.link.ssid},
                        {JKEY_DIAG_BSSID, sample.link.bssid},
                        {JKEY_DIAG_IS_FINAL, sample.isFinal}};

                addInt(j, JKEY_DIAG_RSSI_DBM, sample.link.rssiDbm);
                addInt(j, JKEY_DIAG_LINK_RATE_MBPS, sample.link.linkRateMbps);
                addDouble(j, JKEY_DIAG_TX_RETRY_PCT, sample.link.txRetryPct);
                addInt(j, JKEY_DIAG_BEACON_LOSS_COUNT, sample.link.beaconLossCount);
                addInt(j, JKEY_DIAG_FREQ_MHZ, sample.link.freqMhz);
                addInt(j, JKEY_DIAG_CHANNEL, sample.link.channel);
                addProbe(j, sample.gateway, JKEY_DIAG_GATEWAY_RTT_MS, JKEY_DIAG_GATEWAY_LOSS_PCT);
                addProbe(j, sample.scorbit, JKEY_DIAG_SCORBIT_RTT_MS, JKEY_DIAG_SCORBIT_LOSS_PCT);
                addProbe(j, sample.publicInternet, JKEY_DIAG_PUBLIC_RTT_MS,
                         JKEY_DIAG_PUBLIC_LOSS_PCT);

                const auto endpoint = url(URL_DIAGNOSTICS_WIFI_SAMPLE_PATH, fmt::arg(ARG_RUN_ID, runId));
                INF("API sending wifi capture sample: run_id={}, final={}", runId, sample.isFinal);
                return std::make_tuple(endpoint, cpr::Body {j.dump()});
            }));
}

void Net::postWifiCaptureEvent(const std::string &runId, const wifi::Event &event)
{
    m_worker.post(createPostRequestTask(
            [runId, kind = event.kind](Error error, std::string reply) {
                if (error == Error::Success) {
                    INF("API wifi capture event: ok, run_id={}, kind={}", runId, kind);
                } else {
                    WRN("API wifi capture event: failed, run_id={}, kind={}, error code: {}, "
                        "reply: {}",
                        runId, kind, static_cast<int>(error), reply);
                }
            },
            [this, runId, event]() {
                json payload = json::object();
                if (!event.payloadJson.empty()) {
                    try {
                        payload = json::parse(event.payloadJson);
                    } catch (const std::exception &) {
                        payload = event.payloadJson;
                    }
                }

                json j {{JKEY_DIAG_TS, to_iso8601(event.ts)},
                        {JKEY_DIAG_KIND, event.kind},
                        {JKEY_DIAG_PAYLOAD, payload}};
                if (event.reasonCode) {
                    j[JKEY_DIAG_REASON_CODE] = *event.reasonCode;
                }

                const auto endpoint = url(URL_DIAGNOSTICS_WIFI_EVENT_PATH, fmt::arg(ARG_RUN_ID, runId));
                INF("API sending wifi capture event: run_id={}, kind={}", runId, event.kind);
                return std::make_tuple(endpoint, cpr::Body {j.dump()});
            }));
}

void Net::recoverNetworkMonitorState()
{
    if (m_networkMonitorStateRecovered) {
        return;
    }
    m_networkMonitorStateRecovered = true;

    const auto state = wifi::NetworkMonitor::recoverState();
    if (!state || state->deadline <= std::chrono::system_clock::now()) {
        return;
    }

    nlohmann::json payload {
            {"started_at", to_iso8601(state->startedAt)},
            {"deadline", to_iso8601(state->deadline)},
            {"now", to_iso8601(std::chrono::system_clock::now())},
    };
    wifi::Event event;
    event.kind = "scorbitd_restart";
    event.payloadJson = payload.dump();
    postWifiCaptureEvent(state->runId, event);
}

void Net::scheduleDelayedOnWorker(std::chrono::steady_clock::duration delay,
                                  std::function<void()> fn)
{
    m_worker.stopTimer(Worker::Timer::ModeExpiry);
    m_worker.startTimer(Worker::Timer::ModeExpiry, delay, std::move(fn));
}

void Net::cancelModeExpiryTimer()
{
    m_worker.stopTimer(Worker::Timer::ModeExpiry);
}

void Net::uploadDiagnostics(std::vector<std::string> logPaths,
                            std::vector<std::string> recordingPaths, std::string logString)
{
    m_worker.post([this, logPaths = std::move(logPaths), recordingPaths = std::move(recordingPaths),
                   logString = std::move(logString)]() mutable {
        INF("API diagnostics upload: starting");

        std::vector<ArchiveFileEntry> archiveFiles;
        std::vector<ArchiveMemoryEntry> archiveMemory;

        auto filterPaths = [](std::vector<std::string> &paths, size_t maxCount, uintmax_t maxSize,
                              const char *label) {
            if (paths.size() > maxCount) {
                WRN("Diagnostics: truncating {} from {} to {}", label, paths.size(), maxCount);
                paths.resize(maxCount);
            }
            std::erase_if(paths, [maxSize, label](const std::string &path) {
                try {
                    if (!fs::exists(path)) {
                        WRN("Diagnostics: {} file does not exist, skipping: {}", label, path);
                        return true;
                    }
                    const auto size = fs::file_size(path);
                    if (size > maxSize) {
                        WRN("Diagnostics: {} file too large ({} bytes), skipping: {}", label, size,
                            path);
                        return true;
                    }
                } catch (const fs::filesystem_error &e) {
                    WRN("Diagnostics: error checking {} file, skipping: {}: {}", label, path,
                        e.what());
                    return true;
                }
                return false;
            });
        };

        filterPaths(logPaths, DIAG_MAX_LOGS, DIAG_MAX_LOG_SIZE, "log");
        filterPaths(recordingPaths, DIAG_MAX_RECORDINGS, DIAG_MAX_RECORDING_SIZE, "recording");

        for (const auto &path : logPaths) {
            const auto filename = fs::path(path).filename().string();
            archiveFiles.push_back({"logs/" + filename, path});
        }

        for (const auto &path : recordingPaths) {
            const auto filename = fs::path(path).filename().string();
            archiveFiles.push_back({"recordings/" + filename, path});
        }

        if (!logString.empty()) {
            if (logString.size() > DIAG_MAX_LOG_STRING_SIZE) {
                WRN("Diagnostics: log string too large ({} bytes), truncating to {} bytes",
                    logString.size(), DIAG_MAX_LOG_STRING_SIZE);
                logString.resize(DIAG_MAX_LOG_STRING_SIZE);
            }
            archiveMemory.push_back({"logs/extra.log", std::move(logString)});
        }

        if (archiveFiles.empty() && archiveMemory.empty()) {
            WRN("Diagnostics: no files to upload");
            m_eventManager->push(std::make_shared<DiagnosticsUploadedEvent>(false));
            return;
        }

        {
            std::string listing;
            for (const auto &f : archiveFiles) {
                if (!listing.empty()) {
                    listing += ", ";
                }
                listing += fmt::format("{} <- {}", f.archivePath, f.sourcePath);
            }
            for (const auto &m : archiveMemory) {
                if (!listing.empty()) {
                    listing += ", ";
                }
                listing += fmt::format("{} (in-memory, {} bytes)", m.archivePath, m.data.size());
            }
            INF("Diagnostics: archive will include: {}", listing);
        }

        const auto tempDir = fs::temp_directory_path();
        const auto unixTimeStamp = std::chrono::duration_cast<std::chrono::seconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count();
        const auto archivePath =
                (tempDir / fmt::format("diagnostics_{}.tar.gz", unixTimeStamp)).string();

        if (!createTarGz(archivePath, archiveFiles, archiveMemory)) {
            ERR("Diagnostics: failed to create archive");
            m_eventManager->push(std::make_shared<DiagnosticsUploadedEvent>(false));
            return;
        }

        INF("API diagnostics archive created: {}", archivePath);

        if (!fs::exists(archivePath)) {
            ERR("Diagnostics: archive missing after creation: {}", archivePath);
            m_eventManager->push(std::make_shared<DiagnosticsUploadedEvent>(false));
            return;
        }

        // Stream from disk via CPR (do not load the whole archive into memory).
        SafeMultipart multipart {cpr::Multipart {
                {"file", cpr::Files {cpr::File {archivePath, "diagnostics.tar.gz"}}},
        }};

        auto callback = [this, archivePath](Error error, std::string reply) {
            fs::remove(archivePath);
            if (error == Error::Success) {
                INF("API diagnostics upload: success");
                m_eventManager->push(std::make_shared<DiagnosticsUploadedEvent>(true));
            } else {
                ERR("API diagnostics upload: failed, error code: {}, reply: {}",
                    static_cast<int>(error), reply);
                m_eventManager->push(std::make_shared<DiagnosticsUploadedEvent>(false));
            }
        };

        auto deferredSetup = [this, multipart = std::move(multipart)]() {
            return std::make_tuple(url(URL_SCORBITRON_DIAGNOSTICS), std::move(multipart));
        };

        auto task = createPostMultipartRequestTask(std::move(callback), std::move(deferredSetup));
        task();
    });
}

task_t Net::createAuthenticateTask()
{
    return [this]() {
        const auto normalAuthentication = !m_isRefreshingToken;
        std::unique_lock lock(m_authMutex, std::defer_lock);

        if (normalAuthentication) {
            lock.lock();
            if (m_status != AuthStatus::NotAuthenticated) {
                return;
            }
            m_status = AuthStatus::Authenticating;
            m_lastEmittedPairingState.reset();
        }

        m_isRefreshingToken = true;

        // Get server time first - provisioning and authentication both need accurate timestamps
        std::string timestamp;
        for (int i = 0; i < 10 && !m_stop; ++i) {
            INF("API getting noop to retrieve server time...");
            auto noopReply = cpr::Get(cpr::Url {NOOP_URL}, cpr::Timeout {NET_TIMEOUT});
            std::string output =
                    fmt::format("code: {}, reply: {}", noopReply.status_code, noopReply.text);
            output += fmt::format("\nHEADER: [Date: {}]", noopReply.header["Date"]);
            const auto timestampUtc = parseHttpDateToUnixTimestamp(noopReply.header["Date"]);
            INF("API noop timestamp: {}, output {}", timestampUtc, output);
            if (timestampUtc > 1770153380) { // Some viable timestamp (2026-02-03)
                timestamp = std::to_string(timestampUtc);
                checkSystemTimeAccuracy(timestampUtc);
                break;
            }
            std::this_thread::sleep_for(1s);
        }

        if (timestamp.empty()) {
            // Fallback to local time
            timestamp = std::to_string(std::time(nullptr));
            WRN("API failed to get server time, falling back to local time: {}", timestamp);
        }

        // Resolve keys if we don't have a signer yet (async key resolver path).
        // Resolve keys via the resolver chain (signer, NFC TPM, soft key).
        // Done after obtaining server time so provisioning uses accurate timestamps.
        if (!m_signer && !m_keyResolvers.empty()) {
            if (!resolveKeys(timestamp)) {
                m_status = AuthStatus::AuthenticationFailed;
                ERR("API there is no functional key to authenticate");
                m_authCV.notify_all();
                return;
            }
        }

        for (int i = 0;; ++i) {
            const auto signature = getSignature(m_signer, m_deviceInfo.uuid, timestamp);
            if (signature.empty()) {
                ERR("Can't authenticate, signature is empty");
                m_status = AuthStatus::AuthenticationFailed;
                stopTokenRefreshTimer();
                m_authCV.notify_all();
                return;
            }

            // Create json string
            json j {
                    {JKEY_AUTH_PROVIDER, m_deviceInfo.provider},
                    {JKEY_AUTH_UUID, m_deviceInfo.uuid},
                    {JKEY_AUTH_TIMESTAMP, timestamp},
                    {JKEY_AUTH_SIGNATURE, signature},
                    {JKEY_AUTH_SERIAL_NUMBER, 0},
            };

            if (m_fingerprint.hasAny()) {
                j["fingerprint"] = m_fingerprint.toJson();
            }

            const auto payload = j.dump();
            INF("API authenticating to {}", m_hostname);

            // Use custom HTTP request since authentication has special retry logic
            cpr::Header authHeaders {{HDR_KEY_CONTENT_TYPE, HDR_VAL_CONTENT_JSON}};
            if (!m_fingerprintHash.empty()) {
                authHeaders[HDR_KEY_FINGERPRINT_HASH] = m_fingerprintHash;
            }
            auto r = cpr::Post(url(URL_SCORBITRON_TOKEN), cpr::Body {payload}, authHeaders,
                               cpr::Timeout {NET_TIMEOUT}, sslOptions());

            if (m_stop) {
                m_status = AuthStatus::AuthenticationFailed;
                m_isRefreshingToken = false;
                m_authCV.notify_all();
                return;
            }

            // Check system time and timestamp from response header
            const auto parsedTimestamp = parseHttpDateToUnixTimestamp(r.header["Date"]);
            if (parsedTimestamp > 0) {
                timestamp = std::to_string(parsedTimestamp);
            }

            if (r.status_code == 200) {
                try {
                    const auto json = json::parse(r.text);
                    {
                        std::unique_lock tokenLock(m_tokenMutex);
                        json[JKEY_SCORBITRON_TOKEN].get_to(m_stoken);
                    }

                    startTokenRefreshTimer(); // Start/restart token refresh timer
                    m_authCV.notify_all();

                    if (normalAuthentication) {
                        m_status = AuthStatus::AuthenticatedCheckingPairing;
                        INF("API authentication successful! Checking pairing status...");
                        initializeConnectionState();
                    } else {
                        INF("API token refreshed successful!");
                        requestReleaseTrackInfo();
                    }
                    break;
                } catch (const std::exception &e) {
                    ERR("Error parsing authentication reply: {}", e.what());
                    m_status = AuthStatus::AuthenticationFailed;
                    stopTokenRefreshTimer();
                    m_authCV.notify_all();
                    return;
                }
            } else if (r.status_code == 400) {
                // Retry with new timestamp parsed from reply header
                INF("API authentication failed: code {}, {}, {}, will retry with new timestamp",
                    r.status_code, r.error.message, r.text);
                if (i < 10) {
                    std::this_thread::sleep_for(1000ms);
                    continue;
                }
            } else if (r.status_code == 404 && reprovisionSoftKey(timestamp)) {
                // Scorbitron was deleted from API - re-provisioned with a new identity, retry auth
                if (i < 10) {
                    continue;
                }
            } else if (r.status_code == 0) {
                // Network error, retry
                ERR("API authentication network error: {}, will retry in 10s", r.error.message);
                std::this_thread::sleep_for(10s);
                continue;
            }

            m_status = AuthStatus::AuthenticationFailed;
            stopTokenRefreshTimer();
            const auto msg = fmt::format("API authentication failed: code {}, {}", r.status_code,
                                         r.error.message);
            ERR("{}", msg);
            ERR("{}", r.text);
            // TODO: Sentry
            // SentryManager::message(msg);
            m_authCV.notify_all();
            break;
        }
    };
}

task_t Net::updateConfigTask(const std::string &type, const std::string &version, bool installed,
                             std::optional<std::string> log)
{
    // Create json string
    json j {
            {JKEY_SCFG_VERSION, version},
            {JKEY_SCFG_TYPE, type},
            {JKEY_SCFG_INSTALLED, installed},
    };
    if (log) {
        j[JKEY_SCFG_LOG] = *log;
    }

    auto callback = [](Error error, std::string reply) {
        if (error == Error::Success) {
            INF("API update config: ok, {}", reply);
        } else {
            ERR("API update config: failed, error code: {}, reply: {}", static_cast<int>(error),
                reply);
        }
    };

    auto deferredSetup = [this, payload = j.dump()]() {
        INF("API sending update config: {}", payload);
        return std::make_tuple(url(URL_SCORBITRON_CONFIG), cpr::Body {payload});
    };

    return createPatchRequestTask(std::move(callback), std::move(deferredSetup),
                                  {
                                          AuthStatus::AuthenticatedUnpaired,
                                          AuthStatus::AuthenticatedPaired,
                                  });
}

task_t Net::createSessionCreateTask(int sessionId, GameStartOrigin origin,
                                    std::function<void()> onCreated)
{
    INF("API session create for id: {}, started by: {} ...", sessionId, origin);
    int sessionCounter;
    size_t playerCount = 0;
    int64_t elapsedMilliseconds = 0;
    {
        std::scoped_lock lock(m_gameSessionsMutex);
        if (m_gameSessions.count(sessionId) == 0) {
            // Nothing to do, session is already finished and removed
            ERR("API this error should not happen. Can't find session {} in game sessions",
                sessionId);
            return noop_task;
        }

        auto &gameSession = m_gameSessions[sessionId];
        sessionCounter = ++gameSession.sessionCounter;
        playerCount = gameSession.gameData.players.size();
        elapsedMilliseconds = chrono::duration_cast<chrono::milliseconds>(
                                      chrono::steady_clock::now() - gameSession.startedTime)
                                      .count();
    }
    const auto currentDateTime = to_iso8601(chrono::system_clock::now());

    const auto isUseLobby = (origin == GameStartOrigin::FromLobby);

    json j {
            {JKEY_SESS_PLAYER_COUNT, isUseLobby ? numberOfPlayersRequested() : playerCount},
            {JKEY_SESS_SEQUENCE_NUMBER, sessionCounter},
            {JKEY_SESS_SESSION_TIME, elapsedMilliseconds},
            {JKEY_SESS_ACTIVE_ON, currentDateTime},
            {JKEY_SESS_USE_LOBBY, isUseLobby},
    };

    auto deferredSetup = [this, body = j.dump()]() {
        return std::make_tuple(url(URL_SCORBITRON_SESSIONS), cpr::Body {body});
    };

    auto callback = [this, sessionId, onCreated = std::move(onCreated)](Error error,
                                                                        std::string reply) {
        if (error == Error::Success) {
            INF("API create session: ok, id: {}, {}", sessionId, reply);

            try {
                json json = json::parse(reply);

                if (const auto it = json.find(JKEY_SESS_UUID);
                    it != json.end() && it->is_string()) {

                    std::string newSessionUuid;
                    it->get_to(newSessionUuid);

                    bool sessionUpdated = false;
                    {
                        std::scoped_lock lock(m_gameSessionsMutex);
                        const auto gsIt = m_gameSessions.find(sessionId);
                        if (gsIt == m_gameSessions.end()) {
                            ERR("API create session: session {} no longer in game sessions",
                                sessionId);
                        } else {
                            gsIt->second.sessionUuid = std::move(newSessionUuid);
                            sessionUpdated = true;

                            INF("API created session id: {}, uuid: {}, address: {:x}", sessionId,
                                gsIt->second.sessionUuid,
                                reinterpret_cast<std::uintptr_t>(&gsIt->second.gameData));

                            // Scores array will have players' profiles
                            if (const auto scoresIt = json.find(JKEY_SCR_SCORES);
                                scoresIt != json.end() && scoresIt->is_array()) {
                                processScoresAndPlayersProfiles(*scoresIt, gsIt->second);
                            } else {
                                WRN("API create session: can't find scores list in reply");
                            }
                        }
                    }

                    if (onCreated && !m_stop && sessionUpdated) {
                        try {
                            onCreated();
                        } catch (const std::exception &e) {
                            ERR("API create session onCreated: {}", e.what());
                        }
                    }
                } else {
                    ERR("API create session: can't find session UUID in reply");
                }
            } catch (const std::exception &e) {
                ERR("API error parsing game data reply: {}", e.what());
            }
        }
    };

    return createPostRequestTask(std::move(callback), std::move(deferredSetup),
                                 {AuthStatus::AuthenticatedPaired},
                                 true /* includeFingerprintHash */);
}

task_t Net::createSessionUpdateTask(int sessionId, SessionFlags flags)
{
    std::string sessionUuid;
    size_t playerCount = 0;
    int64_t elapsedMilliseconds = 0;
    bool isActive = false;
    int sessionCounterForForm = 0;
    std::string csv;

    {
        std::scoped_lock lock(m_gameSessionsMutex);
        if (m_gameSessions.count(sessionId) == 0) {
            // Nothing to do, session is already finished and removed
            ERR("API this error should not happen. Can't find session {} in game sessions",
                sessionId);
            return noop_task;
        }

        const auto &gameSession = m_gameSessions[sessionId];
        sessionUuid = gameSession.sessionUuid;
        playerCount = gameSession.gameData.players.size();
        elapsedMilliseconds = chrono::duration_cast<chrono::milliseconds>(
                                      chrono::steady_clock::now() - gameSession.startedTime)
                                      .count();
        isActive = gameSession.gameData.isGameActive;
        sessionCounterForForm = gameSession.sessionCounter;
        if (flags.has(SessionFlag::UploadHistoryLogs)) {
            csv = gameHistoryToCsv(gameSession.history);
        }
    }
    if (sessionUuid.empty()) {
        // Try again later
        INF("API update session for id: {} will be retried in {}, session uuid not ready yet...",
            sessionId, chrono::duration_cast<chrono::milliseconds>(SESSION_UPDATE_NO_UUID_RETRY));
        m_worker.startTimer(Worker::Timer::SessionUpdate, SESSION_UPDATE_NO_UUID_RETRY,
                            [this, sessionId, flags]() { sessionUpdate(sessionId, flags); });

        // Session patch cancelled, session uuid is not ready yet
        return noop_task;
    }

    INF("API update session for id: {}, uuid: {}, upload logs: {}, players count: {} ...",
        sessionId, sessionUuid, flags.has(SessionFlag::UploadHistoryLogs), playerCount);

    const auto currentDateTime = to_iso8601(chrono::system_clock::now());

    const auto filename = fmt::format("{}.{}", sessionUuid, SESS_LOG_EXTENSION);

    // Here we can't use json in case of uploading session log file, so we are using multipart
    cpr::Multipart formData {
            {JKEY_SESS_PLAYER_COUNT, std::to_string(playerCount)},
            {JKEY_SESS_SEQUENCE_NUMBER, std::to_string(sessionCounterForForm)},
            {JKEY_SESS_SESSION_TIME, std::to_string(elapsedMilliseconds)},
            {(isActive ? JKEY_SESS_ACTIVE_ON : JKEY_SESS_SETTLED_ON), currentDateTime},
    };

    // If the game is finished, set "active off" time
    if (!isActive) {
        formData.parts.push_back({JKEY_SESS_SUCCESSFULLY_COMPLETED, "True"});
    }

    // History CSV logs - built under m_gameSessionsMutex from session.history (no GameHistory
    // copy). csv must outlive formData for cpr::Buffer; SafeMultipart copies buffers later.
    if (flags.has(SessionFlag::UploadHistoryLogs)) {
        formData.parts.push_back(
                {JKEY_SESS_LOG_FILE, cpr::Buffer(csv.cbegin(), csv.cend(), filename)});
    }

    const auto sessionUpdateUrl =
            url(URL_SCORBITRON_SESSION_UPDATE, fmt::arg(ARG_SESSION_UUID, sessionUuid));

    auto deferredSetup = [sessionUpdateUrl = std::move(sessionUpdateUrl),
                          safeFormData = SafeMultipart {std::move(formData)}]() {
        return std::make_tuple(sessionUpdateUrl, std::move(safeFormData));
    };

    auto callback = [this, sessionId](Error error, std::string reply) {
        if (error == Error::Success) {
            INF("API update session: ok, id: {}, {}", sessionId, reply);

            // Erase the session if the game is finished
            std::scoped_lock lock(m_gameSessionsMutex);
            if (!m_gameSessions[sessionId].gameData.isGameActive) {
                m_gameSessions.erase(sessionId);
            } else {
                try {
                    json json = json::parse(reply);
                    if (const auto scoresIt = json.find(JKEY_SCR_SCORES);
                        scoresIt != json.end() && scoresIt->is_array()) {
                        auto gameSession = &m_gameSessions[sessionId];
                        processScoresAndPlayersProfiles(*scoresIt, *gameSession);
                    }
                } catch (const std::exception &e) {
                    ERR("API update session error parsing game data reply: {}", e.what());
                }
            }
        } else {
            ERR("API update session: failed, id: {}, error code: {}", sessionId,
                static_cast<int>(error));
            // TODO: Sentry
            // FIXME: what to do with game session? Erase it or retry again?
        }
    };

    return createPatchMultipartRequestTask(std::move(callback), std::move(deferredSetup),
                                           {AuthStatus::AuthenticatedPaired},
                                           true /* includeFingerprintHash */);
}

task_t Net::createHeartbeatTask()
{
    return noop_task; // FIXME: disable heartbeat for now, implement heatbeat v2

    /*
    return [this]() {
        for (int i = 0; i < NUM_RETRIES; ++i) {
            // DBG("Before waiting heartbeat");

            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                switch (m_status) {
                case AuthStatus::AuthenticatedCheckingPairing:
                case AuthStatus::AuthenticatedUnpaired:
                case AuthStatus::AuthenticatedPaired:
                case AuthStatus::AuthenticationFailed:
                    return true;
                default:
                    if (m_stop) {
                        return true;
                    }
                    return false;
                }
            });

            if (m_status == AuthStatus::AuthenticationFailed) {
                break;
            }

            bool isActiveSession;
            {
                std::scoped_lock lockGameSession(m_gameSessionsMutex);
                isActiveSession = !m_gameSessions.empty();
            }
            const auto parameters =
                    cpr::Parameters {{"session_active", isActiveSession ? "true" : "false"}};

            // TODO: sentry

            INF("API sending heartbeat with session_active: {}", isActiveSession);

            const auto r = cpr::Get(url(HEARTBEAT_URL), parameters, authHeader(),
                                    cpr::Timeout {NET_TIMEOUT}, sslOptions());

            if (r.status_code == 200) {
                INF("API heartbeat: ok, {}", r.text);

                try {
                    json json = json::parse(r.text);
                    AuthStatus status {AuthStatus::AuthenticatedPaired};
                    if (const auto it = json.find("unpaired");
                        it != json.end() && it->is_boolean()) {
                        if (it->get<bool>()) {
                            status = AuthStatus::AuthenticatedUnpaired;
                            m_machineInfo.venuemachineId = 0;
                            m_machineInfo.opdbId.clear();
                        }
                    }

                    if (const auto it = json.find("venuemachine_id");
                        it != json.end() && it->is_number()) {
                        m_machineInfo.venuemachineId = it->get<int64_t>();
                    }

                    if (const auto configIt = json.find("config");
                        configIt != json.end() && configIt->is_object()) {
                        if (const auto opdbIt = configIt->find("opdb_id");
                            opdbIt != configIt->end() && opdbIt->is_string()) {
                            m_machineInfo.opdbId = opdbIt->get<std::string>();
                        }
                    }

                    m_updater.checkNewVersionAndUpdate(json);

                    if (m_status != status) {
                        m_status = status;
                        m_authCV.notify_all();
                    }
                } catch (const std::exception &e) {
                    ERR("Error parsing heartbeat reply: {}", e.what());
                }
                break;
            }

            ERR("API hearbeat failed: code={}, {}", r.status_code, r.error.message);
            ERR("{}", r.text);

            if (r.status_code != 401) {
                break;
            }

            m_status = AuthStatus::NotAuthenticated;
            stopTokenRefreshTimer();
            auto auth = createAuthenticateTask();
            auth();
        }

        m_isHeartbeatInQueue = false;
        // DBG("On quit heartbeat");
    };
*/
}

void Net::sessionUpdate(int sessionId, SessionFlags flags)
{
    if (m_stop) {
        return;
    }

    m_worker.stopTimer(Worker::Timer::SessionUpdate);

    if (!flags.has(SessionFlag::UploadHistoryLogs) && !flags.has(SessionFlag::Debounced)
        && flags.has(SessionFlag::PlayersAdd)) {
        // Debounce, wait some time before sending update, maybe another player will press Start
        // Button at this time
        INF("API post update session debounce, postpone to {}, id: {}, upload history logs: {}, "
            "player add: {}",
            chrono::duration_cast<chrono::milliseconds>(SESSION_UPDATE_ADD_PLAYER_DEBOUNCE),
            sessionId, flags.has(SessionFlag::UploadHistoryLogs),
            flags.has(SessionFlag::PlayersAdd));

        flags.set(SessionFlag::Debounced);
        m_worker.startTimer(Worker::Timer::SessionUpdate, SESSION_UPDATE_ADD_PLAYER_DEBOUNCE,
                            [this, sessionId, flags]() { sessionUpdate(sessionId, flags); });
        return;
    }

    INF("API post update session, id: {}, upload history logs: {}, player add: {}", sessionId,
        flags.has(SessionFlag::UploadHistoryLogs), flags.has(SessionFlag::PlayersAdd));
    m_worker.postSessionQueue(createSessionUpdateTask(sessionId, flags));
}

void Net::startHeartbeatTimer()
{
    m_worker.startTimer(Worker::Timer::Heartbeat, HEARTBEAT_TIME, [this] {
        startHeartbeatTimer();
        sendHeartbeat();
    });
}

void Net::stopHeartbeatTimer()
{
    m_worker.stopTimer(Worker::Timer::Heartbeat);
}

void Net::startTokenRefreshTimer()
{
    // Calculate time until token expires minus TOKEN_BEFORE_EXPIRY
    const auto timeUntilExpiration = getTimeUntilTokenExpiration();

    if (timeUntilExpiration && *timeUntilExpiration > REFRESH_TOKEN_BEFORE_EXPIRY) {
        const auto refreshDelay = *timeUntilExpiration - REFRESH_TOKEN_BEFORE_EXPIRY;
        INF("API starting token refresh timer, will refresh in {} seconds", refreshDelay.count());

        m_worker.startTimer(Worker::Timer::TokenRefresh, refreshDelay, [this] {
            INF("API token refresh timer triggered, requesting new token...");
            m_isRefreshingToken = true;
            authenticate();
        });
    } else {
        WRN("API token refresh timer not started: token expires too soon or is invalid");
    }
}

void Net::stopTokenRefreshTimer()
{
    m_worker.stopTimer(Worker::Timer::TokenRefresh);
    m_isRefreshingToken = false;
}

void Net::sendLatestGameData(int sessionId)
{
    m_worker.postCommitTask([this, sessionId]() {
        // Cancel timer if any
        m_worker.stopTimer(Worker::Timer::GameData);

        if (m_stop || !m_centrifugo) {
            return;
        }

        GameData data;
        std::string sessionUuid;
        int sessionCounter = 0;
        std::chrono::system_clock::time_point startedSystemTime {};
        std::unordered_map<sb_player_t, ScoreMetadata> scoresMetadataSnapshot;

        {
            std::scoped_lock lock(m_gameSessionsMutex);
            const auto it = m_gameSessions.find(sessionId);
            if (it == m_gameSessions.end()) {
                return;
            }
            GameSession &gameSession = it->second;
            data = gameSession.gameData;
            sessionUuid = gameSession.sessionUuid;
            startedSystemTime = gameSession.startedSystemTime;
            scoresMetadataSnapshot = gameSession.scoresMetadata;
        }

        const auto &gameData = data;

        // Ensure that only single task in the queue (while another can be running).
        // However, if game session is finished (not active), post task anyway, because this is the
        // last task for that game session.

        if (sessionUuid.empty()
            || m_centrifugo->state() != centrifugo::ConnectionState::Connected) {
            INF("Skip publishing score yet: has session uuid: {}, centrifugo connected: {}",
                !sessionUuid.empty(),
                m_centrifugo->state() == centrifugo::ConnectionState::Connected);
        } else {
            {
                std::scoped_lock lock(m_gameSessionsMutex);
                const auto it = m_gameSessions.find(sessionId);
                if (it == m_gameSessions.end()) {
                    return;
                }
                sessionCounter = ++it->second.sessionCounter;
            }
            const auto modes = json::parse(gameData.modes.jsonStr());
            const auto updatedAt = to_iso8601(chrono::system_clock::now());
            const auto createdAt = to_iso8601(startedSystemTime);

            json::array_t scores;
            for (const auto &[playerNum, playerState] : gameData.players) {
                json playerProfileJson = nullptr;
                if (const auto playerProfile = m_playersManager.profile(playerNum);
                    playerProfile.has_value() && playerProfile->hasInfo()) {
                    playerProfileJson = {
                            {JKEY_PLAYER_ID, playerProfile->id},
                            {JKEY_PLAYER_PREFER_INITIALS, playerProfile->preferInitials},
                            {JKEY_USERNAME, playerProfile->username},
                            {JKEY_PLAYER_DISPLAY_NAME, playerProfile->name},
                            {JKEY_PLAYER_INITIALS, playerProfile->initials},
                            {JKEY_AVATAR, playerProfile->pictureUrl}};
                }

                const auto valBallInProgress =
                        gameData.isGameActive && (gameData.activePlayer == playerNum);

                ScoreMetadata meta;
                if (const auto metaIt = scoresMetadataSnapshot.find(playerNum);
                    metaIt != scoresMetadataSnapshot.end()) {
                    meta = metaIt->second;
                }

                json playerScoreJson {{JKEY_SCR_POSITION, playerNum},
                                      {JKEY_SCR_ID, meta.id},
                                      {JKEY_SCR_IS_NFC_VERIFIED, meta.isNfcVerified},
                                      {JKEY_SCR_TOURNAMENT_UUID, meta.tournamentUuid},
                                      {JKEY_SCR_PLAYER, playerProfileJson},
                                      {JKEY_SCR_SCORE, playerState.score()},
                                      {JKEY_SCR_BALL, gameData.ball},
                                      {JKEY_SCR_BALL_IN_PROGRESS, valBallInProgress},
                                      {JKEY_SCR_MODES, modes}};

                scores.emplace_back(playerScoreJson);
            }

            const auto valType = (data.isGameActive ? JVAL_SCR_SCORE_UPDATE : JVAL_SCR_GAME_END);
            const auto keyScores = (data.isGameActive ? JKEY_SCR_SCORES : JKEY_SCR_FINAL_SCORES);

            json j {{JKEY_CHN_TYPE, valType},
                    {JKEY_CHN_PAYLOAD,
                     {
                             {JKEY_SCR_GAME_IN_PROGRESS, data.isGameActive},
                             {keyScores, scores},
                     }},
                    {JKEY_SCR_METADATA,
                     {
                             {JKEY_SCR_GAME, sessionUuid},
                             {JKEY_SCR_MACHINE, m_machineInfo.machineUuid},
                             {JKEY_SCR_VARIANT, m_machineInfo.variantUuid},
                             {JKEY_SCR_VENUE, m_machineInfo.venueUuid},
                             {JKEY_SCR_SEQUENCE, sessionCounter},
                             {JKEY_SCR_CREATED_AT, createdAt},
                             {JKEY_SCR_UPDATED_AT, updatedAt},
                     }}};

            const auto jstr = j.dump();
            INF("API sending game data to channel: {}, data: {}", m_machineChannel, jstr);

            const auto r = m_centrifugo->publish(m_machineChannel, j);
            if (!r) {
                WRN("API failed to send game data: {}", r.error().message);
            }
        }

        // Set timer for the next game data send if the game is still active
        if (data.isGameActive) {
            m_worker.startTimer(Worker::Timer::GameData, GAME_DATA_UPDATE_INTERVAL,
                                [this, sessionId] { sendLatestGameData(sessionId); });
        } else {
            // It's game over, request from client the credits status as a capstone
            requestCreditsStatusEvent();
        }
    });
}

void Net::initializeConnectionState()
{
    // set authentication info and pair status
    sendScorbitronObject();
    requestReleaseTrackInfo();

    getConfig(); // Get template
    requestFirmwaresList();
    sendHeartbeat();
    startHeartbeatTimer();
    restartCentrifugo();
    createNfcNonces();
    recoverNetworkMonitorState();
}

void Net::initScorbitronObject()
{
    std::scoped_lock lock(m_scorbitronObjectMutex);

    m_scorbitronObject = nlohmann::json::object();

    // Set versions
    m_scorbitronObject[JKEY_SOBJ_SDK_VERSION] = SCORBIT_SDK_VERSION;
    m_scorbitronObject[JKEY_SOBJ_SCORBITD_VERSION] = m_deviceInfo.scorbitdVersion;
    m_scorbitronObject[JKEY_SOBJ_GAME_CODE_VERSION] = m_deviceInfo.gameCodeVersion;

    // Set capabilities
    m_scorbitronObject[JKEY_SOBJ_NFC_CAPABLE] = m_isNfcCapable;
    m_scorbitronObject[JKEY_SOBJ_START_GAME_CAPABLE] = false;
    m_scorbitronObject[JKEY_SOBJ_CREDIT_DROP_CAPABLE] = false;
    // SB-3363 — handler ships unconditionally; advertise so the API can gate
    // probe dispatch on this bool and avoid timing out old-SDK devices.
    m_scorbitronObject[JKEY_SOBJ_DIAG_PROBE_CAPABLE] = true;
    m_scorbitronObject[JKEY_SOBJ_DIAG_CAPTURE_CAPABLE] = true;

    if (const auto lanIp = getPrimaryLanIp(); !lanIp.empty()) {
        m_scorbitronObject[JKEY_SOBJ_LAN_IP] = lanIp;
    }
}

void Net::sendScorbitronObject()
{
    m_worker.post(createPatchRequestTask(
            [this](Error error, std::string reply) {
                if (error == Error::Success) {
                    INF("API initial Scorbitron object sent: ok, {}", reply);
                } else {
                    ERR("API initial Scorbitron object sent: failed, error code: {}, "
                        "reply: {}",
                        static_cast<int>(error), reply);
                }
                parseScorbitronObject(error, reply);
            },
            [this]() {
                std::string body;
                {
                    std::scoped_lock lock(m_scorbitronObjectMutex);
                    body = m_scorbitronObject.dump();
                }
                const auto endpoint = url(URL_SCORBITRON_OBJECT);
                INF("API patching Scorbitron with {}", body);

                return make_tuple(endpoint, cpr::Body {body});
            },
            {
                    AuthStatus::AuthenticatedCheckingPairing,
                    AuthStatus::AuthenticatedUnpaired,
                    AuthStatus::AuthenticatedPaired,
            }));
}

void Net::requestReleaseTrackInfo()
{
    INF("API request release track info using {}...", m_releaseTrackUrl);

    auto callback = [this](Error error, std::string reply) {
        if (error == Error::Success) {
            INF("API get release track info: ok, {}", reply);
            try {
                json j = json::parse(reply);
                m_updater.checkNewVersionAndUpdate(j, m_eventManager);
            } catch (const std::exception &e) {
                ERR("API error parsing release track info reply: {}", e.what());
            }
            // m_eventManager->push(std::make_shared<ScorbitdUpdateReceivedEvent>(reply));
        } else {
            ERR("API get release track info: failed, error code: {}, reply: {}",
                static_cast<int>(error), reply);
        }
    };

    m_worker.post(createGetRequestTask(
            std::move(callback),
            [this]() { return make_tuple(cpr::Url {m_releaseTrackUrl}, cpr::Parameters {}); },
            {
                    AuthStatus::AuthenticatedUnpaired,
                    AuthStatus::AuthenticatedPaired,
            }));
}

void Net::requestMachineObject()
{
    INF("API request machine object...");

    auto callback = [this](Error error, std::string reply) {
        if (error == Error::Success) {
            INF("API request machine object: ok, {}", reply);
            try {
                const auto j = json::parse(reply);
                const auto name = j.value(JKEY_MOBJ_NAME, std::string {});
                const auto edition = j.value(JKEY_MOBJ_EDITION, std::string {});

                if (const auto gameIt = j.find(JKEY_MOBJ_GAME);
                    gameIt != j.end() && gameIt->is_object()) {
                    if (const auto slugIt = gameIt->find(JKEY_MOBJ_SLUG);
                        slugIt != gameIt->end() && slugIt->is_string()) {
                        m_machineInfo.gameSlug = slugIt->get<std::string>();
                    } else {
                        m_machineInfo.gameSlug.reset();
                    }
                } else {
                    m_machineInfo.gameSlug.reset();
                }

                m_machineInfo.title = fmt::format("{} ({})", name, edition);
                updateDiscoveryDescription();
            } catch (const std::exception &e) {
                ERR("API reuquest machine object parse error: {}, reply: {}", e.what(), reply);
            }
        } else {
            ERR("API reuquest machine object: failed, error code: {}, reply: {}",
                static_cast<int>(error), reply);
        }
    };

    m_worker.post(createGetRequestTask(std::move(callback), [this]() {
        return make_tuple(url(URL_MACHINE_OBJECT), cpr::Parameters {});
    }));
}

void Net::requestSessionData(const std::string &sessionUuid)
{
    INF("API post queue request session data, uuid: {} ...", sessionUuid);

    auto callback = [this, sessionUuid](Error error, std::string reply) {
        if (error == Error::Success) {
            INF("API get session data: ok, {}", reply);
            // Find out sessionId
            int sessionId = -1;
            {
                std::scoped_lock lock(m_gameSessionsMutex);
                for (const auto &[id, gameSession] : m_gameSessions) {
                    if (gameSession.sessionUuid == sessionUuid) {
                        sessionId = id;
                        break;
                    }
                }
            }

            if (sessionId >= 0) {
                json j = json::parse(reply);
                if (const auto scoresIt = j.find(JKEY_SCR_SCORES);
                    scoresIt != j.end() && scoresIt->is_array()) {
                    std::scoped_lock lock(m_gameSessionsMutex);
                    const auto gsIt = m_gameSessions.find(sessionId);
                    if (gsIt != m_gameSessions.end()) {
                        processScoresAndPlayersProfiles(*scoresIt, gsIt->second);
                    }
                }
            }
        } else {
            ERR("API get session data: failed, error code: {}, reply: {}", static_cast<int>(error),
                reply);
        }
    };

    auto deferredSetup = [this, sessionUuid]() {
        const auto endpoint =
                url(URL_SCORBITRON_SESSION_UPDATE, fmt::arg(ARG_SESSION_UUID, sessionUuid));
        cpr::Parameters parameters;
        return make_tuple(endpoint, parameters);
    };

    m_worker.postSessionQueue(createGetRequestTask(std::move(callback), std::move(deferredSetup)));
}

void Net::postUploadHistoryTask(const GameHistory &history, const std::string &sessionUuid)
{
    m_worker.postQueue(createUploadHistoryTask(history, sessionUuid));
}

task_t Net::createUploadHistoryTask(const GameHistory &history, const string &sessionUuid)
{
    const auto filename = fmt::format("{}.{}", sessionUuid, SESS_LOG_EXTENSION);
    const auto csv = gameHistoryToCsv(history);
    SafeMultipart multipart {cpr::Multipart {
            {JKEY_SESS_LOG_UUID, sessionUuid},
            {JKEY_SESS_LOG_FILE_DEPRECATED, cpr::Buffer(csv.cbegin(), csv.cend(), filename)},
    }};

    return createUploadTask(SESSION_CSV_URL, filename, std::move(multipart));
}

task_t Net::createUploadTask(const std::string &endpoint, const std::string &filename,
                             SafeMultipart &&multipart)
{
    auto callback = [filename](Error error, std::string reply) {
        if (error == Error::Success) {
            INF("API upload to backend finished: ok! {}", filename);
        } else {
            ERR("API upload to backend failed! {}, error code: {}, reply: {}", filename,
                static_cast<int>(error), reply);
        }
    };

    auto deferredSetup = [this, endpoint, multipart = std::move(multipart)]() {
        return std::make_tuple(url(endpoint), std::move(multipart));
    };

    return createPostMultipartRequestTask(std::move(callback), std::move(deferredSetup));
}

void Net::clearPairedMachineContext()
{
    m_machineInfo.opdbId.clear();
    m_machineInfo.machineUuid.clear();
    m_machineInfo.variantUuid.reset();
    m_machineInfo.gameSlug.reset();
    m_machineInfo.venueUuid.reset();
    m_machineInfo.title = fmt::format("not paired");
    m_machineChannel.clear();
    updateDiscoveryDescription();
}

void Net::emitPairingStatusEventIfChanged(bool isPaired)
{
    if (m_lastEmittedPairingState.has_value() && *m_lastEmittedPairingState == isPaired) {
        return;
    }
    m_lastEmittedPairingState = isPaired;
    m_eventManager->push(std::make_shared<PairingStatusChangedEvent>(isPaired));
}

void Net::onPaired()
{
    m_status = AuthStatus::AuthenticatedPaired;
    sendScorbitronObject();
    requestReleaseTrackInfo();
    getConfig();
    requestFirmwaresList();
    sendHeartbeat();
    startHeartbeatTimer();
    createNfcNonces();
    restartCentrifugo();
    emitPairingStatusEventIfChanged(true);
}

void Net::onUnpaired()
{
    m_status = AuthStatus::AuthenticatedUnpaired;
    clearPairedMachineContext();
    m_authCV.notify_all();
    emitPairingStatusEventIfChanged(false);
    restartCentrifugo();
}

void Net::parseScorbitronObject(Error error, const std::string &reply)
{
    if (error != Error::Success) {
        return;
    }

    try {
        json json = json::parse(reply);

        // "shortcode"
        if (const auto it = json.find(JKEY_SOBJ_SHORTCODE); it != json.end() && it->is_string()) {
            {
                std::scoped_lock lock(m_shortCodeMutex);
                it->get_to(m_cachedShortCode);
            }
            m_shortCodeCV.notify_all();
        }

        bool isPaired {false};

        // "machine"
        if (const auto it = json.find(JKEY_SOBJ_MACHINE_OBJ); it != json.end()) {
            isPaired = it->is_object();
            if (isPaired) {
                if (const auto idIt = it->find(JKEY_SOBJ_MACHINE_UUID);
                    idIt != it->end() && idIt->is_string()) {
                    idIt->get_to(m_machineInfo.machineUuid);
                    m_machineChannel = fmt::format("machine:{}", m_machineInfo.machineUuid);
                    requestCreditsStatusIfReady();
                }
            }
        }

        AuthStatus status {AuthStatus::AuthenticatedPaired};

        if (!isPaired) {
            status = AuthStatus::AuthenticatedUnpaired;
            clearPairedMachineContext();
        } else {
            requestMachineObject();
        }

        // Get release track url
        if (const auto configIt = json.find(JKEY_SOBJ_RELEASE_TRACK);
            configIt != json.end() && configIt->is_object()) {
            if (const auto urlIt = configIt->find(JKEY_SOBJ_RELEASE_URL);
                urlIt != configIt->end() && urlIt->is_string()) {
                urlIt->get_to(m_releaseTrackUrl);
            }
        }

        m_eventManager->push(std::make_shared<ConfigReceivedEvent>(json));

        if (m_status != status) {
            m_status = status;
            m_authCV.notify_all();
            emitPairingStatusEventIfChanged(isPaired);
        }

    } catch (const std::exception &e) {
        ERR("API error parsing config reply: {}", e.what());
    }
}

// Template implementation for generic HTTP request task
template<typename DeferredSetupT, typename HttpMethodT>
task_t Net::createHttpRequestTask(const char *requestType, StringCallback replyCallback,
                                  DeferredSetupT deferredSetup, HttpMethodT httpMethod,
                                  std::vector<AuthStatus> allowedStatuses,
                                  bool includeFingerprintHash, bool resilientTransferTimeouts)
{
    return [this, requestType, callback = std::move(replyCallback),
            deferredSetup = std::move(deferredSetup), httpMethod = std::move(httpMethod),
            allowedStatuses = std::move(allowedStatuses), includeFingerprintHash,
            resilientTransferTimeouts]() {
        Error error {Error::ApiError};
        std::string reply;

        for (int i = 0; i < NUM_RETRIES; ++i) {
            {
                std::unique_lock lock(m_authMutex);
                m_authCV.wait(lock, [this, &allowedStatuses] {
                    return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed
                        || m_stop || checkAllowedStatuses(allowedStatuses);
                });
            }

            auto setupResult = deferredSetup();
            auto url = std::get<0>(setupResult);

            if (!checkAllowedStatuses(allowedStatuses)) {
                if (m_status == AuthStatus::AuthenticationFailed) {
                    DBG("Can't send {} request to {}, authentication failed!", requestType,
                        url.str());
                    error = Error::AuthFailed;
                } else {
                    DBG("Can't send {} request to {}, not paired!", requestType, url.str());
                    error = Error::NotPaired;
                }
                break;
            }

            INF("API {} request: {}", requestType, url.str());

            auto hdrs = authHeader();
            if (includeFingerprintHash && !m_fingerprintHash.empty()) {
                hdrs[HDR_KEY_FINGERPRINT_HASH] = m_fingerprintHash;
            }
            auto r = httpMethod(url, std::get<1>(setupResult), hdrs, cpr::Timeout {NET_TIMEOUT},
                                resilientTransferTimeouts);
            reply = std::move(r.text);

            if (r.status_code >= 200 && r.status_code < 300) {
                DBG("API {} request to {} OK, {}", requestType, url.str(), reply);
                error = Error::Success;
                break;
            }

            ERR("API {} request to {} FAILED: code={}, {}, reply: {}", requestType, url.str(),
                r.status_code, r.error.message, reply);

            if (r.status_code == 0) {
                std::this_thread::sleep_for(1000ms);
                continue;
            }

            if (r.status_code != 401) {
                break;
            }

            {
                std::scoped_lock lock(m_authMutex);
                if (m_status == AuthStatus::NotAuthenticated
                    || m_status == AuthStatus::Authenticating
                    || m_status == AuthStatus::AuthenticationFailed) {
                    continue;
                }
                m_status = AuthStatus::NotAuthenticated;
            }
            stopTokenRefreshTimer();
            auto auth = createAuthenticateTask();
            auth();
        }

        if (callback) {
            callback(error, std::move(reply));
        }
    };
}

task_t Net::createGetRequestTask(StringCallback replyCallback, deferred_get_setup_t deferredSetup,
                                 std::vector<AuthStatus> allowedStatuses)
{
    return createHttpRequestTask(
            REST_GET, std::move(replyCallback), std::move(deferredSetup),
            [this](const cpr::Url &url, const cpr::Parameters &params, const cpr::Header &header,
                   const cpr::Timeout &timeout, bool resilient) {
                if (resilient) {
                    return cpr::Get(url, params, header, cpr::Timeout {NET_TRANSFER_TOTAL_TIMEOUT},
                                    cpr::ConnectTimeout {NET_CONNECT_TIMEOUT},
                                    cpr::LowSpeed {NET_TRANSFER_LOW_SPEED_BPS,
                                                   NET_TRANSFER_LOW_SPEED_STALL_TIME},
                                    sslOptions());
                }
                return cpr::Get(url, params, header, timeout, sslOptions());
            },
            std::move(allowedStatuses));
}

task_t Net::createPostRequestTask(StringCallback replyCallback, deferred_post_setup_t deferredSetup,
                                  std::vector<AuthStatus> allowedStatuses,
                                  bool includeFingerprintHash)
{
    return createHttpRequestTask(
            REST_POST, std::move(replyCallback), std::move(deferredSetup),
            [this](const cpr::Url &url, const cpr::Body &body, const cpr::Header &header,
                   const cpr::Timeout &timeout, bool resilient) {
                if (resilient) {
                    return cpr::Post(url, body, header, cpr::Timeout {NET_TRANSFER_TOTAL_TIMEOUT},
                                     cpr::ConnectTimeout {NET_CONNECT_TIMEOUT},
                                     cpr::LowSpeed {NET_TRANSFER_LOW_SPEED_BPS,
                                                    NET_TRANSFER_LOW_SPEED_STALL_TIME},
                                     sslOptions());
                }
                return cpr::Post(url, body, header, timeout, sslOptions());
            },
            std::move(allowedStatuses), includeFingerprintHash);
}

task_t Net::createPostMultipartRequestTask(StringCallback replyCallback,
                                           deferred_post_multipart_setup_t deferredSetup,
                                           std::vector<AuthStatus> allowedStatuses)
{
    return createHttpRequestTask(
            REST_POST, std::move(replyCallback), std::move(deferredSetup),
            [this](const cpr::Url &url, const SafeMultipart &multipart, cpr::Header header,
                   const cpr::Timeout &timeout, bool resilient) {
                header[HDR_KEY_CONTENT_TYPE] = HDR_VAL_CONTENT_MULTIPART;
                if (resilient) {
                    return cpr::Post(url, multipart.get(), header,
                                     cpr::Timeout {NET_TRANSFER_TOTAL_TIMEOUT},
                                     cpr::ConnectTimeout {NET_CONNECT_TIMEOUT},
                                     cpr::LowSpeed {NET_TRANSFER_LOW_SPEED_BPS,
                                                    NET_TRANSFER_LOW_SPEED_STALL_TIME},
                                     sslOptions());
                }
                return cpr::Post(url, multipart.get(), header, timeout, sslOptions());
            },
            std::move(allowedStatuses), false, true);
}

task_t Net::createPatchRequestTask(StringCallback replyCallback,
                                   deferred_patch_setup_t deferredSetup,
                                   std::vector<AuthStatus> allowedStatuses)
{
    return createHttpRequestTask(
            REST_PATCH, std::move(replyCallback), std::move(deferredSetup),
            [this](const cpr::Url &url, const cpr::Body &body, const cpr::Header &header,
                   const cpr::Timeout &timeout, bool resilient) {
                if (resilient) {
                    return cpr::Patch(url, body, header, cpr::Timeout {NET_TRANSFER_TOTAL_TIMEOUT},
                                      cpr::ConnectTimeout {NET_CONNECT_TIMEOUT},
                                      cpr::LowSpeed {NET_TRANSFER_LOW_SPEED_BPS,
                                                     NET_TRANSFER_LOW_SPEED_STALL_TIME},
                                      sslOptions());
                }
                return cpr::Patch(url, body, header, timeout, sslOptions());
            },
            std::move(allowedStatuses));
}

task_t Net::createPatchMultipartRequestTask(StringCallback replyCallback,
                                            deferred_patch_multipart_setup_t deferredSetup,
                                            std::vector<AuthStatus> allowedStatuses,
                                            bool includeFingerprintHash)
{
    return createHttpRequestTask(
            REST_PATCH, std::move(replyCallback), std::move(deferredSetup),
            [this](const cpr::Url &url, const SafeMultipart &multipart, cpr::Header header,
                   const cpr::Timeout &timeout, bool resilient) {
                header[HDR_KEY_CONTENT_TYPE] = HDR_VAL_CONTENT_MULTIPART;
                if (resilient) {
                    return cpr::Patch(url, multipart.get(), header,
                                      cpr::Timeout {NET_TRANSFER_TOTAL_TIMEOUT},
                                      cpr::ConnectTimeout {NET_CONNECT_TIMEOUT},
                                      cpr::LowSpeed {NET_TRANSFER_LOW_SPEED_BPS,
                                                     NET_TRANSFER_LOW_SPEED_STALL_TIME},
                                      sslOptions());
                }
                return cpr::Patch(url, multipart.get(), header, timeout, sslOptions());
            },
            std::move(allowedStatuses), includeFingerprintHash, true);
}

task_t Net::createDownloadFileTask(StringCallback replyCallback, std::string url,
                                   std::string filename, HttpHeaders extraHeaders)
{
    return [this, callback = std::move(replyCallback), url = std::move(url),
            filename = std::move(filename), extraHeaders = std::move(extraHeaders)]() {
        Error error {Error::ApiError};
        std::string reply;
        int statusCode = 0;

        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            ERR("API Can't open file for writing: {}", filename);
            error = Error::FileError;
        } else {
            const auto fullUrl = this->url(url);
            const bool isInternal =
                    isInternalDownloadForAuth(fullUrl.str(), m_hostname, m_deviceInfo);

            const auto elidedUrl = elideUrl(fullUrl.str());

            for (int i = 0; i < NUM_RETRIES; ++i) {
                INF("API Download file: {}", elidedUrl);

                auto headers = isInternal ? authHeader() : cpr::Header {};
                for (const auto &[k, v] : extraHeaders) {
                    headers[k] = v;
                }

                auto r = cpr::Download(file, fullUrl, cpr::Timeout {NET_TRANSFER_TOTAL_TIMEOUT},
                                       cpr::ConnectTimeout {NET_CONNECT_TIMEOUT},
                                       cpr::LowSpeed {NET_TRANSFER_LOW_SPEED_BPS,
                                                      NET_TRANSFER_LOW_SPEED_STALL_TIME},
                                       headers, sslOptions());
                reply = std::move(r.text);
                statusCode = r.status_code;

                if (statusCode == 200) {
                    DBG("API Download file: ok, {}", reply);
                    error = Error::Success;
                    break;
                }

                error = Error::ApiError;
                ERR("API Download file failed: code={}, message: {}, reply: {}, url: {}",
                    statusCode, r.error.message, reply, elidedUrl);

                if (statusCode >= 400) {
                    break;
                }
            }
        }
        file.close();

        if (callback) {
            callback(error,
                     fmt::format("HTTP CODE: {}, url: {}, to file: {}", statusCode, url, filename));
        }
    };
}

task_t Net::createDownloadBufferTask(VectorCallback replyCallback, std::string url,
                                     size_t reserveBufferSize, HttpHeaders extraHeaders)
{
    return [this, callback = std::move(replyCallback), url = std::move(url), reserveBufferSize,
            extraHeaders = std::move(extraHeaders)]() {
        Error error {Error::ApiError};
        std::vector<uint8_t> buffer;
        if (reserveBufferSize > 0) {
            buffer.reserve(reserveBufferSize);
        }

        const auto fullUrl = this->url(url);
        const bool isInternal = isInternalDownloadForAuth(fullUrl.str(), m_hostname, m_deviceInfo);

        auto headers = isInternal ? authHeader() : cpr::Header {};
        for (const auto &[k, v] : extraHeaders) {
            headers[k] = v;
        }

        const auto elidedUrl = elideUrl(fullUrl.str());

        for (int i = 0; i < NUM_RETRIES; ++i) {
            INF("API Download buffer: {}", elidedUrl);

            auto r = cpr::Get(
                    fullUrl, headers, cpr::Timeout {NET_TRANSFER_TOTAL_TIMEOUT},
                    cpr::ConnectTimeout {NET_CONNECT_TIMEOUT},
                    cpr::LowSpeed {NET_TRANSFER_LOW_SPEED_BPS, NET_TRANSFER_LOW_SPEED_STALL_TIME},
                    sslOptions());

            if (r.status_code == 200) {
                const auto *data = reinterpret_cast<const uint8_t *>(r.text.data());
                buffer.assign(data, data + r.text.size());

                if (buffer.size() > MAX_BUFFER_DOWNLOAD_SIZE) {
                    ERR("API Download buffer: too big, {} bytes", buffer.size());
                    buffer.clear();
                    error = Error::ApiError;
                    break;
                }

                DBG("API Download buffer: ok, {} bytes", buffer.size());
                error = Error::Success;
                break;
            }

            error = Error::ApiError;
            ERR("API Download buffer failed: code={}, message: {}, reply: {}, url: {}",
                r.status_code, r.error.message, r.text, elidedUrl);

            if (r.status_code >= 400) {
                break;
            }
        }

        if (callback) {
            callback(error, std::move(buffer));
        }
    };
}

cpr::Header Net::header() const
{
    return cpr::Header {{HDR_KEY_CACHE_CONTROL, HDR_VAL_NO_CACHE}};
}

cpr::Header Net::authHeader() const
{
    auto h = header();
    std::shared_lock tokenLock(m_tokenMutex);
    h[HDR_KEY_AUTHORIZATION] = HDR_VAL_BEARER + m_stoken;
    h[HDR_KEY_CONTENT_TYPE] = HDR_VAL_CONTENT_JSON;
    return h;
}

cpr::SslOptions Net::sslOptions() const
{
    return makeSslOptions();
}

bool Net::checkAllowedStatuses(const std::vector<AuthStatus> &allowedStatuses) const
{
    return std::ranges::any_of(allowedStatuses,
                               [this](AuthStatus status) { return status == m_status; });
}

void Net::processScoresAndPlayersProfiles(const json &val, GameSession &gameSession)
{
    // Process scores
    try {
        for (const auto &obj : val) {
            sb_player_t playerNum = obj.at(JKEY_SCR_POSITION).get<sb_player_t>();

            // Create or get the metadata entry
            auto &metadata = gameSession.scoresMetadata[playerNum];

            obj.at(JKEY_SCR_ID).get_to(metadata.id);
            obj.at(JKEY_SCR_IS_NFC_VERIFIED).get_to(metadata.isNfcVerified);

            if (const auto it = obj.find(JKEY_SCR_TOURNAMENT_UUID);
                it != obj.end() && it->is_string()) {
                metadata.tournamentUuid = it->get<std::string>();
            }
        }
    } catch (const std::exception &e) {
        ERR("Error parsing player score: {}", e.what());
    }

    // Process players profiles
    if (auto changedProfiles = m_playersManager.setProfiles(val, m_machineInfo.machineUuid)) {
        m_eventManager->push(std::make_shared<PlayersUpdatedEvent>(std::move(*changedProfiles)));
    }

    if (m_deviceInfo.autoDownloadPlayerPics) {
        const auto toDownload = m_playersManager.picturesToDownload();
        for (const auto &[playerNum, pictureUrl] : toDownload) {
            m_playersManager.setPicture(pictureUrl, Picture {});
            downloadBuffer(true, // Async download
                           [this, playerNum = playerNum,
                            pictureUrl = pictureUrl](Error error, std::vector<uint8_t> data) {
                               if (error == Error::Success) {
                                   m_playersManager.setPicture(pictureUrl, data);
                                   m_eventManager->push(std::make_shared<PlayerPictureReadyEvent>(
                                           playerNum, std::move(data)));
                               } else {
                                   ERR("Picture download failed: {}", static_cast<int>(error));
                                   m_playersManager.removePicture(pictureUrl);
                               }
                           },
                           pictureUrl, PICTURE_BUFFER_RESERVE,
                           {{HDR_KEY_ACCEPT_CONTENT, HDR_VAL_CONTENT_OCTET}});
        }
    }
}

bool Net::isActiveCentrifugoClient(const centrifugo::Client *client) const
{
    return client != nullptr && client == m_centrifugo.get();
}

void Net::pruneRetiredCentrifugoClients()
{
    const auto cutoff = steady_clock::now() - CF_RETIRED_CLIENT_GRACE_PERIOD;
    while (!m_retiredCentrifugoClients.empty()
           && m_retiredCentrifugoClients.front().retiredAt <= cutoff) {
        m_retiredCentrifugoClients.pop_front();
    }
}

void Net::retireCentrifugoClient()
{
    if (!m_centrifugo) {
        return;
    }

    pruneRetiredCentrifugoClients();
    m_retiredCentrifugoClients.push_back(
            {.retiredAt = steady_clock::now(), .client = std::move(m_centrifugo)});
}

void Net::centrifugoSetup(bool fetchFreshToken)
{
    // Create centrifugo client
    centrifugo::ClientConfig config;
    config.name = "scorbit_sdk";
    config.version = SCORBIT_SDK_VERSION;
    config.refreshTokenBeforeExpiry = 3min;
    auto initialCfToken = std::make_shared<std::string>();

    if (fetchFreshToken) {
        std::string authToken;
        {
            std::shared_lock lock(m_tokenMutex);
            authToken = m_stoken;
        }
        if (!authToken.empty()) {
            *initialCfToken =
                    getJwtToken(url(URL_SCORBITRON_CF_TOKEN).str(), authToken, sslOptions());
        }
    }

    config.getToken = [this, initialCfToken]() -> std::string {
        if (m_stop) {
            return {};
        }

        if (!initialCfToken->empty()) {
            auto token = std::move(*initialCfToken);
            initialCfToken->clear();
            return token;
        }

        std::shared_lock lock(m_tokenMutex);
        return getJwtToken(url(URL_SCORBITRON_CF_TOKEN).str(), m_stoken, sslOptions());
    };

    config.logHandler = [](centrifugo::LogEntry entry) {
        switch (entry.level) {
        case centrifugo::LogLevel::Debug:
            DBG("CF {}, {}", entry.message, entry.fields.dump());
            break;
        case centrifugo::LogLevel::Error:
            ERR("CF {}, {}", entry.message, entry.fields.dump());
            break;
        }
    };

    const auto cfUrl = fmt::format("{}/{}", m_cfHostname, URL_CENTRIFUGO);
    INF("API-CF centrifugo url: {}", cfUrl);
    m_centrifugo = std::make_unique<centrifugo::Client>(m_worker.centrifugoStrand(), cfUrl, config);
    auto *const client = m_centrifugo.get();
    const auto withActiveClient = [this, client]<typename Callback>(Callback &&callback) {
        return [this, client, callback = std::forward<Callback>(callback)](auto &&...args) mutable {
            if (!isActiveCentrifugoClient(client)) {
                return;
            }
            callback(std::forward<decltype(args)>(args)...);
        };
    };
    INF("API-CF centrifugo debug 1");

    m_centrifugo->onSslContextConfigure([](boost::asio::ssl::context &ctx) {
        try {
            // Boost.Asio can parse PEM format directly from memory
            auto fs = cmrc::scorbit::get_filesystem();
            auto certFile = fs.open("cacert.pem");
            ctx.add_certificate_authority(boost::asio::buffer(certFile.begin(), certFile.size()));
            return true;
        } catch (const std::exception &e) {
            ERR("Failed to load embedded CA certificates: {}", e.what());
            return false;
        }
    });

    m_centrifugo->onError(withActiveClient([](const centrifugo::Error &error) {
        ERR("API-CF Error: ({}, {})", error.ec.value(), error.message);
    }));

    m_centrifugo->onConnecting(withActiveClient([](centrifugo::Error const &error) {
        INF("API-CF Connecting to Centrifugo server... ({}, {})", error.ec.value(), error.message);
    }));

    m_centrifugo->onConnected(withActiveClient([this] {
        if (m_stop) {
            return;
        }

        INF("API-CF Connected to Centrifugo!");
        pruneRetiredCentrifugoClients();
        requestCreditsStatusIfReady();
    }));

    m_centrifugo->onDisconnected(withActiveClient([this, withActiveClient](
                                                          centrifugo::Error const &error) {
        WRN("API-CF Disconnected from Centrifugo ({}, {})", error.ec.value(), error.message);

        if (m_stop) {
            return;
        }

        constexpr auto RECONNECT_DELAY = 10s;
        constexpr auto RESTART_DELAY = 100ms;

        if (m_restartCentrifugoPending.exchange(false)) {
            INF("API-CF rebuilding centrifugo client after disconnect");
            retireCentrifugoClient();
            m_worker.startTimer(Worker::Timer::CentrifugoReconnect, RESTART_DELAY, [this] {
                if (m_stop || m_centrifugo) {
                    return;
                }
                setupAndConnectCentrifugo(true);
            });
            return;
        }

        // Check CF codes here https://centrifugal.dev/docs/server/codes
        switch (error.ec.value()) {
        case 3500:
        case 3501:
        case 3502:
            if (!m_stop) {
                INF("API-CF reset and setup centrifugo client in {}", RECONNECT_DELAY);
                m_worker.startTimer(Worker::Timer::CentrifugoReconnect, RECONNECT_DELAY,
                                    withActiveClient([this] { setupAndConnectCentrifugo(true); }));
                // m_worker.post([this]() { m_centrifugo.reset(); }); // TODO: if we need to reset?
            }
            break;

        default:
            break;
        }
    }));

    m_centrifugo->onSubscribing(withActiveClient([](const std::string &channel) {
        INF("API-CF Subscribing to channel: {}...", channel);
    }));

    m_centrifugo->onSubscribed(withActiveClient([](const std::string &channel) {
        INF("API-CF Subscribed successfully to channel: {}", channel);
    }));

    m_centrifugo->onUnsubscribed(withActiveClient([](const std::string &channel) {
        INF("API-CF Unsubscribed from channel: {}", channel);
    }));

    m_centrifugo->onPublication(withActiveClient([this](const std::string &channel,
                                                        centrifugo::Publication const &pub) {
        if (m_stop) {
            return;
        }

        try {
            INF("API-CF Publication received on channel: {}, data: {}, offset: {}", channel,
                pub.data.dump(), pub.offset);
            if (pub.info) {
                INF("API-CF Publication info, from user: {}, client: {}", pub.info->user,
                    pub.info->client);
            }

            if (channel.starts_with(CF_CHN_CONTROL_MACHINE)) {
                const auto &j = pub.data;
                if (const auto payloadIt = j.find(JKEY_CHN_PAYLOAD);
                    payloadIt != j.end() && payloadIt->is_object()) {
                    const auto type = j.value(JKEY_CHN_TYPE, "");

                    if (type == JVAL_CHN_TYPE_START_GAME) {
                        const int playerCount = payloadIt->value(JKEY_SESS_PLAYER_COUNT, 1);
                        setNumberOfPlayersRequested(playerCount);
                        m_eventManager->push(
                                std::make_shared<GameStartRequestedEvent>(playerCount));
                    } else if (type == JVAL_TYPE_ACTION) {
                        const auto method = payloadIt->value(JKEY_METHOD, "");
                        const auto name = payloadIt->value(JKEY_ACTION_NAME, "");
                        if (method == JVAL_METHOD_GET) {
                            if (name == JVAL_ACITON_GET_SCORBITRON_SESSION) {
                                const auto url = payloadIt->value(JKEY_URL, "");
                                const auto sessionUuid = parseUrlUuid(url, URL_SESSIONS_ID);
                                requestSessionData(sessionUuid);
                            }
                        } else if (method == JVAL_METHOD_MSG) {
                            requestCreditsStatusEvent(); // TODO add extra condition
                        }
                    } else if (type == JVAL_CHN_TYPE_ADD_CREDITS) {
                        const int credits = payloadIt->value(JKEY_CREDITS_COUNT, 1);
                        const auto transaction =
                                payloadIt->value(JKEY_CREDITS_TRANSACTION, std::string {});
                        m_eventManager->push(
                                std::make_shared<CreditsAddRequestedEvent>(credits, transaction));
                    } else if (type == JVAL_CHN_TYPE_DIAG_PROBE) {
                        handleDiagnosticProbe(*payloadIt);
                    } else if (type == JVAL_CHN_TYPE_DIAG_CAPTURE_START) {
                        handleDiagnosticCaptureStart(*payloadIt);
                    } else if (type == JVAL_CHN_TYPE_DIAG_CAPTURE_STOP) {
                        handleDiagnosticCaptureStop(*payloadIt);
                    } else {
                        WRN("API-CF Unknown publication type: {}", type);
                    }
                }
            } else if (channel.starts_with(CF_CHN_CONTROL_SCORBITRON)) {
                const auto &j = pub.data;
                if (const auto payloadIt = j.find(JKEY_CHN_PAYLOAD);
                    payloadIt != j.end() && payloadIt->is_object()) {
                    const auto type = j.value(JKEY_CHN_TYPE, "");
                    if (type == JVAL_TYPE_ACTION) {
                        const auto method = payloadIt->value(JKEY_METHOD, "");
                        const auto name = payloadIt->value(JKEY_ACTION_NAME, "");
                        if (method == JVAL_METHOD_SIGNAL
                            && name == JVAL_ACTION_UPLOAD_DIAGNOSTICS) {
                            INF("API-CF Diagnostics upload requested via control channel");
                            m_eventManager->push(
                                    std::make_shared<DiagnosticsUploadRequestedEvent>(false));
                        } else if (method == JVAL_METHOD_SIGNAL
                                   && name == JVAL_ACTION_SCORBITRON_PAIRED) {
                            INF("API-CF Scorbitron paired signal received");
                            onPaired();
                        } else if (method == JVAL_METHOD_SIGNAL
                                   && name == JVAL_ACTION_SCORBITRON_UNPAIRED) {
                            INF("API-CF Scorbitron unpaired signal received");
                            onUnpaired();
                        } else if (method == JVAL_METHOD_GET
                                   && name == JVAL_ACTION_CONFIG_REFRESH) {
                            INF("API-CF Config refresh requested via control channel");
                            getConfig();
                        }
                    }
                }
            }
        } catch (const std::exception &e) {
            ERR("API-CF Error parsing publication data: {}", e.what());
        }
    }));
}

void Net::centrifugoConnect()
{
    if (m_stop || !m_centrifugo) {
        return;
    }

    if (m_centrifugo->state() != centrifugo::ConnectionState::Disconnected) {
        DBG("API-CF connect skipped, current state={}", static_cast<int>(m_centrifugo->state()));
        return;
    }

    if (auto const res = m_centrifugo->connect(); !res) {
        ERR("API-CF Failed to connect to Centrifugo: ({}, {})", res.error().ec.value(),
            res.error().message);
    }
}

void Net::setupAndConnectCentrifugo(bool fetchFreshToken)
{
    pruneRetiredCentrifugoClients();
    centrifugoSetup(fetchFreshToken);
    centrifugoConnect();
}

void Net::restartCentrifugo()
{
    m_worker.stopTimer(Worker::Timer::CentrifugoReconnect);

    if (!m_centrifugo) {
        m_restartCentrifugoPending = false;
        setupAndConnectCentrifugo(true);
        return;
    }

    if (m_centrifugo->state() == centrifugo::ConnectionState::Disconnected) {
        m_restartCentrifugoPending = false;
        retireCentrifugoClient();
        setupAndConnectCentrifugo(true);
        return;
    }

    m_restartCentrifugoPending = true;
    m_centrifugo->disconnect();
}

std::optional<std::chrono::seconds> Net::getTimeUntilTokenExpiration() const
{
    std::shared_lock tokenLock(m_tokenMutex);
    return getJwtTokenTimeUntilExpiration(m_stoken);
}

void Net::createNfcNonces()
{
    if (!m_isNfcCapable) {
        return;
    }

    m_worker.post(createPostRequestTask(
            [this](Error error, std::string reply) {
                if (error == Error::Success) {
                    INF("API create NFC nonces: ok");
                    try {
                        json j = json::parse(reply);
                        if (const auto it = j.find(JVAL_NONCES); it != j.end() && it->is_array()) {
                            {
                                std::scoped_lock lock(m_noncesMutex);
                                it->get_to(m_nonces);
                            }
                            setNfcTag();
                            INF("API created {} NFC nonces", m_nonces.size());
                        } else {
                            ERR("API create NFC nonces: can't find nonces in reply");
                        }
                    } catch (const std::exception &e) {
                        ERR("API error parsing NFC nonces reply: {}", e.what());
                    }
                } else {
                    ERR("API create NFC nonces: failed, error code: {}, reply: {}",
                        static_cast<int>(error), reply);
                }
            },
            [this]() {
                const auto endpoint = url(URL_SCORBITRON_NFC_NONCE_CREATE);
                INF("API create NFC nonces: requesting new batch...");

                return make_tuple(endpoint, cpr::Body {});
            },
            {
                    AuthStatus::AuthenticatedUnpaired,
                    AuthStatus::AuthenticatedPaired,
            }));
}

void Net::startNfcCheckTimer()
{
    m_worker.startTimer(Worker::Timer::NfcCheckTag, NFC_CHECK_TIME, [this] {
        startNfcCheckTimer();

        if (m_probesManager) {
            const auto isNfcTagRead = std::invoke([this]() {
                std::scoped_lock lock {m_nfcMutex};
                return m_probesManager->isNfcTagRead();
            });

            if (isNfcTagRead) {
                setNfcTag();
            }
        }
    });
}

void Net::setNfcTag()
{
    const auto tag = fmt::format(URL_NFC_TAG, fmt::arg("machine_uuid", m_machineInfo.machineUuid),
                                 fmt::arg("nonce", consumeNonce()));

    const auto ok = std::invoke([this, &tag]() {
        std::scoped_lock lock {m_nfcMutex};
        return m_probesManager->setNfcTag(tag);
    });

    if (ok) {
        INF("NFC tag set ok: {}", tag);
    }
}

void Net::checkNfcBootReason()
{
    if (m_probesManager && m_isNfcCapable) {
        m_worker.stopTimer(Worker::Timer::NfcBootReason);

        const auto bootReason = std::invoke([this]() {
            std::scoped_lock lock {m_nfcMutex};
            return m_probesManager->probesBootReason(nfc::ProbeType::NFC);
        });

        if (bootReason && *bootReason != m_lastNfcBootReason) {
            INF("{}", *bootReason);
            m_lastNfcBootReason = *bootReason;
        }

        m_worker.startTimer(Worker::Timer::NfcBootReason, NFC_BOOT_REASON_DELAY,
                            [this]() { checkNfcBootReason(); });
    }
}

void Net::requestCreditsStatusEvent()
{
    m_eventManager->push(std::make_shared<CreditsStatusRequestedEvent>());
}

void Net::requestCreditsStatusIfReady()
{
    if (m_stop || !m_centrifugo || m_machineChannel.empty()) {
        return;
    }
    if (m_centrifugo->state() != centrifugo::ConnectionState::Connected) {
        return;
    }
    requestCreditsStatusEvent();
}

void Net::requestFirmwaresList()
{
    if (m_deviceInfo.provider != PROVIDER_SCORBITRON
        && m_deviceInfo.provider != PROVIDER_VSCORBITRON)
        return;

    m_worker.postQueue(createGetRequestTask(
            [this](Error error, std::string reply) {
                if (error == Error::Success) {
                    INF("API request firmwares list: ok, {}", reply);
                    m_eventManager->push(std::make_shared<FirmwaresListReceivedEvent>(reply));
                } else {
                    ERR("API request firmwares list: failed, error code: {}, reply: {}",
                        static_cast<int>(error), reply);
                }
            },
            [this]() {
                const auto endpoint = url(URL_SCORBITRON_FIRMWARES_LIST);
                INF("API request firmwares list...");

                return make_tuple(endpoint, cpr::Parameters {{"per_page", "100"}});
            }));
}
void Net::checkSystemTimeAccuracy(int64_t timestamp) const
{
    // This function can be used only once per application lifetime
    static bool checked {false};
    if (checked) {
        return;
    }
    checked = true;

    const auto systemTime = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    const auto delta = std::abs(systemTime - timestamp);

    if (delta > MAX_SYSTEM_TIME_DRIFT_SECONDS) {
        INF("System time appears to be inaccurate! Net time: {}, system time: {}, drift: {} "
            "seconds. Setting new system date time",
            timestamp, systemTime, delta);

        // Set new date time
        if (setSystemTime(timestamp)) {
            INF("System date time set OK");
        } else {
            WRN("Couldn't set system date time");
        }
    }
}

void Net::updateDiscoveryDescription()
{
    const auto providerVersion = std::invoke([this]() {
        if (m_deviceInfo.provider == PROVIDER_SCORBITRON
            || m_deviceInfo.provider == PROVIDER_VSCORBITRON) {
            return fmt::format("scorbitd {}", m_deviceInfo.scorbitdVersion);
        }
        return fmt::format("{} {}", m_deviceInfo.provider, m_deviceInfo.gameCodeVersion);
    });

    if (m_probesManager->setDiscoveryDescription(m_deviceInfo.serialNumber, m_machineInfo.title,
                                                 providerVersion, m_deviceInfo.machineTitle)) {
        INF("Discovery description updated machine: {}, provider: {}, extra info: {}",
            m_machineInfo.title, providerVersion, m_deviceInfo.machineTitle);
    }
}

} // namespace detail
} // namespace scorbit
