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
#include "logger.h"
#include "updater.h"
#include "safe_multipart.h"
#include "utils/bytearray.h"
#include "utils/mac_address.h"
#include "utils/date_time_parser.h"
#include "utils/jwt_parser.h"
#include <scorbit_sdk/net_types.h>
#include <scorbit_sdk/version.h>
#include <fmt/format.h>
#include <openssl/sha.h>
#include <cpr/cpr.h>
#include <boost/uuid.hpp>
#include <boost/filesystem.hpp>
#include <boost/url/url_view.hpp>
#include <boost/url/parse.hpp>
#include <optional>

using namespace std;
namespace fs = boost::filesystem;
using json = nlohmann::json;

namespace scorbit {
namespace detail {

// constexpr auto HEARTBEAT_URL {"heartbeat/"};
constexpr auto SESSION_CSV_URL {"session_log/"};
// constexpr auto LOCAL_TOP_SCORES_URL {"venuemachine/{venuemachine_id}/top_scores/"};

constexpr auto PAIRING_DEEPLINK {"https://scorbit.link/"
                                 "qrcode?$deeplink_path={manufacturer_prefix}"
                                 "&machineid={scorbit_machine_id}&uuid={scorbitron_uuid}"};
// constexpr auto CLAIM_DEEPLINK {"https://scorbit.link/qrcode?$deeplink_path={venuemachine_id}"
//                                "&opdb={opdb_id}&position={player_number}"};

using namespace std::chrono_literals;
constexpr auto NET_TIMEOUT = 14s;
constexpr auto HEARTBEAT_TIME = 10s;
constexpr auto NUM_RETRIES = 3;
constexpr auto REFRESH_TOKEN_BEFORE_EXPIRY = 5min; // Refresh token when 5 minutes remain

constexpr auto MAX_BUFFER_DOWNLOAD_SIZE = 10 * 1024 * 1024; // 10 MB max size to download to memory
constexpr auto PICTURE_BUFFER_RESERVE = 300 * 1024;         // 300 KB reserve for picture download

auto noop_task = []() { };

string getSignature(const SignerCallback &signer, const std::string &uuid,
                    const std::string &timestamp)
{
    ByteArray message(removeSymbols(uuid, "-{}"));
    ByteArray timestampBytes(timestamp.cbegin(), timestamp.cend());
    message.insert(message.end(), timestampBytes.cbegin(), timestampBytes.cend());

    Digest digest;
    SHA256(message.data(), message.size(), digest.data());

    Signature signature = signer(digest);
    if (signature.empty()) {
        ERR("Can't sign message, signer callback returned error");
        return string {};
    }

    return ByteArray(signature).hex();
}

std::string getJwtToken(const std::string &url, const std::string &authToken)
{
    INF("API-CF getting JWT token from: {}", url);

    auto r = cpr::Get(cpr::Url {url},
                      cpr::Header {{HDR_KEY_AUTHORIZATION, HDR_VAL_BEARER + authToken}},
                      cpr::Timeout {NET_TIMEOUT});

    if (r.status_code != 200) {
        ERR("API-CF failed to get JWT token: HTTP {} - {}", r.status_code, r.error.message);
    } else {
        try {
            const auto json = json::parse(r.text);
            if (const auto it = json.find(JKEY_CF_TOKEN); it != json.end() && it->is_string()) {
                const auto token = it->get<std::string>();
                INF("API-CF received JWT token successfully");
                return token;
            } else {
                ERR("API-CF JWT token not found in response");
            }
        } catch (const std::exception &e) {
            ERR("API-CF failed to parse JWT token response: {}", e.what());
        }
    }
    return {};
}

// --------------------------------------------------------------------------------

Net::Net(SignerCallback signer, DeviceInfo deviceInfo, bool useEncryptedKey)
    : m_signer(std::move(signer))
    , m_deviceInfo(std::move(deviceInfo))
    , m_updater(*this, useEncryptedKey)
    , m_eventManager(std::make_unique<EventManager>(m_worker.eventsStrand()))
{
    setHostname(m_deviceInfo.hostname, "");

    // Verify that mandatory "provider" field in deviceInfo is set
    if (m_deviceInfo.provider.empty()) {
        ERR("Provider is not set");
        // TODO: Set an appropriate error status
        return;
    }

    // Verify that mandatory "provider" field in deviceInfo is set for manufacturers (except
    // scorbitron, vscorbitron)
    if (m_deviceInfo.provider != PROVIDER_SCORBITRON
        && m_deviceInfo.provider != PROVIDER_VSCORBITRON) {
        if (m_deviceInfo.machineId == 0) {
            ERR("Machine ID not set");
            // TODO: Set an appropriate error status
            return;
        }
    }

    // Parse UUID to if it's in correct format
    if (!m_deviceInfo.uuid.empty()) {
        const auto originalUuid = m_deviceInfo.uuid;
        m_deviceInfo.uuid = parseUuid(originalUuid);
        if (m_deviceInfo.uuid.empty()) {
            ERR("Given UUID is not in correct format: {}", originalUuid);
            // TODO: set error status
            return;
        }
    } else {
        const auto macAddress = getMacAddress();
        m_deviceInfo.uuid = deriveUuid(fmt::format("{}|{}", m_deviceInfo.provider, macAddress));
        INF("Derived UUID: {} from mac address: {}", m_deviceInfo.uuid, macAddress);
    }

    centrifugoSetup();

    // Start worker thread
    m_worker.start();
}

Net::~Net()
{
    stopHeartbeatTimer();
    stopTokenRefreshTimer();
    m_stop = true;
    m_authCV.notify_all();
}

void Net::setEventCallback(EventCallback &&callback)
{
    m_eventManager->setEventCallback(std::move(callback));
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

void Net::sessionCreate(const GameData &data, GameStartOrigin origin)
{
    {
        std::lock_guard lock(m_gameSessionsMutex);
        auto &session = m_gameSessions[data.id];
        session.gameData = data;
        session.history.push_back(data);
    }

    INF("API post queue create session, id: {}", data.id);
    m_worker.postQueue(createSessionCreateTask(data.id, origin));
}

void Net::sessionUpdate(const GameData &data, bool uploadHistoryLog)
{
    INF("API post queue update session, id: {}, upload history logs: {}", data.id,
        uploadHistoryLog);
    m_worker.postQueue(createSessionUpdateTask(data.id, uploadHistoryLog));
}

void Net::sendGameData(const detail::GameData &data, bool isGameJustFinished)
{
    const auto sessionId = data.id;
    std::string sessionUuid;
    GameSession *gameSession = nullptr;
    {
        std::lock_guard lock(m_gameSessionsMutex);
        gameSession = &m_gameSessions[sessionId];
        gameSession->gameData = data;
        gameSession->history.push_back(data);
        sessionUuid = gameSession->sessionUuid;
    }

    const auto &gameData = gameSession->gameData;

    // Ensure that only single task in the queue (while another can be running).
    // However, if game session is finished (not active), post task anyway, because this is the last
    // task for that game session.

    // TODO: check if centrifugo is connected, if not, skip sending game data
    if (!sessionUuid.empty()
        && m_centrifugo->state() == centrifugo::ConnectionState::Connected) {
        // m_worker.postGameDataQueue(createGameDataTask(data.id));

        // TODO: check if it's needed to lock mutex here
        const auto sessionCounter = ++gameSession->sessionCounter;
        const auto modes = json::parse(gameData.modes.jsonStr());
        const auto updatedAt = to_iso8601(chrono::system_clock::now());
        const auto createdAt = to_iso8601(gameSession->startedSystemTime);

        json::array_t scores;
        for (const auto &[playerNum, playerState] : gameData.players) {
            json playerProfileJson = nullptr;
            if (const auto playerProfile = m_playersManager.profile(playerNum)) {
                playerProfileJson = {{JKEY_USERNAME, playerProfile->username},
                                     {JKEY_AVATAR, playerProfile->pictureUrl}};
            }

            const auto valBallInProgress =
                    gameData.isGameActive && (gameData.activePlayer == playerNum);

            json playerScoreJson {{JKEY_SCR_POSITION, playerNum},
                                  {JKEY_SCR_ID, gameSession->scoresMetadata[playerNum].id},
                                  {JKEY_SCR_IS_NFC_VERIFIED,
                                   gameSession->scoresMetadata[playerNum].isNfcVerified},
                                  {JKEY_SCR_PLAYER, playerProfileJson},
                                  {JKEY_SCR_SCORE, playerState.score()},
                                  {JKEY_SCR_BALL, gameData.ball},
                                  {JKEY_SCR_BALL_IN_PROGRESS, valBallInProgress},
                                  {JKEY_SCR_MODES, modes}};

            scores.emplace_back(playerScoreJson);
        }

        const auto valType = (isGameJustFinished ? JVAL_SCR_GAME_END : JVAL_SCR_SCORE_UPDATE);
        const auto keyScores = (isGameJustFinished ? JKEY_SCR_FINAL_SCORES : JKEY_SCR_SCORES);

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
                         {JKEY_SCR_VENUE, m_machineInfo.venueUuid.empty()
                                                  ? json(nullptr)
                                                  : json(m_machineInfo.venueUuid)},
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
                    AuthStatus status {AuthStatus::AuthenticatedPaired};

                    const auto isPaired = [&json]() {
                        if (const auto it = json.find(JKEY_SCFG_IS_PAIRED);
                            it != json.end() && it->is_boolean()) {
                            return it->get<bool>();
                        }
                        return false;
                    }();

                    if (!isPaired) {
                        status = AuthStatus::AuthenticatedUnpaired;
                        m_machineInfo.opdbId.clear();
                        m_machineInfo.machineUuid.clear();
                        m_machineInfo.variantUuid.clear();
                    }

                    if (const auto it = json.find(JKEY_SCFG_SHORTCODE);
                        it != json.end() && it->is_string()) {
                        it->get_to(m_cachedShortCode);
                        m_shortCodeCV.notify_all();
                    }

                    if (const auto it = json.find(JKEY_SCFG_MACHINE_UUID);
                        it != json.end() && it->is_string()) {
                        it->get_to(m_machineInfo.machineUuid);
                        m_machineChannel = fmt::format("machine:{}", m_machineInfo.machineUuid);
                    }

                    if (const auto it = json.find(JKEY_SCFG_VARIANT_ID);
                        it != json.end() && it->is_string()) {
                        it->get_to(m_machineInfo.variantUuid);
                    }

                    if (const auto it = json.find(JKEY_SCFG_VENUE_ID);
                        it != json.end() && it->is_string()) {
                        it->get_to(m_machineInfo.venueUuid);
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

                    m_updater.checkNewVersionAndUpdate(json);

                    m_eventManager->push(std::make_shared<ConfigReceivedEvent>(json.dump()));

                    if (m_status != status) {
                        m_status = status;
                        m_authCV.notify_all();
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
                    AuthStatus::AuthenticatedCheckingPairing,
                    AuthStatus::AuthenticatedUnpaired,
                    AuthStatus::AuthenticatedPaired,
            }));
}

void Net::requestPairCode(StringCallback callback)
{
    // Return cached short code if available
    if (!m_cachedShortCode.empty()) {
        callback(Error::Success, m_cachedShortCode);
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

const string &Net::getPairDeeplink() const
{
    m_cachedPairDeeplink =
            fmt::format(PAIRING_DEEPLINK, fmt::arg("manufacturer_prefix", m_deviceInfo.provider),
                        fmt::arg("scorbit_machine_id", m_deviceInfo.machineId),
                        fmt::arg("scorbitron_uuid", m_deviceInfo.uuid));
    return m_cachedPairDeeplink;
}

const string &Net::getClaimDeeplink(int /*player*/) const
{
    // FIXME: disable claim deeplink for now, find out what will be claim deeplink format

    // if (m_machineInfo.venuemachineId == 0) {
    //     DBG("Venue machine ID is not set, make sure that the device is authenticated "
    //         "and paired");
    //     m_cachedCclaimDeeplink.clear();
    // } else {
    //     m_cachedCclaimDeeplink = fmt::format(
    //             CLAIM_DEEPLINK, fmt::arg("venuemachine_id", m_machineInfo.venuemachineId),
    //             fmt::arg("opdb_id", m_machineInfo.opdbId), fmt::arg("player_number", player));
    // }

    return m_cachedCclaimDeeplink;
}

const DeviceInfo &Net::deviceInfo() const
{
    return m_deviceInfo;
}

void Net::requestTopScores(sb_score_t /*scoreFilter*/, StringCallback /*callback*/)
{
    return; // FIXME: implement when API will be ready

    // m_worker.postQueue(createGetRequestTask(std::move(callback), [this, scoreFilter]() {
    //     const auto endpoint = url(fmt::format(
    //             LOCAL_TOP_SCORES_URL, fmt::arg("venuemachine_id",
    //             m_machineInfo.venuemachineId)));
    //     cpr::Parameters parameters;
    //     if (scoreFilter > 0) {
    //         parameters.Add({"score", fmt::format("{}", scoreFilter)});
    //     }

    //     return make_tuple(endpoint, parameters);
    // }));
}

void Net::requestUnpair(StringCallback callback)
{
    m_worker.postQueue(createPatchRequestTask(
            [this, callback = std::move(callback)](Error error, std::string reply) {
                if (error == Error::Success || error == Error::NotPaired) {
                    m_status = AuthStatus::AuthenticatedUnpaired;
                    error = Error::Success;
                }
                callback(error, reply);
            },
            [this]() {
                json j {
                        {JKEY_SCFG_SCORBITRON_MACHINE, nullptr},
                };

                const auto endpoint = url(URL_SCORBITRON_OBJECT);
                const auto body {j.dump()};
                INF("API requesting unpair with {}", body);

                return make_tuple(endpoint, cpr::Body {body});
            }));
}

void Net::download(StringCallback callback, const std::string &url, const std::string &filename)
{
    m_worker.postQueue(createDownloadFileTask(std::move(callback), url, filename));
}

void Net::downloadBuffer(VectorCallback callback, const std::string &url, size_t reserveBufferSize)
{
    m_worker.postQueue(createDownloadBufferTask(std::move(callback), url, reserveBufferSize));
}

PlayerProfilesManager &Net::playersManager()
{
    return m_playersManager;
}

task_t Net::createAuthenticateTask()
{
    return [this]() {
        std::lock_guard lock(m_authMutex);

        const auto startServices = !m_isRefreshingToken;

        if (m_status != AuthStatus::NotAuthenticated && !m_isRefreshingToken) {
            return;
        }

        m_isRefreshingToken = true;
        m_status = AuthStatus::Authenticating;
        std::string timestamp = std::to_string(std::chrono::seconds(std::time(nullptr)).count());
        ByteArray message(m_deviceInfo.uuid);

        for (;;) {
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

            INF("API authenticating to {}", m_hostname);

            auto r = cpr::Post(url(URL_SCORBITRON_TOKEN), cpr::Body {j.dump()},
                               cpr::Header {{HDR_KEY_CONTENT_TYPE, HDR_VAL_CONTENT_JSON}});

            if (m_stop) {
                m_status = AuthStatus::AuthenticationFailed;
                m_isRefreshingToken = false;
                m_authCV.notify_all();
                return;
            }

            if (r.status_code == 200) {
                try {
                    const auto json = json::parse(r.text);
                    {
                        std::unique_lock tokenLock(m_tokenMutex);
                        json[JKEY_SCORBITRON_TOKEN].get_to(m_stoken);
                    }

                    m_status = AuthStatus::AuthenticatedCheckingPairing;
                    INF("API authentication successful! Checking pairing status...");

                    startTokenRefreshTimer(); // Start/restart token refresh timer
                    m_authCV.notify_all();

                    if (startServices) {
                        getConfig(); // Get config after authentication, it also checks pair status
                        sendHeartbeat();
                        startHeartbeatTimer();
                        centrifugoConnect();
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
                INF("API authentication failed: code {}, {}, will retry with new timestamp",
                    r.status_code, r.error.message);
                timestamp = std::to_string(parseHttpDateToUnixTimestamp(r.header["Date"]));
            } else if (r.status_code == 0) {
                // Network error, retry
                ERR("API authentication network error: {}, will retry in 10s", r.error.message);
                std::this_thread::sleep_for(10s);
            } else {
                m_status = AuthStatus::AuthenticationFailed;
                stopTokenRefreshTimer();
                const auto msg = fmt::format("API authentication failed: code {}, {}",
                                             r.status_code, r.error.message);
                ERR("{}", msg);
                ERR("{}", r.text);
                // TODO: Sentry
                // SentryManager::message(msg);
                m_authCV.notify_all();
                break;
            }
        }
    };
}

task_t Net::updateConfigTask(const std::string &type, const std::string &version, bool installed,
                             std::optional<std::string> log)
{
    return [this, type, version, installed = std::move(installed), log = std::move(log)]() {
        for (int i = 0; i < NUM_RETRIES; ++i) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed || m_stop;
            });

            if (m_status == AuthStatus::AuthenticationFailed) {
                DBG("Can't send installed task, authentication failed!");
                break;
            }

            // Create json string
            json j {
                    {JKEY_SCFG_VERSION, version},
                    {JKEY_SCFG_TYPE, type},
                    {JKEY_SCFG_INSTALLED, installed},
            };
            if (log) {
                j[JKEY_SCFG_LOG] = *log;
            }

            const auto payload = j.dump();
            INF("API update config: {}", payload);

            // TODO: Sentry

            const auto r = cpr::Patch(url(URL_SCORBITRON_CONFIG), cpr::Body {payload}, authHeader(),
                                      cpr::Timeout {NET_TIMEOUT});

            if (r.status_code == 200) {
                INF("API update config response: {}", r.text);
                break;
            }

            ERR("API update config failed: code={}, {}", r.status_code, r.error.message);
            ERR("{}", r.text);

            if (r.status_code != 401) {
                break;
            }

            m_status = AuthStatus::NotAuthenticated;
            stopTokenRefreshTimer();
            auto auth = createAuthenticateTask();
            auth();
        }
    };
}

task_t Net::createSessionCreateTask(int sessionId, GameStartOrigin origin)
{
    INF("API session create for id: {}, started by: {} ...", sessionId, origin);
    int sessionCounter;
    GameSession *gameSession = nullptr;
    {
        std::lock_guard lock(m_gameSessionsMutex);
        if (m_gameSessions.count(sessionId) == 0) {
            // Nothing to do, session is already finished and removed
            ERR("API this error should not happen. Can't find session {} in game sessions",
                sessionId);
            return noop_task;
        }

        gameSession = &m_gameSessions[sessionId];
        sessionCounter = ++gameSession->sessionCounter;
    }

    const auto playerCount = gameSession->gameData.players.size();
    const int64_t elapsedMilliseconds =
            chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now()
                                                        - gameSession->startedTime)
                    .count();
    const auto currentDateTime = to_iso8601(chrono::system_clock::now());

    json j {
            {JKEY_SESS_PLAYER_COUNT, playerCount},
            {JKEY_SESS_SEQUENCE_NUMBER, sessionCounter},
            {JKEY_SESS_SESSION_TIME, elapsedMilliseconds},
            {JKEY_SESS_ACTIVE_ON, currentDateTime},
            {JKEY_SESS_USE_LOBBY, origin == GameStartOrigin::FromLobby},
    };

    auto deferredSetup = [this, body = j.dump()]() {
        return std::make_tuple(url(URL_SCORBITRON_SESSIONS), cpr::Body {body});
    };

    auto callback = [this, sessionId](Error error, std::string reply) {
        if (error == Error::Success) {
            INF("API create session: ok, id: {}, {}", sessionId, reply);

            try {
                json json = json::parse(reply);

                if (const auto it = json.find(JKEY_SESS_UUID);
                    it != json.end() && it->is_string()) {

                    GameSession *gameSession = nullptr;
                    {
                        std::lock_guard lock(m_gameSessionsMutex);
                        gameSession = &m_gameSessions[sessionId];
                    }

                    it->get_to(gameSession->sessionUuid);

                    INF("API created session id: {}, uuid: {}, address: {:x}", sessionId,
                        gameSession->sessionUuid, (uint64_t)&gameSession->gameData);

                    // Scores array will have players' profiles
                    if (const auto it = json.find("scores"); it != json.end()) {
                        processScoresAndPlayersProfiles(*it, *gameSession);
                    } else {
                        WRN("API create session: can't find scores in reply");
                    }
                } else {
                    ERR("API create session: can't find session UUID in reply");
                }
            } catch (const std::exception &e) {
                ERR("API error parsing game data reply: {}", e.what());
            }
        }
    };

    return createPostRequestTask(std::move(callback), std::move(deferredSetup));
}

task_t Net::createSessionUpdateTask(int sessionId, bool uploadHistoryLogs)
{
    GameSession *gameSession = nullptr;
    {
        std::lock_guard lock(m_gameSessionsMutex);
        if (m_gameSessions.count(sessionId) == 0) {
            // Nothing to do, session is already finished and removed
            ERR("API this error should not happen. Can't find session {} in game sessions",
                sessionId);
            return noop_task;
        }

        gameSession = &m_gameSessions[sessionId];
    }
    const auto &sessionUuid = gameSession->sessionUuid;
    if (sessionUuid.empty()) {
        // Session patch cancelled, session uuid is not ready yet
        return noop_task;
    }

    INF("API update session for id: {}, uuid: {} ...", sessionId, sessionUuid);

    const auto playerCount = gameSession->gameData.players.size();
    const int64_t elapsedMilliseconds =
            chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now()
                                                        - gameSession->startedTime)
                    .count();
    const auto currentDateTime = to_iso8601(chrono::system_clock::now());

    const auto filename = fmt::format("{}.{}", sessionUuid, SESS_LOG_EXTENSION);
    const auto isActive = gameSession->gameData.isGameActive;

    // Here we can't use json in case of uploading session log file, so we are using multipart
    cpr::Multipart formData {
            {JKEY_SESS_PLAYER_COUNT, std::to_string(playerCount)},
            {JKEY_SESS_SEQUENCE_NUMBER, std::to_string(gameSession->sessionCounter)},
            {JKEY_SESS_SESSION_TIME, std::to_string(elapsedMilliseconds)},
            {(isActive ? JKEY_SESS_ACTIVE_ON : JKEY_SESS_SETTLED_ON), currentDateTime},
    };

    // If the game is finished, set "active off" time
    if (!isActive) {
        formData.parts.push_back({JKEY_SESS_SUCCESSFULLY_COMPLETED, "True"});
    }

    // History CSV logs
    // This variable must be declared here to have same life length as formData, otherwise
    // cpr::Buffer will use invalid reference upon exit from "if" clause which will cause memory
    // corruption. Later, in SafeMultipart the cpr::Buffer will be copied
    const auto csv = gameHistoryToCsv(gameSession->history);
    if (uploadHistoryLogs) {
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
            std::lock_guard lock(m_gameSessionsMutex);
            if (!m_gameSessions[sessionId].gameData.isGameActive) {
                m_gameSessions.erase(sessionId);
            }
        } else {
            ERR("API update session: failed, id: {}, error code: {}", sessionId,
                static_cast<int>(error));
            // TODO: Sentry
            // FIXME: what to do with game session? Erase it or retry again?
        }
    };

