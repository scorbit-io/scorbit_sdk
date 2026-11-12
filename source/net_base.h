/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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
#include "leaderboard_internal.h"
#include "device_info.h"
#include "download_options.h"
#include "player_profiles_manager.h"
#include "event_classes.h"
#include "event_queue.h"
#include "session_flags.h"
#include "table_mappings_internal.h"
#include "competitions/message_outbox.h"
#include "competitions/messages.h"
#include <boost/signals2.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>

namespace spb {
class ProbesManager;
}

namespace scorbit {
namespace detail {

struct GameData;

/** Called once the server has created the session, with the uuid it assigned. */
using SessionCreatedCallback = std::function<void(const std::string &sessionUuid)>;

/** Called with the new player profiles whenever a player claims or leaves a slot. */
using PlayersChangedCallback = std::function<void(const std::vector<PlayerProfile> &profiles)>;

/** Called after the scheduled refresh of the API token succeeded. */
using TokenRefreshedCallback = std::function<void()>;

/** Outcome of an achievements API request. */
struct ApiReply {
    Error error {Error::ApiError};

    /** Status of the final attempt; 0 when no HTTP response was received. 304 is a success. */
    int httpStatus {0};

    std::string body;

    /** The response's `ETag`, when it carried one. */
    std::string etag;
};

using ApiReplyCallback = std::function<void(ApiReply reply)>;

/** Called with each `competition.goal` that parsed. */
using CompetitionGoalCallback = std::function<void(competitions::CompetitionGoal goal)>;

/** One claimed slot of a session, as a session reply's `scores` names it. */
struct SessionSeat {
    std::string userId;
    /** The competition the slot's Entry is for (`tournament_id`); empty when it has none. */
    std::string tournament;
};

/** Who holds which slot of one session: position → seat, claimed slots only. */
using SessionSeats = std::map<sb_player_t, SessionSeat>;

/** Called with the seats every session reply carries, under that session's uuid. */
using SessionPlayersCallback =
        std::function<void(const std::string &sessionUuid, SessionSeats seats)>;

class NetBase
{
public:
    NetBase() = default;
    virtual ~NetBase() = default;

    virtual AuthStatus status() const = 0;

    // Connectivity evidence for the host (sb_realtime_connected and its siblings). The defaults
    // say "no evidence", for test doubles.
    virtual bool realtimeConnected() const { return false; }
    virtual int64_t secondsSinceRestSuccess() const { return -1; }
    virtual uint32_t restTransportFailures() const { return 0; }
    virtual int64_t secondsSinceHeartbeatReply() const { return -1; }

    virtual void authenticate() = 0;

    /**
     * Reports a typed config update to the API.
     *
     * @p callback, when set, receives the SDK-level error, the HTTP status of the final attempt
     * (0 when no HTTP response was received) and the raw reply body. The SDK does not retry a 4xx
     * or 5xx, apart from a 401, which it answers by re-authenticating and trying again; the
     * reported status is that of the final attempt.
     */
    virtual void updateConfig(const std::string &type, const std::string &version, bool installed,
                              std::optional<std::string> log = std::nullopt,
                              HttpStatusCallback callback = {}) = 0;
    virtual void sessionCreate(const detail::GameData &data, GameStartOrigin origin,
                               SessionCreatedCallback onCreated) = 0;
    virtual void submitGameData(const detail::GameData &data, SessionFlags flags) = 0;
    virtual void getConfig() = 0;
    virtual void requestPairCode(StringCallback cb) = 0;

    virtual const std::string &getMachineUuid() const = 0;
    virtual std::uint64_t getMachineSerial() const = 0;
    virtual const std::string &getPairDeeplink() const = 0;

    virtual const DeviceInfo &deviceInfo() const = 0;

    virtual void requestTopScores(LeaderboardScope scope, LeaderboardPeriod period,
                                  const std::string &since,
                                  LeaderboardVpinFilter vpinFilter,
                                  LeaderboardHandleCallback callback) = 0;
    virtual void requestUnpair(StringCallback callback) = 0;

