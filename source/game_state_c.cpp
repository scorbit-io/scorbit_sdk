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

#include <scorbit_sdk/game_state_c.h>
#include <scorbit_sdk/leaderboard_c.h>
#include <scorbit_sdk/net_types.h>
#include <scorbit_sdk/net_types_c.h>
#include <scorbit_sdk/game_state_factory.h>
#include "device_info.h"
#include "game_state_impl.h"
#include "leaderboard_internal.h"
#include "net_base.h"
#include "net.h"
#include "key_resolver.h"
#include "signer_key_resolver.h"
#include "nfc_tpm_key_resolver.h"
#include "soft_key_resolver.h"
#include "utils/thread_priority.h"
#include <logger/logger.h>
#include <blockingconcurrentqueue.h>
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <exception>
#include <thread>
#include <utility>
#include <variant>
#include <cstdint>

using namespace scorbit;
using namespace detail;

struct sb_game_state_struct;

namespace scorbit_c_api_queue {

inline std::string copyCStr(const char *p)
{
    return p ? std::string(p) : std::string {};
}

inline HttpHeaders copyHeaders(const sb_http_header_t *headers, size_t count)
{
    HttpHeaders result;
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
    HttpHeaders headers;
    sb_string_callback_t callback;
    void *user_data;
};

struct JobDownloadBuffer {
    sb_game_state_struct *h;
    std::string url;
    size_t reserve_buffer_size;
    HttpHeaders headers;
    sb_buffer_callback_t callback;
    void *user_data;
};

struct JobUploadDiagnostics {
    sb_game_state_struct *h;
    std::vector<std::string> logPaths;
    std::vector<std::string> recordingPaths;
    std::string logString;
};

using ApiQueueItem =
        std::variant<Poison, JobSetGameStarted, JobSetGameFinished, JobSetCurrentBall,
                     JobSetActivePlayer, JobSetScore, JobAddMode, JobAddModeExpiring,
                     JobTickModeExpiries, JobRemoveMode, JobClearModes, JobCommit,
                     JobRequestTopScores, JobRequestPairCode, JobRequestUnpair, JobSetCapabilities,
                     JobPairMachine, JobCreditsDropped, JobCreditsStatus, JobDownload,
                     JobDownloadBuffer, JobUploadDiagnostics>;

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
    return [cb, user_data](Error error, const std::string &reply) {
        if (cb) {
            cb(static_cast<sb_error_t>(error), reply.c_str(), user_data);
        }
    };
}

inline auto makeBufferReplyBridge(sb_buffer_callback_t cb, void *user_data)
{
    return [cb, user_data](Error error, const std::vector<uint8_t> &data) {
        if (cb) {
            cb(static_cast<sb_error_t>(error), data.data(), data.size(), user_data);
        }
    };
}

inline auto makeLeaderboardReplyBridge(sb_leaderboard_callback_t cb, void *user_data)
{
    return [cb, user_data](Error error, sb_leaderboard_t *leaderboard) {
        if (cb) {
            cb(static_cast<sb_error_t>(error), leaderboard, user_data);
        }
        if (leaderboard) {
            detail::destroyLeaderboard(leaderboard);
        }
    };
}

} // namespace scorbit_c_api_queue

struct sb_game_state_struct {
    detail::GameStateImpl gameState;
    moodycamel::BlockingConcurrentQueue<scorbit_c_api_queue::ApiQueueItem> cApiQueue;
    std::atomic<bool> cApiAccepting {true};
    std::thread cApiDispatcher;

    explicit sb_game_state_struct(std::unique_ptr<NetBase> net);
    ~sb_game_state_struct();

    void postApiJob(scorbit_c_api_queue::ApiQueueItem &&job);
    void shutdownCApiDispatcher();

private:
    void cApiDispatcherLoop();
};