    return createPatchMultipartRequestTask(std::move(callback), std::move(deferredSetup));
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
                std::lock_guard lockGameSession(m_gameSessionsMutex);
                isActiveSession = !m_gameSessions.empty();
            }
            const auto parameters =
                    cpr::Parameters {{"session_active", isActiveSession ? "true" : "false"}};

            // TODO: sentry

            INF("API sending heartbeat with session_active: {}", isActiveSession);

            const auto r = cpr::Get(url(HEARTBEAT_URL), parameters, authHeader(),
                                    cpr::Timeout {NET_TIMEOUT});

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
}

void Net::requestSessionData(const std::string &sessionUuid)
{
    INF("API post queue request session data, uuid: {} ...", sessionUuid);

    auto callback = [this, sessionUuid](Error error, std::string reply) {
        if (error == Error::Success) {
            INF("API get session data: ok, {}", reply);
            // Find out sessionId
            int sessionId = -1;
            for (const auto &[id, gameSession] : m_gameSessions) {
                if (gameSession.sessionUuid == sessionUuid) {
                    sessionId = id;
                    break;
                }
            }

            if (sessionId >= 0) {
                json j = json::parse(reply);
                if (const auto scoresIt = j.find(JKEY_SCR_SCORES);
                    scoresIt != j.end() && scoresIt->is_array()) {
                    GameSession *gameSession = nullptr;
                    {
                        std::lock_guard lock(m_gameSessionsMutex);
                        gameSession = &m_gameSessions[sessionId];
                    }
                    processScoresAndPlayersProfiles(*scoresIt, *gameSession);
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

    m_worker.postQueue(createGetRequestTask(std::move(callback), std::move(deferredSetup)));
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
    return [this, endpoint, filename, multipart = std::move(multipart)]() {
        for (int i = 0; i < NUM_RETRIES; ++i) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed || m_stop;
            });

            if (m_status != AuthStatus::AuthenticatedPaired) {
                DBG("API can't upload {}, device is not paired or authentication failed!",
                    filename);
                break;
            }

            INF("API upload to backend started: {} to {}", filename, endpoint);
            auto r = cpr::Post(url(endpoint), multipart.get(), authHeader());

            if (r.status_code == 200) {
                INF("API upload to backend finished: ok! {} to {}", filename, endpoint);
                break;
            }

            ERR("API try {} out of {} upload to backend failed! {} to {}, code={}, "
                "error message: {}",
                i + 1, NUM_RETRIES, filename, endpoint, r.status_code, r.error.message);
            ERR("{}", r.text);
        }
    };
}

// Template implementation for generic HTTP request task
template<typename DeferredSetupT, typename HttpMethodT>
task_t Net::createHttpRequestTask(const char *requestType, StringCallback replyCallback,
                                  DeferredSetupT deferredSetup, HttpMethodT httpMethod,
                                  std::vector<AuthStatus> allowedStatuses)
{
    return [this, requestType, callback = std::move(replyCallback),
            deferredSetup = std::move(deferredSetup), httpMethod = std::move(httpMethod),
            allowedStatuses = std::move(allowedStatuses)]() {
        Error error {Error::ApiError};
        std::string reply;

        for (int i = 0; i < NUM_RETRIES; ++i) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this, allowedStatuses] {
                return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed || m_stop
                    || checkAllowedStatuses(allowedStatuses);
            });

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

            auto r = httpMethod(url, std::get<1>(setupResult), authHeader(),
                                cpr::Timeout {NET_TIMEOUT});
            reply = std::move(r.text);

            if (r.status_code >= 200 && r.status_code < 300) {
                DBG("API {} request to {} OK, {}", requestType, url.str(), reply);
                error = Error::Success;
                break;
            }

            ERR("API {} request to {} FAILED: code={}, {}, reply: {}", requestType, url.str(),
                r.status_code, r.error.message, reply);

            if (r.status_code != 401) {
                break;
            }

            m_status = AuthStatus::NotAuthenticated;
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
            [](const cpr::Url &url, const cpr::Parameters &params, const cpr::Header &header,
               const cpr::Timeout &timeout) { return cpr::Get(url, params, header, timeout); },
            std::move(allowedStatuses));
}