    virtual void download(bool isAsync, StringCallback callback, const std::string &url,
                          const std::string &filename, const HttpHeaders &headers) = 0;
    /// download() under @p options' limits; see sb_download_with_options.
    virtual void downloadWithOptions(bool isAsync, StringCallback callback, const std::string &url,
                                     const std::string &filename, const HttpHeaders &headers,
                                     const DownloadOptions &options)
    {
        (void)isAsync;
        (void)url;
        (void)filename;
        (void)headers;
        (void)options;
        if (callback) {
            callback(Error::Unknown, "unsupported");
        }
    }
    virtual void downloadBuffer(bool isAsync, VectorCallback callback, const std::string &url,
                                size_t reserveBufferSize, const HttpHeaders &headers) = 0;

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

    /**
     * Uploads diagnostics. @p requestGeneration, when set, is echoed back to the API so it can
     * tell which request this upload answers; when unset the field is omitted entirely.
     */
    virtual void uploadDiagnostics(std::vector<std::string> logPaths,
                                   std::vector<std::string> recordingPaths, std::string logString,
                                   std::optional<std::uint64_t> requestGeneration = std::nullopt)
    {
        (void)logPaths;
        (void)recordingPaths;
        (void)logString;
        (void)requestGeneration;
    }

    /**
     * POSTs a hardware probe result (@p resultJson, sent verbatim) for @p runId. @p callback gets
     * the HTTP status of the final attempt, 0 when none was received. Invalid input is answered
     * with Error::Unknown and status 0 without sending anything.
     */
    virtual void submitHardwareProbeResult(const std::string &runId, const std::string &resultJson,
                                           HttpStatusCallback callback = {}) = 0;

    /// POSTs a probe flash stage for @p runId, once; see sb_submit_probe_flash_stage.
    virtual void submitProbeFlashStage(const std::string &runId, const std::string &stageJson,
                                       HttpStatusCallback callback = {})
    {
        (void)runId;
        (void)stageJson;
        if (callback) {
            callback(Error::Unknown, 0, "unsupported");
        }
    }

    /// POSTs a probe flash result for @p runId until final; see sb_submit_probe_flash_result.
    virtual void submitProbeFlashResult(const std::string &runId, const std::string &resultJson,
                                        HttpStatusCallback callback = {})
    {
        (void)runId;
        (void)resultJson;
        if (callback) {
            callback(Error::Unknown, 0, "unsupported");
        }
    }

    // ---- Achievements (contract v1, §10) --------------------------------------------

    /** `GET achievements/scorbitron/`, revalidated with `If-None-Match: @p etag` when not empty. */
    virtual void fetchAchievementDefinitions(std::string etag, ApiReplyCallback callback)
    {
        (void)etag;
        (void)callback;
    }

    /** `GET achievements/scorbitron/progress/?user_id=@p userId`. */
    virtual void fetchAchievementProgress(std::string userId, ApiReplyCallback callback)
    {
        (void)userId;
        (void)callback;
    }

    /** `POST achievements/report/` with the JSON @p body. */
    virtual void postAchievementReport(std::string body, ApiReplyCallback callback)
    {
        (void)body;
        (void)callback;
    }

    /** Downloads one achievement's artwork (its `frame` or `icon_url`) from @p url to memory. */
    virtual void downloadAchievementArtwork(std::string url, VectorCallback callback)
    {
        (void)url;
        (void)callback;
    }

    /** Runs @p fn on a worker thread after @p delay, replacing any pending one (report retries). */
    virtual void scheduleAchievementRetry(std::chrono::steady_clock::duration delay,
                                          std::function<void()> fn)
    {
        (void)delay;
        (void)fn;
    }