namespace scorbit_c_api_queue {

void dispatchApiJob(ApiQueueItem &&item)
{
    std::visit(
            Overloaded {
                    [](Poison) {},
                    [](JobSetGameStarted &&j) {
                        j.h->gameState.setGameStarted(static_cast<GameStartOrigin>(j.origin));
                    },
                    [](JobSetGameFinished &&j) { j.h->gameState.setGameFinished(); },
                    [](JobSetCurrentBall &&j) { j.h->gameState.setCurrentBall(j.ball); },
                    [](JobSetActivePlayer &&j) { j.h->gameState.setActivePlayer(j.player); },
                    [](JobSetScore &&j) { j.h->gameState.setScore(j.player, j.score, j.feature); },
                    [](JobAddMode &&j) { j.h->gameState.addMode(std::move(j.mode)); },
                    [](JobAddModeExpiring &&j) {
                        j.h->gameState.addModeExpiring(std::move(j.mode), j.duration_seconds);
                    },
                    [](JobTickModeExpiries &&j) { j.h->gameState.tickModeExpiries(); },
                    [](JobRemoveMode &&j) { j.h->gameState.removeMode(j.mode); },
                    [](JobClearModes &&j) { j.h->gameState.clearModes(); },
                    [](JobCommit &&j) { j.h->gameState.commit(); },
                    [](JobRequestTopScores &&j) {
                        j.h->gameState.requestTopScores(
                                static_cast<LeaderboardScope>(j.scope),
                                static_cast<LeaderboardPeriod>(j.period), j.since,
                                static_cast<LeaderboardVpinFilter>(j.vpin_filter),
                                makeLeaderboardReplyBridge(j.callback, j.user_data));
                    },
                    [](JobRequestPairCode &&j) {
                        j.h->gameState.requestPairCode(
                                makeCStringReplyBridge(j.callback, j.user_data));
                    },
                    [](JobRequestUnpair &&j) {
                        j.h->gameState.requestUnpair(
                                makeCStringReplyBridge(j.callback, j.user_data));
                    },
                    [](JobSetCapabilities &&j) { j.h->gameState.setCapabilities(j.capabilities); },
                    [](JobPairMachine &&j) {
                        j.h->gameState.requestPairMachine(
                                std::move(j.machine_uuid), std::move(j.owner_uuid),
                                makeCStringReplyBridge(j.callback, j.user_data));
                    },
                    [](JobCreditsDropped &&j) {
                        j.h->gameState.setCreditsDropped(j.credits, j.transaction, j.success);
                    },
                    [](JobCreditsStatus &&j) {
                        j.h->gameState.setCreditsStatus(j.free_play, j.credits, j.max_credits,
                                                        j.pricing.c_str());
                    },
                    [](JobDownload &&j) {
                        j.h->gameState.download(makeCStringReplyBridge(j.callback, j.user_data),
                                                std::move(j.url), std::move(j.filename),
                                                std::move(j.headers));
                    },
                    [](JobDownloadBuffer &&j) {
                        j.h->gameState.downloadBuffer(
                                makeBufferReplyBridge(j.callback, j.user_data), std::move(j.url),
                                j.reserve_buffer_size, std::move(j.headers));
                    },
                    [](JobUploadDiagnostics &&j) {
                        j.h->gameState.uploadDiagnostics(std::move(j.logPaths),
                                                         std::move(j.recordingPaths),
                                                         std::move(j.logString));
                    },
            },
            std::move(item));
}

} // namespace scorbit_c_api_queue

using namespace scorbit_c_api_queue;

sb_game_state_struct::sb_game_state_struct(std::unique_ptr<NetBase> net)
    : gameState(std::move(net))
    , cApiDispatcher([this] { cApiDispatcherLoop(); })
{
    gameState.setModeExpiryPoster([this]() {
        if (!cApiAccepting.load(std::memory_order_relaxed)) {
            return;
        }
        cApiQueue.enqueue(ApiQueueItem {JobTickModeExpiries {this}});
    });
}

sb_game_state_struct::~sb_game_state_struct()
{
    shutdownCApiDispatcher();
}

