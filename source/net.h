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

#include <scorbit_sdk/net_types.h>
#include "net_base.h"
#include "key_resolver.h"
#include "game_data.h"
#include "worker.h"
#include "updater.h"
#include "identifiers.h"
#include "event_manager.h"
#include "utils/machine_fingerprint.h"
#include <centrifugo.h>
#include <fmt/format.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <shared_mutex>
#include <unordered_map>
#include <optional>

namespace scorbit {
namespace detail {

std::string getSignature(const SignerCallback &signer, const std::string &uuid,
                         const std::string &timestamp);

class SafeMultipart;

class Net : public NetBase
{
    using deferred_get_setup_t = std::function<std::tuple<cpr::Url, cpr::Parameters>()>;
    using deferred_post_setup_t = std::function<std::tuple<cpr::Url, cpr::Body>()>;
    using deferred_post_multipart_setup_t = std::function<std::tuple<cpr::Url, SafeMultipart>()>;
    using deferred_patch_setup_t = std::function<std::tuple<cpr::Url, cpr::Body>()>;
    using deferred_patch_multipart_setup_t = std::function<std::tuple<cpr::Url, SafeMultipart>()>;

    struct ScoreMetadata {
        uint64_t id {0}; // score id
        bool isNfcVerified {false};
        std::optional<std::string> tournamentUuid;
    };

    struct GameSession {
        int sessionCounter {0};
        std::string sessionUuid;
        GameData gameData;
        std::chrono::time_point<std::chrono::steady_clock> startedTime {
                std::chrono::steady_clock::now()};
        std::chrono::time_point<std::chrono::system_clock> startedSystemTime {
                std::chrono::system_clock::now()};
        GameHistory history;
        std::unordered_map<sb_player_t, ScoreMetadata> scoresMetadata;
    };

    struct MachineInfo {
        std::string opdbId;
        std::string machineUuid;
        std::optional<std::string> variantUuid;
        std::optional<std::string> venueUuid;
        std::string title;
    };

public:
    Net(DeviceInfo deviceInfo, std::vector<std::unique_ptr<IKeyResolver>> resolvers);
    ~Net() override;

    AuthStatus status() const override;

    const std::string &hostname() const;
    const std::string &cfHostname() const; // Centrifugo hostname
    void setHostname(std::string hostname, std::string cfHostname = std::string {});
    bool isAuthenticated() const;

    void authenticate() override;
    void updateConfig(const std::string &type, const std::string &version, bool installed,
                      std::optional<std::string> log = std::nullopt) override;
    void sessionCreate(const detail::GameData &data, GameStartOrigin origin,
                       std::function<void()> onCreated) override;
    void submitGameData(const detail::GameData &data, SessionFlags flags) override;
    void sendHeartbeat() override;
    void getConfig() override;
    void requestPairCode(StringCallback callback) override;

    const std::string &getMachineUuid() const override;
    std::uint64_t getMachineSerial() const override;
    const std::string &getPairDeeplink() const override;

    const DeviceInfo &deviceInfo() const override;

    void requestTopScores(sb_score_t scoreFilter, StringCallback callback) override;
    void requestUnpair(StringCallback callback) override;

    void download(bool isAsync, StringCallback callback, const std::string &url,
                  const std::string &filename, const HttpHeaders &headers) override;
    void downloadBuffer(bool isAsync, VectorCallback callback, const std::string &url,
                        size_t reserveBufferSize, const HttpHeaders &headers) override;

    PlayerProfilesManager &playersManager() override;

    void patchScorbitron(std::string body, StringCallback callback,
                         std::vector<AuthStatus> allowedStatuses = {
                                 AuthStatus::AuthenticatedPaired}) override;

    std::string consumeNonce() override;
    void setProbesManager(std::shared_ptr<nfc::ProbesManager> manager) override;

    void requestPairMachine(const std::string &machineUuid, const std::string &ownerUuid,
                            StringCallback callback) override;

    void setCapabilities(Capabilities capabilities) override;

    void setCreditsDropped(int credits, const std::string &transaction, bool success) override;
    void setCreditsStatus(bool freePlay, int credits, int maxCredits, const char *pricing) override;

    void scheduleDelayedOnWorker(std::chrono::steady_clock::duration delay,
                                 std::function<void()> fn) override;
    void cancelModeExpiryTimer() override;

