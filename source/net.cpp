/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "net.h"
#include "net_util.h"
#include "logger.h"
#include "scorbit_sdk/net_types.h"
#include "utils/bytearray.h"
#include "utils/mac_address.h"
#include <fmt/format.h>
#include <openssl/sha.h>
#include <cpr/cpr.h>
#include <boost/uuid.hpp>
#include <boost/json.hpp>
#include <optional>

using namespace std;

namespace scorbit {
namespace detail {

constexpr auto PRODUCTION_LABEL = "production";
constexpr auto STAGING_LABEL = "staging";
constexpr auto PRODUCTION_HOSTNAME = "https://api.scorbit.io";
constexpr auto STAGING_HOSTNAME = "https://staging.scorbit.io";
constexpr auto API = "api";
constexpr auto STOKEN_URL = "stoken/";
constexpr auto INSTALLED_URL = "installed/";
constexpr auto ENTRY_URL = "entry/";
constexpr auto HEARTBEAT_URL {"heartbeat/"};
constexpr auto SESSION_CSV_URL {"session_log/"};
constexpr auto LOCAL_TOP_SCORES_URL {"venuemachine/{venuemachine_id}/top_scores/"};
constexpr auto REQUEST_PAIR_CODE_URL {"scorbitron_paired/{scorbitron_uuid}/"};
constexpr auto REQUEST_UNPAIR_URL {"scorbitron_unpair/"};

constexpr auto PLAYER_SCORE_HEAD {"current_p"};
constexpr auto PLAYER_SCORE_TAIL {"_score"};
constexpr auto REST_TOKEN {"SToken"};
constexpr auto RETURNED_TOKEN_NAME {"stoken"};

constexpr auto PAIRING_DEEPLINK {"https://scorbit.link/"
                                 "qrcode?$deeplink_path={manufacturer_prefix}"
                                 "&machineid={scorbit_machine_id}&uuid={scorbitron_uuid}"};
constexpr auto CLAIM_DEEPLINK {"https://scorbit.link/qrcode?$deeplink_path={venuemachine_id}"
                               "&opdb={opdb_id}&position={player_number}"};

using namespace std::chrono_literals;
constexpr auto NET_TIMEOUT = 14s;
constexpr auto HEARTBEAT_TIME = 10s;
constexpr auto NUM_RETRIES = 3;

string getSignature(const SignerCallback &signer, const std::string &uuid,
                    const std::string &timestamp)
{
    ByteArray message(removeSymbols(uuid, "-{}"));
    ByteArray timestampBytes(timestamp.cbegin(), timestamp.cend());
    message.insert(message.end(), timestampBytes.cbegin(), timestampBytes.cend());

    Digest digest;
    SHA256(message.data(), message.size(), digest.data());

    Signature signature;
    size_t signatureLen = 0;
    if (!signer(signature, signatureLen, digest)) {
        ERR("Can't sign message, signer callback returned error");
        return string {};
    }

    return ByteArray(signature.data(), signatureLen).hex();
}

// --------------------------------------------------------------------------------

Net::Net(SignerCallback signer, DeviceInfo deviceInfo)
    : m_signer(std::move(signer))
    , m_deviceInfo(std::move(deviceInfo))
{
    setHostname(m_deviceInfo.hostname);

    // Verify that mandatory "provider" field in deviceInfo is set
    if (m_deviceInfo.provider.empty()) {
        ERR("Provider is not set");
        // TODO: Set an appropriate error status
        return;
    }

    // Verify that mandatory "provider" field in deviceInfo is set for manufacturers (except
    // scorbitron, vscorbitron)
    if (m_deviceInfo.provider != "scorbitron" && m_deviceInfo.provider != "vscorbitron") {
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

    m_worker.start();
}

Net::~Net()
{
    m_stopHeartbeatTimer = true;
}

AuthStatus Net::status() const
{
    return m_status;
}

std::string Net::hostname() const
{
    return m_hostname;
}

void Net::setHostname(std::string hostname)
{
    if (hostname == PRODUCTION_LABEL || hostname.empty()) {
        hostname = PRODUCTION_HOSTNAME;
    } else if (hostname == STAGING_LABEL) {
        hostname = STAGING_HOSTNAME;
    }

    const auto host = exctractHostAndPort(hostname);
    m_hostname = fmt::format("{}://{}:{}", host.protocol, host.hostname, host.port);
}

bool Net::isAuthenticated() const
{
    return m_status == AuthStatus::AuthenticatedPaired
        || m_status == AuthStatus::AuthenticatedUnpaired;
}

void Net::authenticate()
{
    m_worker.postQueue(createAuthenticateTask());
}

void Net::sendInstalled(const std::string &type, const std::string &version, bool success)
{
    m_worker.postQueue(createInstalledTask(type, version, success));
}

void Net::sendGameData(const detail::GameData &data)
{
    {
        std::lock_guard lock(m_gameSessionsMutex);
        auto &session = m_gameSessions[data.sessionUuid];
        session.gameData = data;
        session.history.push_back(data);
    }

    // Ensure that only single task in the queue (while another can be running).
    // However, if game session is finished (not active), post task anyway, because this is the last
    // task for that game session.
    if (!data.isGameActive || !m_isGameDataInQueue.exchange(true)) {
        m_worker.postGameDataQueue(createGameDataTask(data.sessionUuid));
    }
}

void Net::sendHeartbeat()
{
    // Ensure that only single task in the queue (while another can be running)
    if (!m_isHeartbeatInQueue.exchange(true)) {
        m_worker.postHeartbeatQueue(createHeartbeatTask());
    }
}

void Net::requestPairCode(StringCallback callback)
{
    // Return cached short code if available
    if (!m_cachedShortCode.empty()) {
        callback(Error::Success, m_cachedShortCode);
        return;
    }

    m_worker.postQueue(createPostRequestTask(
            [this, callback = std::move(callback)](Error error, std::string reply) {
                // Parse short code and return it in callback
                if (error == Error::Success) {
                    boost::system::error_code ec;
                    boost::json::object json = boost::json::parse(reply, ec).as_object();
                    if (!ec && json.contains("shortcode")) {
                        reply = json.at("shortcode").as_string();
                        m_cachedShortCode = reply;
                    } else {
                        WRN("API can't parse short code: {}", reply);
                        error = Error::ApiError;
                    }
                }
                callback(error, reply);
            },
            [this]() {
                // Deferred setup
                const auto endpoint = url(fmt::format(
                        REQUEST_PAIR_CODE_URL, fmt::arg("scorbitron_uuid", m_deviceInfo.uuid)));

                return make_tuple(endpoint, cpr::Payload {});
            },
            {AuthStatus::AuthenticatedUnpaired, AuthStatus::AuthenticatedPaired}));
}

string Net::getMachineUuid() const
{
    return m_deviceInfo.uuid;
}

string Net::getPairDeeplink() const
{
    return fmt::format(PAIRING_DEEPLINK, fmt::arg("manufacturer_prefix", m_deviceInfo.provider),
                       fmt::arg("scorbit_machine_id", m_deviceInfo.machineId),
                       fmt::arg("scorbitron_uuid", m_deviceInfo.uuid));
}

string Net::getClaimDeeplink(int player) const
{
    if (m_vmInfo.venuemachineId == 0 || m_vmInfo.opdbId.empty()) {
        DBG("Venue machine ID or OPDB ID is not set, make sure that the device is authenticated "
            "and paired");
        return {};
    }

    return fmt::format(CLAIM_DEEPLINK, fmt::arg("venuemachine_id", m_vmInfo.venuemachineId),
                       fmt::arg("opdb_id", m_vmInfo.opdbId), fmt::arg("player_number", player));
}

const DeviceInfo &Net::deviceInfo() const
{
    return m_deviceInfo;
}

void Net::requestTopScores(sb_score_t scoreFilter, StringCallback callback)
{
    m_worker.postQueue(createGetRequestTask(std::move(callback), [this, scoreFilter]() {
        const auto endpoint = url(fmt::format(
                LOCAL_TOP_SCORES_URL, fmt::arg("venuemachine_id", m_vmInfo.venuemachineId)));
        cpr::Parameters parameters;
        if (scoreFilter > 0) {
            parameters.Add({"score", fmt::format("{}", scoreFilter)});
        }

        return make_tuple(endpoint, parameters);
    }));
}

void Net::requestUnpair(StringCallback callback)
{
    m_worker.postQueue(createPostRequestTask(
            [this, callback = std::move(callback)](Error error, std::string reply) {
                if (error == Error::Success || error == Error::NotPaired) {
                    m_status = AuthStatus::AuthenticatedUnpaired;
                    error = Error::Success;
                }
                callback(error, reply);
            },
            [this]() {
                const auto endpoint = url(fmt::format(
                        REQUEST_UNPAIR_URL, fmt::arg("scorbitron_uuid", m_deviceInfo.uuid)));
                cpr::Payload payload = {{"unpaired", "true"}};

                return make_tuple(endpoint, payload);
            }));
}

Net::task_t Net::createAuthenticateTask()
{
    return [this]() {
        std::lock_guard lock(m_authMutex);

        if (m_status != AuthStatus::NotAuthenticated) {
            return;
        }

        m_stoken.clear();
        m_status = AuthStatus::Authenticating;
        const auto timestamp = std::to_string(std::chrono::seconds(std::time(nullptr)).count());
        ByteArray message(m_deviceInfo.uuid);

        const auto signature = getSignature(m_signer, m_deviceInfo.uuid, timestamp);
        if (signature.empty()) {
            ERR("Can't authenticate, signature is empty");
            return;
        }

        auto payload = cpr::Payload {
                {"provider", m_deviceInfo.provider},
                {"uuid", m_deviceInfo.uuid},
                {"timestamp", timestamp},
                {"sign", signature},
        };

        auto r = cpr::Post(url(STOKEN_URL), payload);

        if (r.status_code != 200) {
            m_status = AuthStatus::AuthenticationFailed;
            const auto msg = fmt::format("API authentication failed: code {}, {}", r.status_code,
                                         r.error.message);
            ERR("{}", msg);
            ERR("{}", r.text);
            // TODO: Sentry
            // SentryManager::message(msg);
            return;
        }

        boost::system::error_code ec;
        boost::json::object json = boost::json::parse(r.text, ec).as_object();

        if (ec || !json.contains(RETURNED_TOKEN_NAME)) {
            const auto msg = fmt::format("No token, authentication failed, got reply: {}", r.text);
            ERR(msg);
            // SentryManager::message(msg);
            // TODO: retry later with exponential timing
            return;
        }

        m_stoken = json.at(RETURNED_TOKEN_NAME).as_string();

        m_status = AuthStatus::AuthenticatedCheckingPairing;
        INF("API authentication successful! Checking pairing status...");

        m_authCV.notify_all();

        sendHeartbeat(); // To check if it's paired
        startHeartbeatTimer();
    };
}

Net::task_t Net::createInstalledTask(const std::string &type, const std::string &version,
                                     bool success)
{
    return [this, type, version, success]() {
        for (int i = 0; i < NUM_RETRIES; ++i) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed;
            });

            if (m_status == AuthStatus::AuthenticationFailed) {
                DBG("Can't send installed task, authentication failed!");
                break;
            }

            auto payload = cpr::Payload {{"installed", success ? "true" : "false"}};
            payload.Add({"type", type});
            payload.Add({"version", version});

            INF("API send installed: type={}, version={}, installed={}", type, version, success);

            // TODO: Sentry

            const auto r = cpr::Post(url(INSTALLED_URL), payload, authHeader(),
                                     cpr::Timeout {NET_TIMEOUT});

            if (r.status_code == 200) {
                INF("API installed response: {}", r.text);
                break;
            }

            ERR("API installed failed: code={}, {}", r.status_code, r.error.message);
            ERR("{}", r.text);

            if (r.status_code != 401) {
                break;
            }

            m_status = AuthStatus::NotAuthenticated;
            auto auth = createAuthenticateTask();
            auth();
        }
    };
}

