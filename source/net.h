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
#include "leaderboard_internal.h"
#include "net_base.h"
#include "key_resolver.h"
#include "game_data.h"
#include "worker.h"
#include "updater.h"
#include "identifiers.h"
#include "event_manager.h"
#include "heartbeat.h"
#include "utils/machine_fingerprint.h"
#include <centrifugo.h>
#include <fmt/format.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <functional>
#include <chrono>
#include <deque>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
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
        std::optional<std::string> gameSlug;
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
    void getConfig() override;
    void requestPairCode(StringCallback callback) override;

    const std::string &getMachineUuid() const override;
    std::uint64_t getMachineSerial() const override;
    const std::string &getPairDeeplink() const override;

    const DeviceInfo &deviceInfo() const override;

    void requestTopScores(LeaderboardScope scope, LeaderboardPeriod period,
                          const std::string &since, LeaderboardVpinFilter vpinFilter,
                          LeaderboardHandleCallback callback) override;
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

    void sessionUpdate(int sessionId, SessionFlags flags);

    /// Come online because the heartbeat server has work waiting. Runs on the heartbeat strand.
    void onHeartbeatWake();

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
    /// Re-send the scorbitron PATCH on a backoff while the pairing status is still unresolved.
    void retryScorbitronObjectIfPairingUnresolved();

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

    bool isLeaderboardContextReady(LeaderboardScope scope) const;
    std::optional<Error> leaderboardRequestTerminalError() const;
    void requestTopScoresImpl(LeaderboardScope scope, LeaderboardPeriod period,
                              const std::string &since, LeaderboardVpinFilter vpinFilter,
                              LeaderboardHandleCallback callback, int deferAttempt);
    void processScoresAndPlayersProfiles(const nlohmann::json &val, GameSession &gameSession);

    bool isActiveCentrifugoClient(const centrifugo::Client *client) const;
    void pruneRetiredCentrifugoClients();
    void retireCentrifugoClient();
    void centrifugoSetup(bool fetchFreshToken = false);
    void centrifugoConnect();

    /// @return The Centrifugo JWT held in the cache, empty if none is ready yet.
    std::string cachedCentrifugoToken() const;
    /// Fetch a Centrifugo JWT and store it in the cache. Blocks; call only on the blocking
    /// executor, never on the Centrifugo strand.
    void fetchCentrifugoTokenNow();
    /// Ask for a cache refresh without blocking the caller. At most one fetch is in flight.
    void refreshCentrifugoToken();
    /// Arm the next refresh from the token's own expiry, staying ahead of the client's request.
    void scheduleCentrifugoTokenRefresh(const std::string &token);

    /// Arm the idle disconnect for a Centrifugo connection that a heartbeat wake just opened.
    void startCentrifugoIdleTimer();
    /// Give the idle window a full extension, but only when it is already armed.
    void resetCentrifugoIdleTimerIfArmed();
    void stopCentrifugoIdleTimer();
    void setupAndConnectCentrifugo(bool fetchFreshToken = false);
    void restartCentrifugo();

    void clearPairedMachineContext();
    void emitPairingStatusEventIfChanged(bool isPaired);
    void onPaired();
    void onUnpaired();
    /// Enter AuthenticationFailed and stop work that a non-authenticating machine should not do.
    void onAuthenticationFailed();

    std::optional<std::chrono::seconds> getTimeUntilTokenExpiration() const;

    /// @return The error to report for a request the gate will never let through.
    Error authGateError() const;
    /**
     * Hold a request until the authentication status lets it run.
     *
     * The request is not allowed to occupy a thread while it waits: a handful of requests
     * arriving before authentication settles would otherwise take every thread the SDK has and
     * leave nothing to finish authenticating with. @p resume runs on @p executor once the gate
     * opens; if it never does, @p callback is invoked with an error at the deadline, so a caller
     * always gets an answer.
     */
    void parkOnAuthGate(task_t resume, StringCallback callback,
                        std::vector<AuthStatus> allowedStatuses);
    /// Re-evaluate every parked request. Called on every authentication status change.
    void notifyAuthStatusChanged();
    /// Arm the deadline timer for the earliest waiter. Caller must hold m_authGateMutex.
    void armAuthGateTimer();

    /// Hand the pair code to everyone waiting for it, or fail those whose deadline has passed.
    void notifyShortCodeChanged();
    /// Arm the deadline timer for the earliest waiter. Caller must hold m_shortCodeMutex.
    void armPairCodeTimer();

    void createNfcNonces();
    void startNfcCheckTimer();
    void setNfcTag();
    void checkNfcBootReason();

    void requestCreditsStatusEvent();
    /// Request credits status from the app only when Centrifugo is connected and
    /// `m_machineChannel` is set (must match a server-side subscription from the JWT).
    void requestCreditsStatusIfReady();

    void requestFirmwaresList();

    // Diagnostic probe (SB-3363) — dispatched from the control_machine branch
    // of onPublication. handleDiagnosticProbe validates the payload, dedupes
    // duplicate trace_ids, and hands the publish + ack work off to the worker
    // strand via the two helpers below.
    void handleDiagnosticProbe(const nlohmann::json &payload);
    void publishDiagnosticPacket(const std::string &traceId, uint64_t sequence,
                                 const std::string &createdAt);
    void postDiagnosticAck(const std::string &traceId, uint64_t sequence,
                           const std::string &createdAt);

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

        const auto myurl = fmt::format("{}/{}", m_hostname, formattedEndpoint);
        return cpr::Url {myurl};
    }

