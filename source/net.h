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

    struct GameSession {
        int sessionCounter {0};
        GameData gameData;
        std::chrono::time_point<std::chrono::steady_clock> startedTime {
                std::chrono::steady_clock::now()};
    };

    struct VenueMachineInfo {
        int64_t venuemachineId {0};
        std::string opdbId;
    };

public:
    Net(SignerCallback signer, DeviceInfo deviceInfo);

    std::string hostname() const;
    void setHostname(std::string hostname);
    bool isAuthenticated() const;

    void authenticate() override;
    void sendInstalled(const std::string &type, const std::string &version,
                       bool success = true) override;
    void sendGameData(const detail::GameData &data) override;
    void sendHeartbeat() override;

    std::string getMachineUuid() const override;
    std::string getPairDeeplink() const override;
    std::string getClaimDeeplink(int player) const override;

private:
    task_t createAuthenticateTask();
    task_t createInstalledTask(const std::string &type, const std::string &version, bool success);
    task_t createGameDataTask(const std::string &sessionUuid);
    task_t createHeartbeatTask();

    cpr::Header header() const;
    cpr::Header authHeader() const;

private:
    SignerCallback m_signer;

    std::atomic<AuthStatus> m_status {AuthStatus::NotAuthenticated};
    std::condition_variable m_authCV;
    std::mutex m_authMutex;
    std::mutex m_gameSessionsMutex;
    std::atomic_bool m_isGameDataInQueue {false};
    std::atomic_bool m_isHeartbeatInQueue {false};

    std::string m_hostname;
    std::string m_stoken;

    DeviceInfo m_deviceInfo;
    VenueMachineInfo m_vmInfo;
    std::map<std::string, GameSession> m_gameSessions;
    Worker m_worker;
};

} // namespace detail
} // namespace scorbit