Net::task_t Net::createGameDataTask(const std::string &sessionUuid)
{
    return [this, sessionUuid]() {
        int sessionCounter;
        {
            std::lock_guard lock(m_gameSessionsMutex);
            if (m_gameSessions.count(sessionUuid) == 0) {
                // Nothing to do, session is already finished and removed
                return;
            }

            sessionCounter = ++m_gameSessions[sessionUuid].sessionCounter;
        }

        std::optional<GameSession> session;

        for (int i = 0; i < NUM_RETRIES; ++i) {
            // DBG("Before waiting game data");

            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed;
            });

            if (m_status != AuthStatus::AuthenticatedPaired) {
                if (m_status == AuthStatus::AuthenticationFailed) {
                    DBG("Can't send game data, authentication failed!");
                }
                break;
            }

            {
                std::lock_guard lock(m_gameSessionsMutex);
                session = m_gameSessions[sessionUuid];
            }

            int64_t elapsedMilliseconds =
                    chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now()
                                                                - session->startedTime)
                            .count();
            auto &data = session->gameData;
            cpr::Payload payload {
                    {"session_uuid", data.sessionUuid},
                    {"session_time", std::to_string(elapsedMilliseconds)},
                    {"active", data.isGameActive ? "true" : "false"},
                    {"session_sequence", to_string(sessionCounter)},
            };

            if (data.ball > 0) {
                payload.Add({"current_ball", std::to_string(data.ball)});
            }

            if (data.activePlayer > 0) {
                payload.Add({"current_player", std::to_string(data.activePlayer)});
            }

            const auto gameModes = data.modes.str();
            if (!gameModes.empty()) {
                payload.Add({"game_modes", gameModes});
            }

            // Iterate over all existing scores and build payload with scores
            std::string scoresStr;
            for (const auto &player : data.players) {
                payload.Add({fmt::format("{}{}{}", PLAYER_SCORE_HEAD, player.second.player(),
                                         PLAYER_SCORE_TAIL),
                             std::to_string(player.second.score())});
                scoresStr +=
                        fmt::format("player{}={}, ", player.second.player(), player.second.score());
            }

            INF("API sending game data for session {}, counter: {}, active player: {}, current "
                "ball: {}, scores: {}modes: [{}]",
                sessionUuid, sessionCounter, data.activePlayer, data.ball, scoresStr, gameModes);

            // Reset the flag only single time. From this point there maybe another score change
            // added and flag will be again true. So, we don't want to reset the flag another time
            // while another score change already is in the queue.
            if (i == 0) {
                m_isGameDataInQueue = false;
            }

            // TODO: sentry

            const auto r =
                    cpr::Post(url(ENTRY_URL), payload, authHeader(), cpr::Timeout {NET_TIMEOUT});

            if (r.status_code == 200) {
                INF("API send game data: ok, counter: {}, {}", sessionCounter, r.text);
                break;
            }

            ERR("API send game data failed: code={}, {}", r.status_code, r.error.message);
            ERR("{}", r.text);

            if (r.status_code == 500 && r.text.find("not attached") != string::npos) {
                m_status = AuthStatus::AuthenticatedUnpaired;
                break;
            }

            if (r.status_code != 401) {
                break;
            }

            m_status = AuthStatus::NotAuthenticated;
            auto auth = createAuthenticateTask();
            auth();
        }

        // Clear the session if the game is finished
        if (session && !session->gameData.isGameActive) {
            std::optional<GameHistory> finishedSessionHistory;

            {
                std::lock_guard lock(m_gameSessionsMutex);
                if (m_gameSessions.count(sessionUuid) > 0) {
                    finishedSessionHistory = std::move(m_gameSessions[sessionUuid].history);
                    m_gameSessions.erase(sessionUuid);
                    INF("Game session {} finished", sessionUuid);
                }
            }

            if (finishedSessionHistory) {
                postUploadHistoryTask(*finishedSessionHistory);
            }
        }

        // DBG("On quit game data");
    };
}