private:
    bool validateDeviceInfo() const;
    bool resolveKeys(const std::string &serverTimestamp);
    bool reprovisionSoftKey(const std::string &serverTimestamp);

    SignerCallback m_signer;
    std::vector<std::unique_ptr<IKeyResolver>> m_keyResolvers;

    /// A request waiting for the authentication status to allow it.
    struct AuthGateWaiter {
        task_t resume;
        StringCallback callback;
        std::vector<AuthStatus> allowedStatuses;
        std::chrono::steady_clock::time_point deadline;
        /// Strand the request came from, so it resumes where its ordering guarantees hold.
        boost::asio::any_io_executor executor;
    };

    /// A caller waiting for the pair code to arrive with the scorbitron reply.
    struct ShortCodeWaiter {
        StringCallback callback;
        std::chrono::steady_clock::time_point deadline;
        boost::asio::any_io_executor executor;
    };

    std::atomic<AuthStatus> m_status {AuthStatus::NotAuthenticated};
    std::mutex m_authGateMutex;
    std::vector<AuthGateWaiter> m_authGateWaiters;
    std::vector<ShortCodeWaiter> m_shortCodeWaiters; // guarded by m_shortCodeMutex
    mutable std::mutex m_authMutex;
    std::mutex m_gameSessionsMutex;
    std::mutex m_shortCodeMutex;
    std::mutex m_nfcMutex;
    mutable std::shared_mutex m_tokenMutex;
    std::atomic_bool m_isGameDataInQueue {false};
    std::atomic_bool m_stop {false};
    std::atomic_bool m_isRefreshingToken {false};
    std::chrono::seconds m_authRetryBackoff;
    std::chrono::seconds m_scorbitronRetryBackoff;

    // The Centrifugo client asks for a JWT from its own strand, through a callback that has to
    // return synchronously. Fetching it there would stall the websocket, the ping timer and every
    // subscription for the length of an HTTP request, so the token is kept ready here instead and
    // refreshed ahead of when the client will ask.
    mutable std::mutex m_cfTokenMutex;
    std::string m_cfToken;
    std::atomic_bool m_cfTokenFetchInFlight {false};

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

    // Diagnostic probe (SB-3363). m_diagProbeSequence is a per-process
    // counter for the JKEY_SCR_SEQUENCE field on outbound probe publications;
    // it is intentionally independent of GameSession::sessionCounter so a
    // diag_probe never perturbs game-session state. m_seenDiagTraceIds is a
    // bounded in-memory dedupe so a duplicate probe (Centrifugo history
    // replay after a restart, etc.) cannot double-publish.
    std::atomic<uint64_t> m_diagProbeSequence {0};
    std::unordered_set<std::string> m_seenDiagTraceIds;
    mutable std::mutex m_seenDiagTraceIdsMutex;

    Updater m_updater;
    PlayerProfilesManager m_playersManager;

    std::shared_ptr<nfc::ProbesManager> m_probesManager;

    // -----------------------------------------------------------------------

    // This must be last element, as it has to be destroyed first, otherwise it will try to access
    // already destroyed member variables
    Worker m_worker;

    // Runs on m_worker's heartbeat strand, so it has to be created after m_worker and destroyed
    // before m_worker
    Heartbeat m_heartbeat;

    // Centrifugo client for real-time updates, it depends on m_worker's strand and has to be
    // created after m_worker and destroyed before m_worker
    std::unique_ptr<centrifugo::Client> m_centrifugo;
    struct RetiredCentrifugoClient {
        std::chrono::steady_clock::time_point retiredAt;
        std::unique_ptr<centrifugo::Client> client;
    };
    // Keep restarted clients alive for a short grace period so pending transport callbacks can
    // unwind safely without retaining every historical client for the rest of the process.
    std::deque<RetiredCentrifugoClient> m_retiredCentrifugoClients;
    std::atomic_bool m_restartCentrifugoPending {false};
    // True only while a heartbeat-wake-opened connection is on its idle countdown. Gates the
    // activity reset so a connection we did not open never acquires an idle disconnect.
    std::atomic_bool m_isCentrifugoIdleTimerArmed {false};

    std::optional<bool> m_lastEmittedPairingState;

    std::shared_ptr<EventManager> m_eventManager;
};

} // namespace detail
} // namespace scorbit