    /**
     * Runs @p fn on a worker thread after @p delay, replacing any pending one (definitions
     * revalidation).
     */
    virtual void scheduleDefinitionsRevalidation(std::chrono::steady_clock::duration delay,
                                                 std::function<void()> fn)
    {
        (void)delay;
        (void)fn;
    }

    /** Delivers @p event to game code through the event callback. */
    virtual void publishEvent(EventPtr event) { (void)event; }

    /**
     * Whether the system clock agrees with the server's: a server reading has arrived, and the
     * clock is within the drift the SDK tolerates. Local time judged on any other clock can't be
     * trusted (§5.5.2).
     */
    virtual bool isClockSynchronised() const { return true; }

    /** @p callback is invoked from a network thread whenever player profiles change. */
    virtual void setPlayersChangedCallback(PlayersChangedCallback callback) { (void)callback; }

    /** @p callback is invoked from a network thread after each scheduled token refresh. */
    virtual void setTokenRefreshedCallback(TokenRefreshedCallback callback) { (void)callback; }

    // ---- Competitions (SB-6437) -----------------------------------------------------

    /**
     * @p callback is invoked from a network thread with each `competition.goal` that parsed.
     * One that didn't is refused by the network layer, which raises the event itself.
     */
    virtual void setCompetitionGoalCallback(CompetitionGoalCallback callback) { (void)callback; }

    /**
     * @p callback is invoked from a network thread with the seats of each session reply that
     * carries them, whether or not they changed the machine's profiles.
     */
    virtual void setSessionPlayersCallback(SessionPlayersCallback callback) { (void)callback; }

    /** `POST competitions/goal-installed/` or `.../attempt-started/` with the JSON @p body. */
    virtual void postCompetitionMessage(competitions::MessageKind kind, std::string body,
                                        ApiReplyCallback callback)
    {
        (void)kind;
        (void)body;
        (void)callback;
    }

    /** Runs @p fn on a worker thread after @p delay, replacing any pending one (resends). */
    virtual void scheduleCompetitionRetry(std::chrono::steady_clock::duration delay,
                                          std::function<void()> fn)
    {
        (void)delay;
        (void)fn;
    }

    // ---------------------------------------------------------------------------------

    virtual void setProbesManager(std::shared_ptr<nfc::ProbesManager> manager) { (void)manager; };

    /// A host-reported probe's firmware after startup; nullopt marks it not attached.
    virtual void setProbeFirmware(const std::string &probe, std::optional<ProbeFirmware> firmware)
    {
        (void)probe;
        (void)firmware;
    }
    virtual void setPbspkcommFirmwareId(const std::string &firmwareId) { (void)firmwareId; }

    /// The VenueMachine uuid the device is on (the scorbitron object's machine.id), "" if none.
    virtual std::string venueMachine() const { return {}; }
    /// A copy of the VPIN table mappings (sb_get_table_mappings).
    virtual TableMappingsSnapshot tableMappings() const { return {}; }
    /// Re-reads the VPIN table mappings, resuming reads a refusal stopped.
    virtual void refreshTableMappings() { }
    /// Starts a switch to @p venueMachine; the game-active refusal is the caller's.
    virtual void switchMachine(const std::string &venueMachine, MachineSwitch::Callback callback)
    {
        if (callback) {
            callback(SB_SWITCH_MACHINE_FAILED, venueMachine);
        }
    }
    /// Looks up which VenueMachine a table maps to (sb_match_table_mapping).
    virtual void matchTableMapping(TableMatchRequestData request, TableMatchCallback callback)
    {
        (void)request;
        if (callback) {
            callback(SB_TABLE_MATCH_FAILED, TableMatchAnswer {});
        }
    }

    void setNumberOfPlayersRequested(int count) { m_numberOfPlayersRequested = count; }
    int numberOfPlayersRequested() const { return m_numberOfPlayersRequested.load(); }

private:
    std::atomic<int> m_numberOfPlayersRequested {0};
};

} // namespace detail
} // namespace scorbit