Net::task_t Net::createHeartbeatTask()
{
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
                    return false;
                }
            });

            if (m_status == AuthStatus::AuthenticationFailed) {
                break;
            }

            bool isActiveSession;
            {
                std::lock_guard lock(m_gameSessionsMutex);
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

                boost::system::error_code ec;
                boost::json::object json = boost::json::parse(r.text, ec).as_object();
                if (!ec) {
                    if (m_status == AuthStatus::AuthenticatedCheckingPairing) {
                        if (json.contains("unpaired") && json.at("unpaired").as_bool()) {
                            m_status = AuthStatus::AuthenticatedUnpaired;
                            m_vmInfo.venuemachineId = 0;
                            m_vmInfo.opdbId.clear();
                        } else {
                            m_status = AuthStatus::AuthenticatedPaired;
                        }

                        m_authCV.notify_all();

                        if (const auto venuemachineId = json.if_contains("venuemachine_id")) {
                            m_vmInfo.venuemachineId = venuemachineId->as_int64();
                        }

                        if (const auto config = json.if_contains("config")) {
                            if (const auto opdbId = config->as_object().if_contains("opdb_id")) {
                                m_vmInfo.opdbId = opdbId->as_string();
                            }
                        }
                    }
                }
                break;
            }

            ERR("API send game data failed: code={}, {}", r.status_code, r.error.message);
            ERR("{}", r.text);

            if (r.status_code != 401) {
                break;
            }

            m_status = AuthStatus::NotAuthenticated;
            auto auth = createAuthenticateTask();
            auth();
        }

        m_isHeartbeatInQueue = false;
        // DBG("On quit heartbeat");
    };
}

