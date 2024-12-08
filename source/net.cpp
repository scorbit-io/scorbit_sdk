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

using namespace std;

namespace scorbit {
namespace detail {

constexpr auto PRODUCTION_LABEL = "production";
constexpr auto STAGING_LABEL = "staging";
constexpr auto PRODUCTION_HOSTNAME = "https://api.scorbit.io";
constexpr auto STAGING_HOSTNAME = "https://staging.scorbit.io";
constexpr auto API = "/api/";
constexpr auto STOKEN_URL = "stoken/";
constexpr auto INSTALLED_URL = "installed/";
constexpr auto ENTRY_URL = "entry/";
constexpr auto HEARTBEAT_URL {"heartbeat/"};
constexpr auto PLAYER_SCORE_HEAD {"current_p"};
constexpr auto PLAYER_SCORE_TAIL {"_score"};
constexpr auto REST_TOKEN {"SToken"};
constexpr auto RETURNED_TOKEN_NAME {"stoken"};

using namespace std::chrono_literals;
constexpr auto NET_TIMEOUT = 14s;

string getSignature(const SignerCallback &signer, const std::string &uuid,
                    const std::string &timestamp)
{
    ByteArray message(uuid);
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

    // Parse UUID to if it's in correct format
    if (!m_deviceInfo.uuid.empty()) {
        const auto originalUuid = m_deviceInfo.uuid;
        m_deviceInfo.uuid = parseUuid(originalUuid);
        if (m_deviceInfo.uuid.empty()) {
            ERR("Given UUID is not in correct format: {}", originalUuid);
            // TODO: set error status
            return;
        }
    }

    if (m_deviceInfo.uuid.empty()) {
        const auto macAddress = getMacAddress();
        m_deviceInfo.uuid = deriveUuid(macAddress);
        INF("Derived UUID: {} from mac address: {}", m_deviceInfo.uuid, macAddress);
    }

    // Cleanup UUID
    m_deviceInfo.uuid = removeSymbols(m_deviceInfo.uuid, "-{}");

    m_worker.start();
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
    return m_status == AuthStatus::Authenticated;
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
        m_gameSessions[data.sessionUuid].gameData = data;
    }

    m_worker.post(createGameDataTask(data.sessionUuid));
}

void Net::sendHeartbeat(bool isActive)
{
    (void)isActive; // TODO
    m_worker.postQueue(createHeartbeatTask());
}

Net::task_t Net::createAuthenticateTask()
{
    return [this] {
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

        auto r = cpr::Post(cpr::Url {m_hostname + API + STOKEN_URL}, payload);

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

        if (!json.contains(RETURNED_TOKEN_NAME)) {
            const auto msg = fmt::format("No token, authentication failed, got reply: {}", r.text);
            ERR(msg);
            // SentryManager::message(msg);
            // TODO: retry later
            return;
        }

        m_stoken = json.at(RETURNED_TOKEN_NAME).as_string();

        m_status = AuthStatus::Authenticated;
        INF("API authentication successful!");

        m_authCV.notify_all();

        m_worker.postQueue(createHeartbeatTask());
    };
}

Net::task_t Net::createInstalledTask(const std::string &type, const std::string &version,
                                     bool success)
{
    return [this, type, version, success] {
        for (;;) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return m_status == AuthStatus::Authenticated
                    || m_status == AuthStatus::AuthenticationFailed;
            });

            if (m_status != AuthStatus::Authenticated) {
                DBG("Can't send installed task, authentication failed!");
                return;
            }

            auto payload = cpr::Payload {{"installed", success ? "true" : "false"}};
            payload.Add({"type", type});
            payload.Add({"version", version});

            INF("API send installed: type={}, version={}, installed={}", type, version, success);

            // TODO: Sentry
            // auto transaction = SentryManager::startTransaction(INSTALLED_URL, "POST");
            // m_session.SetHeader(header(m_authHeader));
            const auto r = cpr::Post(cpr::Url {m_hostname + API + INSTALLED_URL}, payload,
                                     authHeader(), cpr::Timeout {NET_TIMEOUT});
            // if (transaction) {
            //     transaction->setStatus(r.status_code);
            //     transaction->finish();
            // }

            if (r.status_code == 200) {
                INF("API installed response: {}", r.text);
                return;
            }

            ERR("API installed failed: code={}, {}", r.status_code, r.error.message);
            ERR("{}", r.text);

            if (r.status_code == 401) {
                m_status = AuthStatus::NotAuthenticated;
                auto auth = createAuthenticateTask();
                auth();
            }
        }
    };
}

Net::task_t Net::createGameDataTask(const std::string &sessionUuid)
{
    return [this, sessionUuid]() {
        INF("API sending game data for session {}", sessionUuid);

        int sessionCounter;
        {
            std::lock_guard lock(m_gameSessionsMutex);
            sessionCounter = ++m_gameSessions[sessionUuid].sessionCounter;
        }

        GameSession session;
        {
            std::lock_guard lock(m_gameSessionsMutex);
            session = m_gameSessions[sessionUuid];
        }

        int64_t elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              chrono::steady_clock::now() - session.startedTime)
                                              .count();
        auto &data = session.gameData;
        cpr::Payload payload {
                {"session_uuid", data.sessionUuid},
                {"session_time", std::to_string(elapsedMilliseconds)},
                {"active", data.isGameStarted ? "true" : "false"},
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
        for (const auto &player : data.players) {
            payload.Add({fmt::format("{}{}{}", PLAYER_SCORE_HEAD, player.second.player(),
                                     PLAYER_SCORE_TAIL),
                         std::to_string(player.second.score())});
        }

        // TODO: sentry
        const auto r = cpr::Post(cpr::Url {m_hostname + API + ENTRY_URL}, payload, authHeader(),
                                 cpr::Timeout {NET_TIMEOUT});

        if (r.status_code == 200) {
            INF("API send game data: ok");
            DBG("{}", r.text);
            return;
        }

        ERR("API send game data failed: code={}, {}", r.status_code, r.error.message);
        ERR("{}", r.text);

        if (r.status_code == 401) {
            m_status = AuthStatus::NotAuthenticated;
            auto auth = createAuthenticateTask();
            auth();
        }
    };
}

Net::task_t Net::createHeartbeatTask()
{
    return [this] {
        for (;;) {
            std::unique_lock lock(m_authMutex);
            m_authCV.wait(lock, [this] {
                return m_status == AuthStatus::Authenticated
                    || m_status == AuthStatus::AuthenticationFailed;
            });

            if (m_status != AuthStatus::Authenticated) {
                return;
            }

            bool isActiveSession = false; // FIXME
            const auto parameters =
                    cpr::Parameters {{"session_active", isActiveSession ? "true" : "false"}};

            // TODO: sentry

            const auto r = cpr::Get(cpr::Url {m_hostname + API + HEARTBEAT_URL}, parameters,
                                    authHeader(), cpr::Timeout {NET_TIMEOUT});

            if (r.status_code == 200) {
                INF("API heartbeat: ok");
                DBG("{}", r.text);
                return;
            }

            ERR("API send game data failed: code={}, {}", r.status_code, r.error.message);
            ERR("{}", r.text);

            if (r.status_code == 401) {
                m_status = AuthStatus::NotAuthenticated;
                auto auth = createAuthenticateTask();
                auth();
            }
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

} // namespace detail
} // namespace scorbit