void sb_game_state_struct::cApiDispatcherLoop()
{
    scorbit::detail::applySdkThreadNice(gameState.configuredSdkThreadsNice());

    for (;;) {
        ApiQueueItem item;
        cApiQueue.wait_dequeue(item);
        if (std::holds_alternative<Poison>(item)) {
            break;
        }
        try {
            dispatchApiJob(std::move(item));
        } catch (const std::exception &e) {
            ERR("C API dispatcher task failed: {}", e.what());
        } catch (...) {
            ERR("C API dispatcher task failed: unknown exception");
        }
    }
}

void sb_game_state_struct::postApiJob(scorbit_c_api_queue::ApiQueueItem &&job)
{
    cApiQueue.enqueue(std::move(job));
}

void sb_game_state_struct::shutdownCApiDispatcher()
{
    if (!cApiDispatcher.joinable()) {
        return;
    }
    cApiAccepting.store(false, std::memory_order_relaxed);
    cApiQueue.enqueue(ApiQueueItem {Poison {}});
    cApiDispatcher.join();
}

sb_game_handle_t sb_create_game_state(sb_config_t config)
{
    if (!config) {
        return nullptr;
    }

    if (!config->hasAuthentication()) {
        return nullptr;
    }

    std::vector<std::unique_ptr<IKeyResolver>> resolvers;

    if (config->hasAuthenticationCallback()) {
        auto signer = config->signerCallback;
        auto userData = config->signerUserData;

        SignerCallback cb = [signer, userData](const Digest &digest) {
            Signature signature(SIGNATURE_MAX_LENGTH);
            size_t signatureLength = 0;
            if (0 == signer(signature.data(), &signatureLength, digest.data(), userData)) {
                signature.resize(signatureLength);
            } else {
                signature.clear();
            }
            return signature;
        };

        resolvers.push_back(std::make_unique<SignerKeyResolver>(std::move(cb)));
    } else {
        resolvers.push_back(std::make_unique<NfcTpmKeyResolver>());
        resolvers.push_back(std::make_unique<SoftKeyResolver>());
    }

    auto net = std::make_unique<Net>(*config, std::move(resolvers));
    return new sb_game_state_struct {std::move(net)};
}

void sb_destroy_game_state(sb_game_handle_t handle)
{
    handle->shutdownCApiDispatcher();
    delete handle;
}

void sb_set_game_started(sb_game_handle_t handle, sb_game_start_origin_t origin)
{
    handle->postApiJob(JobSetGameStarted {handle, origin});
}

void sb_set_game_finished(sb_game_handle_t handle)
{
    handle->postApiJob(JobSetGameFinished {handle});
}

void sb_set_current_ball(sb_game_handle_t handle, sb_ball_t ball)
{
    handle->postApiJob(JobSetCurrentBall {handle, ball});
}

void sb_set_active_player(sb_game_handle_t handle, sb_player_t player)
{
    handle->postApiJob(JobSetActivePlayer {handle, player});
}

void sb_set_score(sb_game_handle_t handle, sb_player_t player, sb_score_t score,
                  sb_score_feature_t feature)
{
    handle->postApiJob(JobSetScore {handle, player, score, feature});
}

void sb_add_mode(sb_game_handle_t handle, const char *mode)
{
    handle->postApiJob(JobAddMode {handle, copyCStr(mode)});
}

void sb_add_mode_expiring(sb_game_handle_t handle, const char *mode, uint32_t duration_seconds)
{
    handle->postApiJob(JobAddModeExpiring {handle, copyCStr(mode), duration_seconds});
}

void sb_remove_mode(sb_game_handle_t handle, const char *mode)
{
    handle->postApiJob(JobRemoveMode {handle, copyCStr(mode)});
}

void sb_clear_modes(sb_game_handle_t handle)
{
    handle->postApiJob(JobClearModes {handle});
}

void sb_commit(sb_game_handle_t handle)
{
    handle->postApiJob(JobCommit {handle});
}