void Net::startHeartbeatTimer()
{
    if (!m_stopHeartbeatTimer) {
        m_worker.runTimer(HEARTBEAT_TIME, [this] {
            startHeartbeatTimer();
            sendHeartbeat();
        });
    }
}

void Net::postUploadHistoryTask(const GameHistory &history)
{
    m_worker.postQueue(createUploadHistoryTask(history));
}

Net::task_t Net::createUploadHistoryTask(const GameHistory &history)
{
    const auto sessionUuid = history.front().sessionUuid;
    const auto filename = fmt::format("{}.csv", sessionUuid.get());
    const auto csv = gameHistoryToCsv(history);
    const auto multipart = cpr::Multipart {
            {"uuid", sessionUuid},
            {"log_file", cpr::Buffer(csv.cbegin(), csv.cend(), filename)},
    };

    return createUploadTask(SESSION_CSV_URL, filename, multipart);
}

Net::task_t Net::createUploadTask(const std::string &endpoint, const std::string &filename,
                                  const cpr::Multipart &multipart)
{
    return [this, endpoint, filename, multipart]() {
        for (int i = 0; i < NUM_RETRIES; ++i) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed;
            });

            if (m_status != AuthStatus::AuthenticatedPaired) {
                DBG("API can't upload {}, device is not paired or authentication failed!",
                    filename);
                break;
            }

            INF("API upload to backend started: {} to {}", filename, endpoint);
            auto r = cpr::Post(url(endpoint), multipart, authHeader());

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