task_t Net::createPostRequestTask(StringCallback replyCallback, deferred_post_setup_t deferredSetup,
                                  std::vector<AuthStatus> allowedStatuses)
{
    return createHttpRequestTask(
            REST_POST, std::move(replyCallback), std::move(deferredSetup),
            [](const cpr::Url &url, const cpr::Body &body, const cpr::Header &header,
               const cpr::Timeout &timeout) { return cpr::Post(url, body, header, timeout); },
            std::move(allowedStatuses));
}

task_t Net::createPatchRequestTask(StringCallback replyCallback,
                                   deferred_patch_setup_t deferredSetup,
                                   std::vector<AuthStatus> allowedStatuses)
{
    return createHttpRequestTask(
            REST_PATCH, std::move(replyCallback), std::move(deferredSetup),
            [](const cpr::Url &url, const cpr::Body &body, const cpr::Header &header,
               const cpr::Timeout &timeout) { return cpr::Patch(url, body, header, timeout); },
            std::move(allowedStatuses));
}

task_t Net::createPatchMultipartRequestTask(StringCallback replyCallback,
                                            deferred_patch_multipart_setup_t deferredSetup,
                                            std::vector<AuthStatus> allowedStatuses)
{
    return createHttpRequestTask(
            REST_PATCH, std::move(replyCallback), std::move(deferredSetup),
            [](const cpr::Url &url, const SafeMultipart &multipart, cpr::Header header,
               const cpr::Timeout &timeout) {
                header[HDR_KEY_CONTENT_TYPE] = HDR_VAL_CONTENT_MULTIPART;
                return cpr::Patch(url, multipart.get(), header, timeout);
            },
            std::move(allowedStatuses));
}

