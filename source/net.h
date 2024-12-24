/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include "scorbit_sdk/net_types.h"
#include "scorbit_sdk/net_base.h"
#include "game_data.h"
#include "worker.h"
#include <cpr/cpr.h>
#include <string>
#include <functional>
#include <chrono>
#include <condition_variable>

namespace scorbit {
namespace detail {

std::string getSignature(const SignerCallback &signer, const std::string &uuid,
                         const std::string &timestamp);

enum class AuthStatus {
    NotAuthenticated,
    Authenticating,
    AuthenticatedCheckingPairing,
    AuthenticatedUnpaired,
    AuthenticatedPaired,
    AuthenticationFailed,
};

class Net : public NetBase
{
    using task_t = std::function<void()>;
    using deferred_get_setup_t = std::function<std::tuple<cpr::Url, cpr::Parameters>()>;
    using deferred_post_setup_t = std::function<std::tuple<cpr::Url, cpr::Payload>()>;

    struct GameSession {
        int sessionCounter {0};
        GameData gameData;
        std::chrono::time_point<std::chrono::steady_clock> startedTime {
                std::chrono::steady_clock::now()};
        GameHistory history;
    };

    struct VenueMachineInfo {
        int64_t venuemachineId {0};
        std::string opdbId;
    };

public:
    Net(SignerCallback signer, DeviceInfo deviceInfo);
    ~Net() override;

    std::string hostname() const;
    void setHostname(std::string hostname);
    bool isAuthenticated() const;

    void authenticate() override;
    void sendInstalled(const std::string &type, const std::string &version,
                       bool success = true) override;
    void sendGameData(const detail::GameData &data) override;
    void sendHeartbeat() override;
    void requestPairCode(StringCallback callback) override;

    std::string getMachineUuid() const override;
    std::string getPairDeeplink() const override;
    std::string getClaimDeeplink(int player) const override;

    const DeviceInfo &deviceInfo() const override;

    void requestTopScores(sb_score_t scoreFilter, StringCallback callback) override;

private:
    task_t createAuthenticateTask();
    task_t createInstalledTask(const std::string &type, const std::string &version, bool success);
    task_t createGameDataTask(const std::string &sessionUuid);
    task_t createHeartbeatTask();

    void startHeartbeatTimer();

    void postUploadHistoryTask(const GameHistory &history);
    task_t createUploadHistoryTask(const GameHistory &history);

    task_t createUploadTask(const std::string &endpoint, const std::string &name,
                            const cpr::Multipart &multipart);

    task_t createGetRequestTask(StringCallback replyCallback, deferred_get_setup_t deferredSetup);
    task_t createPostRequestTask(StringCallback replyCallback, deferred_post_setup_t deferredSetup);

    cpr::Header header() const;
    cpr::Header authHeader() const;

    cpr::Url url(std::string_view endpoint) const;

private:
    SignerCallback m_signer;

    std::atomic<AuthStatus> m_status {AuthStatus::NotAuthenticated};
    std::condition_variable m_authCV;
    std::mutex m_authMutex;
    std::mutex m_gameSessionsMutex;
    std::atomic_bool m_isGameDataInQueue {false};
    std::atomic_bool m_isHeartbeatInQueue {false};
    std::atomic_bool m_stopHeartbeatTimer {false};

    std::string m_hostname;
    std::string m_stoken;
    std::string m_cachedShortCode; // As short code for the pairing is permanent, we can cache it

    DeviceInfo m_deviceInfo;
    VenueMachineInfo m_vmInfo;
    std::map<std::string, GameSession> m_gameSessions; // key: session uuid
    Worker m_worker;
};

} // namespace detail
} // namespace scorbit