    void uploadDiagnostics(std::vector<std::string> logPaths,
                           std::vector<std::string> recordingPaths, std::string logString) override;

private:
    task_t createAuthenticateTask();
    task_t updateConfigTask(const std::string &type, const std::string &version, bool installed,
                            std::optional<std::string> log);
    task_t createSessionCreateTask(int sessionId, GameStartOrigin origin,
                                   std::function<void()> onCreated);
    task_t createSessionUpdateTask(int sessionId, SessionFlags flags);
    task_t createHeartbeatTask();

    void sessionUpdate(int sessionId, SessionFlags flags);

    void startHeartbeatTimer();
    void stopHeartbeatTimer();
    void startTokenRefreshTimer();
    void stopTokenRefreshTimer();

    void sendLatestGameData(int sessionId);

    void initializeConnectionState();
    void initScorbitronObject();
    void sendScorbitronObject();

    void requestReleaseTrackInfo();
    void requestMachineObject();

    void requestSessionData(const std::string &sessionUuid);

    void postUploadHistoryTask(const GameHistory &history, const std::string &sessionUuid);
    task_t createUploadHistoryTask(const GameHistory &history, const std::string &sessionUuid);

    task_t createUploadTask(const std::string &endpoint, const std::string &name,
                            SafeMultipart &&multipart);

    void parseScorbitronObject(Error error, const std::string &reply);

    // Generic HTTP request task creator
    template<typename DeferredSetupT, typename HttpMethodT>
    task_t createHttpRequestTask(
            const char *requestType, StringCallback replyCallback, DeferredSetupT deferredSetup,
            HttpMethodT httpMethod,
            std::vector<AuthStatus> allowedStatuses = {AuthStatus::AuthenticatedPaired},
            bool includeFingerprintHash = false, bool resilientTransferTimeouts = false);

    // Specialized methods for different HTTP methods
    task_t createGetRequestTask(StringCallback replyCallback, deferred_get_setup_t deferredSetup,
                                std::vector<AuthStatus> allowedStatuses = {
                                        AuthStatus::AuthenticatedPaired});
    task_t createPostRequestTask(
            StringCallback replyCallback, deferred_post_setup_t deferredSetup,
            std::vector<AuthStatus> allowedStatuses = {AuthStatus::AuthenticatedPaired},
            bool includeFingerprintHash = false);
    task_t createPostMultipartRequestTask(StringCallback replyCallback,
                                          deferred_post_multipart_setup_t deferredSetup,
                                          std::vector<AuthStatus> allowedStatuses = {
                                                  AuthStatus::AuthenticatedPaired});
    task_t createPatchRequestTask(StringCallback replyCallback,
                                  deferred_patch_setup_t deferredSetup,
                                  std::vector<AuthStatus> allowedStatuses = {
                                          AuthStatus::AuthenticatedPaired});
    task_t createPatchMultipartRequestTask(
            StringCallback replyCallback, deferred_patch_multipart_setup_t deferredSetup,
            std::vector<AuthStatus> allowedStatuses = {AuthStatus::AuthenticatedPaired},
            bool includeFingerprintHash = false);
    task_t createDownloadFileTask(StringCallback replyCallback, std::string url,
                                  std::string filename, HttpHeaders extraHeaders);
    task_t createDownloadBufferTask(VectorCallback replyCallback, std::string url,
                                    size_t reserveBufferSize, HttpHeaders extraHeaders);

    cpr::Header header() const;
    cpr::Header authHeader() const;
    cpr::SslOptions sslOptions() const;

    bool checkAllowedStatuses(const std::vector<AuthStatus> &allowedStatuses) const;
    void processScoresAndPlayersProfiles(const nlohmann::json &val, GameSession &gameSession);

    bool isActiveCentrifugoClient(const centrifugo::Client *client) const;
    void pruneRetiredCentrifugoClients();
    void retireCentrifugoClient();
    void centrifugoSetup(bool fetchFreshToken = false);
    void centrifugoConnect();
    void setupAndConnectCentrifugo(bool fetchFreshToken = false);
    void restartCentrifugo();

    void clearPairedMachineContext();
    void emitPairingStatusEventIfChanged(bool isPaired);
    void onPaired();
    void onUnpaired();

