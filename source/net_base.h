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

#include <nfc/probes_manager.h>
#include <scorbit_sdk/net_types.h>
#include <scorbit_sdk/common_types_c.h>
#include "device_info.h"
#include "player_profiles_manager.h"
#include "event_classes.h"
#include "session_flags.h"
#include <boost/signals2.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <atomic>
#include <chrono>
#include <functional>

namespace spb {
class ProbesManager;
}

namespace scorbit {
namespace detail {

struct GameData;

class NetBase
{
public:
    NetBase() = default;
    virtual ~NetBase() = default;

    virtual AuthStatus status() const = 0;

    virtual void authenticate() = 0;

    virtual void updateConfig(const std::string &type, const std::string &version, bool installed,
                              std::optional<std::string> log = std::nullopt) = 0;
    virtual void sessionCreate(const detail::GameData &data, GameStartOrigin origin,
                               std::function<void()> onCreated) = 0;
    virtual void submitGameData(const detail::GameData &data, SessionFlags flags) = 0;
    virtual void sendHeartbeat() = 0;
    virtual void getConfig() = 0;
    virtual void requestPairCode(StringCallback cb) = 0;

    virtual const std::string &getMachineUuid() const = 0;
    virtual std::uint64_t getMachineSerial() const = 0;
    virtual const std::string &getPairDeeplink() const = 0;

    virtual const DeviceInfo &deviceInfo() const = 0;

    virtual void requestTopScores(sb_score_t scoreFilter, StringCallback callback) = 0;
    virtual void requestUnpair(StringCallback callback) = 0;

    virtual void download(bool isAsync, StringCallback callback, const std::string &url,
                          const std::string &filename, const HttpHeaders &headers = {}) = 0;
    virtual void downloadBuffer(bool isAsync, VectorCallback callback, const std::string &url,
                                size_t reserveBufferSize, const HttpHeaders &headers = {}) = 0;

    virtual PlayerProfilesManager &playersManager() = 0;

    virtual void patchScorbitron(std::string body, StringCallback callback,
                                 std::vector<AuthStatus> allowedStatuses) = 0;

    virtual std::string consumeNonce() = 0;

    virtual void requestPairMachine(const std::string &machineUuid, const std::string &ownerUuid,
                                    StringCallback callback) = 0;

    virtual void setCapabilities(Capabilities capabilities) = 0;

    virtual void setCreditsDropped(int credits, const std::string &transaction, bool success) = 0;
    virtual void setCreditsStatus(bool freePlay, int credits, int maxCredits,
                                  const char *pricing) = 0;

    /** Runs @p fn on the network worker thread after @p delay (used for expiring game modes). */
    virtual void scheduleDelayedOnWorker(std::chrono::steady_clock::duration delay,
                                         std::function<void()> fn)
    {
        (void)delay;
        (void)fn;
    }

    virtual void cancelModeExpiryTimer() { }

    virtual void uploadDiagnostics(std::vector<std::string> logPaths,
                                   std::vector<std::string> recordingPaths, std::string logString)
    {
        (void)logPaths;
        (void)recordingPaths;
        (void)logString;
    }

    // ---------------------------------------------------------------------------------

    virtual void setProbesManager(std::shared_ptr<nfc::ProbesManager> manager) { (void)manager; };

    void setNumberOfPlayersRequested(int count) { m_numberOfPlayersRequested = count; }
    int numberOfPlayersRequested() const { return m_numberOfPlayersRequested.load(); }

private:
    std::atomic<int> m_numberOfPlayersRequested {0};
};

} // namespace detail
} // namespace scorbit
