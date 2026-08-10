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

/**
 * @file game_state_handle.h
 * @brief Internal definition of the opaque C API handle and its job queue.
 *
 * `sb_game_handle_t` is an opaque pointer in the public headers. This header is the SDK-internal
 * definition behind it, shared by the C shim translation units (`game_state_c.cpp`,
 * `achievements_c.cpp`) so they can reach `handle->gameState` and `handle->postApiJob()`.
 *
 * @section gsh_convention Calling convention
 *
 * Every **mutating or network** C API call is posted through the job queue and dispatched on a
 * dedicated thread; plain synchronous reads (`sb_get_machine_uuid`, the achievement cache
 * accessors, the local matchers) call `handle->gameState` directly. Functions that must return a
 * value to their caller cannot be queued, and @ref scorbit::detail::AchievementManager carries its
 * own mutexes, so there is nothing to serialise for those.
 *
 * @warning Not a public header. Do not install it.
 */

#pragma once

#include <scorbit_sdk/achievements.h>
#include <scorbit_sdk/achievements_c.h>
#include <scorbit_sdk/common_types_c.h>
#include <scorbit_sdk/game_state_c.h>
#include <scorbit_sdk/leaderboard_c.h>
#include <scorbit_sdk/net_types.h>
#include <scorbit_sdk/net_types_c.h>

#include "game_state_impl.h"
#include "leaderboard_internal.h"
#include "net_base.h"

#include <blockingconcurrentqueue.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

struct sb_game_state_struct;

namespace scorbit_c_api_queue {

inline std::string copyCStr(const char *p)
{
    return p ? std::string(p) : std::string {};
}

inline scorbit::HttpHeaders copyHeaders(const sb_http_header_t *headers, size_t count)
{
    scorbit::HttpHeaders result;
    if (headers && count > 0) {
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            result.emplace_back(copyCStr(headers[i].name), copyCStr(headers[i].value));
        }
    }
    return result;
}

struct Poison {
};

struct JobSetGameStarted {
    sb_game_state_struct *h;
    sb_game_start_origin_t origin;
};

struct JobSetGameFinished {
    sb_game_state_struct *h;
};

struct JobSetCurrentBall {
    sb_game_state_struct *h;
    sb_ball_t ball;
};

struct JobSetActivePlayer {
    sb_game_state_struct *h;
    sb_player_t player;
};

struct JobSetScore {
    sb_game_state_struct *h;
    sb_player_t player;
    sb_score_t score;
    sb_score_feature_t feature;
};

struct JobAddMode {
    sb_game_state_struct *h;
    std::string mode;
};

struct JobAddModeExpiring {
    sb_game_state_struct *h;
    std::string mode;
    uint32_t duration_seconds;
};

struct JobTickModeExpiries {
    sb_game_state_struct *h;
};

struct JobRemoveMode {
    sb_game_state_struct *h;
    std::string mode;
};

struct JobClearModes {
    sb_game_state_struct *h;
};

struct JobCommit {
    sb_game_state_struct *h;
};

struct JobRequestTopScores {
    sb_game_state_struct *h;
    sb_leaderboard_scope_t scope;
    sb_leaderboard_period_t period;
    std::string since;
    sb_leaderboard_vpin_filter_t vpin_filter;
    sb_leaderboard_callback_t callback;
    void *user_data;
};

struct JobRequestPairCode {
    sb_game_state_struct *h;
    sb_string_callback_t callback;
    void *user_data;
};

struct JobRequestUnpair {
    sb_game_state_struct *h;
    sb_string_callback_t callback;
    void *user_data;
};

struct JobSetCapabilities {
    sb_game_state_struct *h;
    sb_capabilities_t capabilities;
};

struct JobPairMachine {
    sb_game_state_struct *h;
    std::string machine_uuid;
    std::string owner_uuid;
    sb_string_callback_t callback;
    void *user_data;
};

struct JobCreditsDropped {
    sb_game_state_struct *h;
    int credits;
    std::string transaction;
    bool success;
};

struct JobCreditsStatus {
    sb_game_state_struct *h;
    bool free_play;
    int credits;
    int max_credits;
    std::string pricing;
};

struct JobDownload {
    sb_game_state_struct *h;
    std::string url;
    std::string filename;
    scorbit::HttpHeaders headers;
    sb_string_callback_t callback;
    void *user_data;
};