    std::optional<std::chrono::seconds> getTimeUntilTokenExpiration() const;

    void createNfcNonces();
    void startNfcCheckTimer();
    void setNfcTag();
    void checkNfcBootReason();

    void requestCreditsStatusEvent();
    /// Request credits status from the app only when Centrifugo is connected and
    /// `m_machineChannel` is set (must match a server-side subscription from the JWT).
    void requestCreditsStatusIfReady();

    void requestFirmwaresList();

    void checkSystemTimeAccuracy(int64_t timestamp) const;
    void updateDiscoveryDescription();

    // Make url() a variadic template that forwards all args to fmt::format
    template<typename... Args>
    cpr::Url url(std::string_view endpoint, Args &&...args) const
    {
        const auto formattedEndpoint = fmt::format(
                fmt::runtime(endpoint), fmt::arg(ARG_SCORBITRON_UUID, m_deviceInfo.uuid),
                fmt::arg(ARG_MACHINE_UUID, m_machineInfo.machineUuid),
                std::forward<Args>(args)...); // Pass extra args

        if (formattedEndpoint.starts_with("http://") || formattedEndpoint.starts_with("https://")) {
            return cpr::Url {formattedEndpoint};
        }

        const auto myurl = fmt::format("{}/{}/{}", m_hostname, URL_API, formattedEndpoint);
        return cpr::Url {myurl};
    }

private:
    bool validateDeviceInfo() const;
    bool resolveKeys(const std::string &serverTimestamp);
    bool reprovisionSoftKey(const std::string &serverTimestamp);

    SignerCallback m_signer;
    std::vector<std::unique_ptr<IKeyResolver>> m_keyResolvers;

    std::atomic<AuthStatus> m_status {AuthStatus::NotAuthenticated};
    std::condition_variable m_authCV;
    std::condition_variable m_shortCodeCV;
    mutable std::mutex m_authMutex;
    std::mutex m_gameSessionsMutex;
    std::mutex m_shortCodeMutex;
    std::mutex m_nfcMutex;
    mutable std::shared_mutex m_tokenMutex;
    std::atomic_bool m_isGameDataInQueue {false};
    std::atomic_bool m_isHeartbeatInQueue {false};
    std::atomic_bool m_stop {false};
    std::atomic_bool m_isRefreshingToken {false};

    std::string m_hostname;
    std::string m_cfHostname;
    std::string m_stoken;
    std::chrono::system_clock::time_point m_tokenExpiration;
    std::string m_cachedShortCode; // As short code for the pairing is permanent, we can cache it
    mutable std::string m_cachedPairDeeplink;

    std::string m_machineChannel;
    std::string m_releaseTrackUrl;

    std::string m_lastNfcBootReason;
    MachineFingerprint m_fingerprint;
    std::string m_fingerprintHash;

    DeviceInfo m_deviceInfo;
    MachineInfo m_machineInfo;
    std::map<int, GameSession> m_gameSessions; // key: session id
    bool m_isNfcCapable {false};

    std::vector<std::string> m_nonces;
    mutable std::mutex m_noncesMutex;

    mutable std::mutex m_scorbitronObjectMutex;
    nlohmann::json m_scorbitronObject;

    Updater m_updater;
    PlayerProfilesManager m_playersManager;

    std::shared_ptr<nfc::ProbesManager> m_probesManager;

    // -----------------------------------------------------------------------

    // This must be last element, as it has to be destroyed first, otherwise it will try to access
    // already destroyed member variables
    Worker m_worker;

    // Centrifugo client for real-time updates, it depends on m_worker's strand and has to be
    // created after m_worker and destroyed before m_worker
    std::unique_ptr<centrifugo::Client> m_centrifugo;
    struct RetiredCentrifugoClient
    {
        std::chrono::steady_clock::time_point retiredAt;
        std::unique_ptr<centrifugo::Client> client;
    };
    // Keep restarted clients alive for a short grace period so pending transport callbacks can
    // unwind safely without retaining every historical client for the rest of the process.
    std::deque<RetiredCentrifugoClient> m_retiredCentrifugoClients;
    std::atomic_bool m_restartCentrifugoPending {false};

    std::optional<bool> m_lastEmittedPairingState;

    std::shared_ptr<EventManager> m_eventManager;
};

} // namespace detail
} // namespace scorbit