const char *sb_get_machine_uuid(sb_game_handle_t handle)
{
    return handle->gameState.getMachineUuid().c_str();
}

uint64_t sb_get_machine_serial(sb_game_handle_t handle)
{
    return handle->gameState.getMachineSerial();
}

const char *sb_get_pair_deeplink(sb_game_handle_t handle)
{
    return handle->gameState.getPairDeeplink().c_str();
}

void sb_request_top_scores(sb_game_handle_t handle, sb_leaderboard_scope_t scope,
                           sb_leaderboard_period_t period, const char *since,
                           sb_leaderboard_vpin_filter_t vpin_filter,
                           sb_leaderboard_callback_t callback, void *user_data)
{
    handle->postApiJob(JobRequestTopScores {handle, scope, period,
                                            scorbit_c_api_queue::copyCStr(since), vpin_filter,
                                            callback, user_data});
}

void sb_request_pair_code(sb_game_handle_t handle, sb_string_callback_t callback, void *user_data)
{
    handle->postApiJob(JobRequestPairCode {handle, callback, user_data});
}

sb_auth_status_t sb_get_status(sb_game_handle_t handle)
{
    return static_cast<sb_auth_status_t>(handle->gameState.getStatus());
}

void sb_request_unpair(sb_game_handle_t handle, sb_string_callback_t callback, void *user_data)
{
    handle->postApiJob(JobRequestUnpair {handle, callback, user_data});
}

void sb_set_capabilities(sb_game_handle_t handle, sb_capabilities_t capabilities)
{
    handle->postApiJob(JobSetCapabilities {handle, capabilities});
}

void sb_game_request_pair_machine(sb_game_handle_t handle, const char *machine_uuid,
                                  const char *owner_uuid, sb_string_callback_t callback,
                                  void *user_data)
{
    handle->postApiJob(JobPairMachine {handle, copyCStr(machine_uuid), copyCStr(owner_uuid),
                                       callback, user_data});
}

void sb_set_credits_dropped(sb_game_handle_t handle, int credits, const char *transaction,
                            bool success)
{
    handle->postApiJob(JobCreditsDropped {handle, credits, copyCStr(transaction), success});
}

void sb_set_credits_status(sb_game_handle_t handle, bool free_play, int credits, int max_credits,
                           const char *pricing)
{
    handle->postApiJob(
            JobCreditsStatus {handle, free_play, credits, max_credits, copyCStr(pricing)});
}

void sb_download(sb_game_handle_t handle, const char *url, const char *filename,
                 const sb_http_header_t *headers, size_t headers_count,
                 sb_string_callback_t callback, void *user_data)
{
    handle->postApiJob(JobDownload {handle, copyCStr(url), copyCStr(filename),
                                    copyHeaders(headers, headers_count), callback, user_data});
}

void sb_download_buffer(sb_game_handle_t handle, const char *url, size_t reserve_buffer_size,
                        const sb_http_header_t *headers, size_t headers_count,
                        sb_buffer_callback_t callback, void *user_data)
{
    handle->postApiJob(JobDownloadBuffer {handle, copyCStr(url), reserve_buffer_size,
                                          copyHeaders(headers, headers_count), callback,
                                          user_data});
}

void sb_upload_diagnostics(sb_game_handle_t handle, const char **log_paths, size_t log_count,
                           const char **recording_paths, size_t recording_count,
                           const char *log_string)
{
    std::vector<std::string> logs;
    logs.reserve(log_count);
    for (size_t i = 0; i < log_count; ++i) {
        if (log_paths && log_paths[i]) {
            logs.emplace_back(log_paths[i]);
        }
    }

    std::vector<std::string> recordings;
    recordings.reserve(recording_count);
    for (size_t i = 0; i < recording_count; ++i) {
        if (recording_paths && recording_paths[i]) {
            recordings.emplace_back(recording_paths[i]);
        }
    }

    handle->postApiJob(JobUploadDiagnostics {handle, std::move(logs), std::move(recordings),
                                             copyCStr(log_string)});
}
