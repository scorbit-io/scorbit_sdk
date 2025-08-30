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
#include "game_data.h"
#include "worker.h"
#include "updater.h"
#include <centrifugo.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <chrono>
#include <condition_variable>

namespace scorbit {
namespace detail {

using Signature = std::vector<uint8_t>;
using Digest = std::array<uint8_t, DIGEST_LENGTH>;

using SignerCallback = std::function<Signature(const Digest &digest)>;

std::string getSignature(const SignerCallback &signer, const std::string &uuid,
                         const std::string &timestamp);

class SafeMultipart;

class Net : public NetBase
{
    using deferred_get_setup_t = std::function<std::tuple<cpr::Url, cpr::Parameters>()>;
    using deferred_post_setup_t = std::function<std::tuple<cpr::Url, cpr::Body>()>;
    using deferred_patch_setup_t = std::function<std::tuple<cpr::Url, cpr::Body>()>;

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
    Net(SignerCallback signer, DeviceInfo deviceInfo, bool useEncryptedKey);
    ~Net() override;

    AuthStatus status() const override;

    const std::string &hostname() const;
    const std::string &cfHostname() const; // Centrifugo hostname
    void setHostname(std::string hostname, std::string cfHostname = std::string {});
    bool isAuthenticated() const;

    void authenticate() override;
    void updateConfig(const std::string &type, const std::string &version, bool installed,
                      std::optional<std::string> log = std::nullopt) override;
    void sessionCreate(const detail::GameData &data) override;
    void sendGameData(const detail::GameData &data) override;
    void sendHeartbeat() override;
    void getConfig() override;
    void requestPairCode(StringCallback callback) override;

    const std::string &getMachineUuid() const override;
    const std::string &getPairDeeplink() const override;
    const std::string &getClaimDeeplink(int player) const override;

    const DeviceInfo &deviceInfo() const override;

    void requestTopScores(sb_score_t scoreFilter, StringCallback callback) override;
    void requestUnpair(StringCallback callback) override;

    void download(StringCallback callback, const std::string &url,
                  const std::string &filename) override;
    void downloadBuffer(VectorCallback callback, const std::string &url,
                        size_t reserveBufferSize) override;

    PlayerProfilesManager &playersManager() override;

private:
    task_t createAuthenticateTask();
    task_t updateConfigTask(const std::string &type, const std::string &version, bool installed,
                            std::optional<std::string> log);
    task_t createSessionCreate(int sessionId);
    task_t createGameDataTask(int sessionId);
    task_t createHeartbeatTask();

    void startHeartbeatTimer();

    void postUploadHistoryTask(const GameHistory &history);
    task_t createUploadHistoryTask(const GameHistory &history);

    task_t createUploadTask(const std::string &endpoint, const std::string &name,
                            SafeMultipart &&multipart);

    // Generic HTTP request task creator
    template<typename DeferredSetupT, typename HttpMethodT>
    task_t createHttpRequestTask(const char *requestType, StringCallback replyCallback,
                                 DeferredSetupT deferredSetup, HttpMethodT httpMethod,
                                 std::vector<AuthStatus> allowedStatuses = {
                                         AuthStatus::AuthenticatedPaired});

    // Specialized methods for different HTTP methods
    task_t createGetRequestTask(StringCallback replyCallback, deferred_get_setup_t deferredSetup,
                                std::vector<AuthStatus> allowedStatuses = {
                                        AuthStatus::AuthenticatedPaired});
    task_t createPostRequestTask(StringCallback replyCallback, deferred_post_setup_t deferredSetup,
                                 std::vector<AuthStatus> allowedStatuses = {
                                         AuthStatus::AuthenticatedPaired});
    task_t createPatchRequestTask(StringCallback replyCallback,
                                  deferred_patch_setup_t deferredSetup,
                                  std::vector<AuthStatus> allowedStatuses = {
                                          AuthStatus::AuthenticatedPaired});
    task_t createDownloadFileTask(StringCallback replyCallback, std::string url,
                                  std::string filename);
    task_t createDownloadBufferTask(VectorCallback replyCallback, std::string url,
                                    size_t reserveBufferSize);

    cpr::Header header() const;
    cpr::Header authHeader() const;

    cpr::Url url(std::string_view endpoint) const;
    bool checkAllowedStatuses(const std::vector<AuthStatus> &allowedStatuses) const;
    void processPlayersProfiles(const nlohmann::json &val);

    void centrifugoSetup();
    void centrifugoConnect();

private:
    SignerCallback m_signer;

    std::atomic<AuthStatus> m_status {AuthStatus::NotAuthenticated};
    std::condition_variable m_authCV;
    std::mutex m_authMutex;
    std::mutex m_gameSessionsMutex;
    std::atomic_bool m_isGameDataInQueue {false};
    std::atomic_bool m_isHeartbeatInQueue {false};
    std::atomic_bool m_stopHeartbeatTimer {false};
    std::atomic_bool m_stop {false};

    std::string m_hostname;
    std::string m_cfHostname;
    std::string m_stoken;
    std::string m_cachedShortCode; // As short code for the pairing is permanent, we can cache it
    mutable std::string m_cachedPairDeeplink;
    mutable std::string m_cachedCclaimDeeplink;

    DeviceInfo m_deviceInfo;
    VenueMachineInfo m_vmInfo;
    std::map<int, GameSession> m_gameSessions; // key: session id

    Updater m_updater;
    PlayerProfilesManager m_playersManager;

    std::unique_ptr<centrifugo::Client> m_centrifugo; // Centrifugo client for real-time updates

    Worker m_worker; // This must be last element, as it has to be destroyed first, otherwise it
                     // will try to access already destroyed member variables
};

} // namespace detail
} // namespace scorbit