task_t Net::createDownloadFileTask(StringCallback replyCallback, std::string url,
                                   std::string filename)
{
    return [callback = std::move(replyCallback), url = std::move(url),
            filename = std::move(filename)]() {
        Error error {Error::ApiError};
        std::string reply;

        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            ERR("Can't open file for writing: {}", filename);
            error = Error::FileError;
        } else {
            for (int i = 0; i < NUM_RETRIES; ++i) {
                INF("Download file: {}", url);

                auto r = cpr::Download(file, cpr::Url {url}, cpr::Timeout {NET_TIMEOUT});
                reply = std::move(r.text);

                if (r.status_code == 200) {
                    DBG("Download file: ok, {}", reply);
                    error = Error::Success;
                    break;
                }

                error = Error::ApiError;
                ERR("Download file failed: code={}, message: {}, reply: {}", r.status_code,
                    r.error.message, reply);

                if (r.status_code >= 400) {
                    break;
                }
            }
        }
        file.close();

        if (callback) {
            callback(error, filename);
        }
    };
}

task_t Net::createDownloadBufferTask(VectorCallback replyCallback, std::string url,
                                     size_t reserveBufferSize)
{
    return [callback = std::move(replyCallback), url = std::move(url), reserveBufferSize]() {
        Error error {Error::ApiError};
        std::vector<uint8_t> buffer;
        if (reserveBufferSize > 0) {
            buffer.reserve(reserveBufferSize);
        }

        cpr::Session session;
        session.SetUrl(cpr::Url {url});
        session.SetTimeout(cpr::Timeout {NET_TIMEOUT});

        for (int i = 0; i < NUM_RETRIES; ++i) {
            INF("Download buffer: {}", url);

            cpr::Response r = session.Download(
                    cpr::WriteCallback {[&buffer](const std::string_view &data, intptr_t) -> bool {
                        if (buffer.size() + data.size() > MAX_BUFFER_DOWNLOAD_SIZE) {
                            ERR("Download buffer: too big, {} bytes", buffer.size() + data.size());
                            return false;
                        }
                        buffer.insert(buffer.end(), data.begin(), data.end());
                        return true;
                    }});

            if (r.status_code == 200) {
                DBG("Download buffer: ok, {} bytes", buffer.size());
                error = Error::Success;
                break;
            }

            error = Error::ApiError;
            ERR("Download buffer failed: code={}, message: {}", r.status_code, r.error.message);

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

bool Net::checkAllowedStatuses(const std::vector<AuthStatus> &allowedStatuses) const
{
    return std::any_of(begin(allowedStatuses), end(allowedStatuses),
                       [this](AuthStatus status) { return status == m_status; });
}

void Net::processScoresAndPlayersProfiles(const json &val, GameSession &gameSession)
{
    // Process scores
    try {
        for (const auto &obj : val) {
            sb_player_t playerNum = obj[JKEY_SCR_POSITION].get<sb_player_t>();
            obj[JKEY_SCR_ID].get_to(gameSession.scoresMetadata[playerNum].id);
            obj[JKEY_SCR_IS_NFC_VERIFIED].get_to(
                    gameSession.scoresMetadata[playerNum].isNfcVerified);
        }
    } catch (const std::exception &e) {
        ERR("Error parsing player score: {}", e.what());
    }

    // Process players profiles
    m_playersManager.setProfiles(val);

    if (m_deviceInfo.autoDownloadPlayerPics) {
        const auto toDownload = m_playersManager.picturesToDownload();
        for (const auto &[playerNum, pictureUrl] : toDownload) {
            // Queue picture to download
            // Set picture to empty, to avoid another download
            m_playersManager.setPicture(playerNum, Picture {});
            downloadBuffer(
                    [this, playerNum = playerNum](Error error, std::vector<uint8_t> data) {
                        if (error == Error::Success) {
                            m_playersManager.setPicture(playerNum, std::move(data));
                        } else {
                            ERR("Picture download failed: {}", static_cast<int>(error));
                            m_playersManager.removePicture(playerNum);
                        }
                    },
                    pictureUrl, PICTURE_BUFFER_RESERVE);
        }
    }
}

void Net::centrifugoSetup()
{
    // Create centrifugo client
    centrifugo::ClientConfig config;
    config.name = "scorbit_sdk";
    config.version = SCORBIT_SDK_VERSION;
    config.refreshTokenBeforeExpiry = 3min;
    config.getToken = [this]() -> std::string {
        // TODO: check if this unique_lock needed

        // std::unique_lock lock(m_authMutex);
        // m_authCV.wait(lock, [this] {
        //     return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed || m_stop;
        // });

        // Get JWT token for Centrifugo connection
        std::shared_lock lock(m_tokenMutex);
        return getJwtToken(url(URL_SCORBITRON_CF_TOKEN).str(), m_stoken);
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
    INF("API centrifugo url: {}", cfUrl);
    m_centrifugo = std::make_unique<centrifugo::Client>(m_worker.centrifugoStrand(), cfUrl, config);

    m_centrifugo->onError([](const centrifugo::Error &error) {
        ERR("API-CF Error: ({}, {})", error.ec.value(), error.message);
    });

    m_centrifugo->onConnecting([](centrifugo::Error const &error) {
        INF("API-CF Connecting to Centrifugo server... ({}, {})", error.ec.value(), error.message);
    });

    m_centrifugo->onConnected([] { INF("[API-CF Connected to Centrifugo!"); });

    m_centrifugo->onDisconnected([](centrifugo::Error const &error) {
        WRN("API-CF Disconnected from Centrifugo ({}, {})", error.ec.value(), error.message);
    });

    m_centrifugo->onSubscribing([](const std::string &channel) {
        INF("API-CF Subscribing to channel: {}...", channel);
    });

    m_centrifugo->onSubscribed([](const std::string &channel) {
        INF("API-CF Subscribed successfully to channel: {}", channel);
    });

    m_centrifugo->onUnsubscribed([](const std::string &channel) {
        INF("API-CF Unsubscribed from channel: {}", channel);
    });

    m_centrifugo->onPublication(
            [this](const std::string &channel, centrifugo::Publication const &pub) {
                try {
                    INF("API-CF Publication received on channel: {}, data: {}, offset: {}", channel,
                        pub.data.dump(), pub.offset);
                    if (pub.info) {
                        INF("API-CF Publication info, from user: {}, client: {}", pub.info->user,
                            pub.info->client);
                    }

                    if (channel.find(CF_CHN_CONTROL_MACHINE) == 0) {
                        const auto &j = pub.data;
                        if (const auto payloadIt = j.find(JKEY_CHN_PAYLOAD);
                            payloadIt != j.end() && payloadIt->is_object()) {
                            const auto type = j.value(JKEY_CHN_TYPE, "");

                            if (type == JVAL_CHN_TYPE_START_GAME) {
                                const int playerCount = payloadIt->value(JKEY_SESS_PLAYER_COUNT, 1);
                                emitGameStartRequested(playerCount);
                            } else if (type == JVAL_TYPE_ACTION) {
                                const auto method = payloadIt->value(JKEY_METHOD, "");
                                const auto name = payloadIt->value(JKEY_ACTION_NAME, "");
                                if (method == JVAL_METHOD_GET) {
                                    if (name == JVAL_ACITON_GET_SCORBITRON_SESSION) {
                                        const auto url = payloadIt->value(JKEY_URL, "");
                                        const auto sessionUuid = parseUrlUuid(url, URL_SESSIONS_ID);
                                        requestSessionData(sessionUuid);
                                    }
                                }
                            } else {
                                WRN("API-CF Unknown publication type: {}", type);
                            }
                        }
                    }
                } catch (const std::exception &e) {
                    ERR("API-CF Error parsing publication data: {}", e.what());
                }
            });
}

void Net::centrifugoConnect()
{
    if (auto const res = m_centrifugo->connect(); !res) {
        ERR("API-CF Failed to connect to Centrifugo: ({}, {})", res.error().ec.value(),
            res.error().message);
    }
}

std::optional<std::chrono::seconds> Net::getTimeUntilTokenExpiration() const
{
    std::shared_lock tokenLock(m_tokenMutex);
    return getJwtTokenTimeUntilExpiration(m_stoken);
}

} // namespace detail
} // namespace scorbit