Net::task_t Net::createGetRequestTask(StringCallback replyCallback,
                                      deferred_get_setup_t deferredSetup,
                                      std::vector<AuthStatus> allowedStatuses)
{
    return [this, callback = std::move(replyCallback), deferredSetup = std::move(deferredSetup),
            allowedStatuses = std::move(allowedStatuses)]() {
        Error error {Error::ApiError};
        std::string reply;

        for (int i = 0; i < NUM_RETRIES; ++i) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed;
            });

            auto [url, parameters] = deferredSetup();

            if (!checkAllowedStatuses(allowedStatuses)) {
                if (m_status == AuthStatus::AuthenticationFailed) {
                    DBG("Can't send GET request to {}, authentication failed!", url.str());
                    error = Error::AuthFailed;
                } else {
                    DBG("Can't send GET request to {}, not paired!", url.str());
                    error = Error::NotPaired;
                }
                break;
            }

            INF("API GET request: {}", url.str());

            auto r = cpr::Get(url, parameters, authHeader(), cpr::Timeout {NET_TIMEOUT});
            reply = std::move(r.text);

            if (r.status_code == 200) {
                INF("API GET request: ok, {}", reply);
                error = Error::Success;
                break;
            }

            ERR("API GET request failed: code={}, {}, reply: {}", r.status_code, r.error.message,
                reply);

            if (r.status_code != 401) {
                break;
            }

            m_status = AuthStatus::NotAuthenticated;
            auto auth = createAuthenticateTask();
            auth();
        }

        if (callback) {
            callback(error, std::move(reply));
        }
    };
}