struct JobDownloadBuffer {
    sb_game_state_struct *h;
    std::string url;
    size_t reserve_buffer_size;
    scorbit::HttpHeaders headers;
    sb_buffer_callback_t callback;
    void *user_data;
};

struct JobUploadDiagnostics {
    sb_game_state_struct *h;
    std::vector<std::string> logPaths;
    std::vector<std::string> recordingPaths;
    std::string logString;
};

// ---- Achievements. Only the four network calls are queued; see @ref gsh_convention. ----

struct JobFetchAchievements {
    sb_game_state_struct *h;
    sb_achievements_callback_t callback;
    void *user_data;
};

struct JobFetchAchievementProgress {
    sb_game_state_struct *h;
    std::string user_id;
    sb_achievement_progress_callback_t callback;
    void *user_data;
};

struct JobUnlockAchievement {
    sb_game_state_struct *h;
    std::string user_id;
    std::string key;
    int count;
    sb_achievement_unlock_callback_t callback;
    void *user_data;
};

struct JobLockAchievement {
    sb_game_state_struct *h;
    std::string user_id;
    std::string key;
    sb_achievement_unlock_callback_t callback;
    void *user_data;
};

using ApiQueueItem =
        std::variant<Poison, JobSetGameStarted, JobSetGameFinished, JobSetCurrentBall,
                     JobSetActivePlayer, JobSetScore, JobAddMode, JobAddModeExpiring,
                     JobTickModeExpiries, JobRemoveMode, JobClearModes, JobCommit,
                     JobRequestTopScores, JobRequestPairCode, JobRequestUnpair, JobSetCapabilities,
                     JobPairMachine, JobCreditsDropped, JobCreditsStatus, JobDownload,
                     JobDownloadBuffer, JobUploadDiagnostics, JobFetchAchievements,
                     JobFetchAchievementProgress, JobUnlockAchievement, JobLockAchievement>;

// Combines lambdas into one functor for std::visit (standard C++17 pattern). C++17 helper for
// std::visit. In C++20+, equivalent functionality may be provided by a standard or library helper
// (std::overloaded).
template<class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template<class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

inline auto makeCStringReplyBridge(sb_string_callback_t cb, void *user_data)
{
    return [cb, user_data](scorbit::Error error, const std::string &reply) {
        if (cb) {
            cb(static_cast<sb_error_t>(error), reply.c_str(), user_data);
        }
    };
}

inline auto makeBufferReplyBridge(sb_buffer_callback_t cb, void *user_data)
{
    return [cb, user_data](scorbit::Error error, const std::vector<uint8_t> &data) {
        if (cb) {
            cb(static_cast<sb_error_t>(error), data.data(), data.size(), user_data);
        }
    };
}

inline auto makeLeaderboardReplyBridge(sb_leaderboard_callback_t cb, void *user_data)
{
    return [cb, user_data](scorbit::Error error, sb_leaderboard_t *leaderboard) {
        if (cb) {
            cb(static_cast<sb_error_t>(error), leaderboard, user_data);
        }
        if (leaderboard) {
            scorbit::detail::destroyLeaderboard(leaderboard);
        }
    };
}

// Defined in achievements_c.cpp, alongside the C-struct marshalling they depend on.
scorbit::AchievementsCallback makeAchievementsReplyBridge(sb_achievements_callback_t cb,
                                                          void *user_data);
scorbit::AchievementProgressCallback
makeAchievementProgressReplyBridge(sb_achievement_progress_callback_t cb, void *user_data);
scorbit::AchievementUnlockCallback
makeAchievementUnlockReplyBridge(sb_achievement_unlock_callback_t cb, void *user_data);

} // namespace scorbit_c_api_queue

struct sb_game_state_struct {
    scorbit::detail::GameStateImpl gameState;
    moodycamel::BlockingConcurrentQueue<scorbit_c_api_queue::ApiQueueItem> cApiQueue;
    std::atomic<bool> cApiAccepting {true};
    std::thread cApiDispatcher;

    explicit sb_game_state_struct(std::unique_ptr<scorbit::detail::NetBase> net);
    ~sb_game_state_struct();

    void postApiJob(scorbit_c_api_queue::ApiQueueItem &&job);
    void shutdownCApiDispatcher();

private:
    void cApiDispatcherLoop();
};