Net::task_t Net::createPostRequestTask(StringCallback replyCallback,
                                       deferred_post_setup_t deferredSetup,
                                       std::vector<AuthStatus> allowedStatuses)
{
    return [this, callback = std::move(replyCallback), deferredSetup = std::move(deferredSetup),
            allowedStatuses = std::move(allowedStatuses)]() {
        Error error {Error::ApiError};
        std::string reply;

        for (int i = 0; i < NUM_RETRIES; ++i) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return isAuthenticated() || m_status == AuthStatus::AuthenticationFailed;
            });

            auto [url, payload] = deferredSetup();

            if (!checkAllowedStatuses(allowedStatuses)) {
                if (m_status == AuthStatus::AuthenticationFailed) {
                    DBG("Can't send POST request to {}, authentication failed!", url.str());
                    error = Error::AuthFailed;
                } else {
                    DBG("Can't send POST request to {}, not paired!", url.str());
                    error = Error::NotPaired;
                }
                break;
            }

            INF("API POST request: {}", url.str());

            auto r = cpr::Post(url, payload, authHeader(), cpr::Timeout {NET_TIMEOUT});
            reply = std::move(r.text);

            if (r.status_code == 200) {
                INF("API POST request: ok, {}", reply);
                error = Error::Success;
                break;
            }

            ERR("API POST request failed: code={}, {}, reply: {}", r.status_code, r.error.message,
                reply);

            if (r.status_code != 401) {
                break;
            }

            m_status = AuthStatus::NotAuthenticated;
            auto auth = createAuthenticateTask();
            auth();
        }

        if (callback) {
            callback(error, std::move(reply));
        }
    };
}

cpr::Header Net::header() const
{
    return cpr::Header {{"Cache-Control", "no-cache"}};
}

cpr::Header Net::authHeader() const
{
    auto h = header();
    h["Authorization"] = fmt::format("{} {}", REST_TOKEN, m_stoken);
    return h;
}

cpr::Url Net::url(std::string_view endpoint) const
{
    return cpr::Url {fmt::format("{}/{}/{}", m_hostname, API, endpoint)};
}

bool Net::checkAllowedStatuses(const std::vector<AuthStatus> &allowedStatuses) const
{
    return std::any_of(begin(allowedStatuses), end(allowedStatuses),
                       [this](AuthStatus status) { return status == m_status; });
}

} // namespace detail
} // namespace scorbit
