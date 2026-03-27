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
#include <scorbit_sdk/net_types.h>
#include <scorbit_sdk/net_types_c.h>
#include <scorbit_sdk/game_state_factory.h>
#include "device_info.h"
#include "game_state_impl.h"
#include "net_base.h"
#include "net.h"
#include "key_resolver.h"
#include "signer_key_resolver.h"
#include "nfc_tpm_key_resolver.h"
#include "soft_key_resolver.h"
#include <logger/logger.h>
#include <blockingconcurrentqueue.h>
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <exception>
#include <functional>
#include <thread>
#include <utility>

using namespace scorbit;
using namespace detail;

namespace {

inline std::string copyCStr(const char *p)
{
    return p ? std::string(p) : std::string {};
}

} // namespace

struct sb_game_state_struct {
    detail::GameStateImpl gameState;
    moodycamel::BlockingConcurrentQueue<std::function<void()>> cApiQueue;
    std::atomic<bool> cApiAccepting {true};
    std::thread cApiDispatcher;

    explicit sb_game_state_struct(std::unique_ptr<NetBase> net);
    ~sb_game_state_struct();

    void postCApiTask(std::function<void()> &&f);
    void shutdownCApiDispatcher();

private:
    void cApiDispatcherLoop();
};

sb_game_state_struct::sb_game_state_struct(std::unique_ptr<NetBase> net)
    : gameState(std::move(net))
    , cApiDispatcher([this] { cApiDispatcherLoop(); })
{
}

sb_game_state_struct::~sb_game_state_struct()
{
    shutdownCApiDispatcher();
}

void sb_game_state_struct::cApiDispatcherLoop()
{
    for (;;) {
        std::function<void()> task;
        cApiQueue.wait_dequeue(task);
        if (!task) {
            break;
        }
        try {
            task();
        } catch (const std::exception &e) {
            ERR("C API dispatcher task failed: {}", e.what());
        } catch (...) {
            ERR("C API dispatcher task failed: unknown exception");
        }
    }
}

void sb_game_state_struct::postCApiTask(std::function<void()> &&f)
{
    // std::lock_guard lock(cApiEnqueueMutex);
    // if (!cApiAccepting.load(std::memory_order_relaxed)) {
    //     return;
    // }
    cApiQueue.enqueue(std::move(f));
}

void sb_game_state_struct::shutdownCApiDispatcher()
{
    {
        if (!cApiDispatcher.joinable()) {
            return;
        }
        cApiAccepting.store(false, std::memory_order_relaxed);
        std::function<void()> poison;
        cApiQueue.enqueue(std::move(poison));
    }
    cApiDispatcher.join();
}

sb_game_handle_t sb_create_game_state(sb_config_t config)
{
    if (!config) {
        return nullptr;
    }

    std::vector<std::unique_ptr<IKeyResolver>> resolvers;

    if (config->hasAuthenticationCallback()) {
        // 1. Signer callback given, use it exclusively
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
        // 2. Without signer callback use following resolvers
        resolvers.push_back(std::make_unique<NfcTpmKeyResolver>());
        resolvers.push_back(std::make_unique<SoftKeyResolver>());
    }

    auto net = std::make_unique<Net>(*config, std::move(resolvers));
    return new sb_game_state_struct {std::move(net)};
}

void sb_destroy_game_state(sb_game_handle_t handle)
{
    handle->shutdownCApiDispatcher();
    handle->gameState.prepareForDestroy();
    delete handle;
}

void sb_set_game_started(sb_game_handle_t handle, sb_game_start_origin_t origin)
{
    handle->postCApiTask([handle, origin] {
        handle->gameState.setGameStarted(static_cast<GameStartOrigin>(origin));
    });
}

void sb_set_game_finished(sb_game_handle_t handle)
{
    handle->postCApiTask([handle] { handle->gameState.setGameFinished(); });
}

void sb_set_current_ball(sb_game_handle_t handle, sb_ball_t ball)
{
    handle->postCApiTask([handle, ball] { handle->gameState.setCurrentBall(ball); });
}

void sb_set_active_player(sb_game_handle_t handle, sb_player_t player)
{
    handle->postCApiTask([handle, player] { handle->gameState.setActivePlayer(player); });
}

void sb_set_score(sb_game_handle_t handle, sb_player_t player, sb_score_t score,
                  sb_score_feature_t feature)
{
    handle->postCApiTask([handle, player, score, feature] {
        handle->gameState.setScore(player, score, feature);
    });
}

void sb_add_mode(sb_game_handle_t handle, const char *mode)
{
    const std::string modeCopy = copyCStr(mode);
    handle->postCApiTask([handle, modeCopy] { handle->gameState.addMode(modeCopy); });
}

void sb_remove_mode(sb_game_handle_t handle, const char *mode)
{
    const std::string modeCopy = copyCStr(mode);
    handle->postCApiTask([handle, modeCopy] { handle->gameState.removeMode(modeCopy); });
}

void sb_clear_modes(sb_game_handle_t handle)
{
    handle->postCApiTask([handle] { handle->gameState.clearModes(); });
}

void sb_commit(sb_game_handle_t handle)
{
    handle->postCApiTask([handle] { handle->gameState.commit(); });
}

const char *sb_get_machine_uuid(sb_game_handle_t handle)
{
    return handle->gameState.getMachineUuid().c_str();
}

const char *sb_get_pair_deeplink(sb_game_handle_t handle)
{
    return handle->gameState.getPairDeeplink().c_str();
}

const char *sb_get_claim_deeplink(sb_game_handle_t handle, int player)
{
    return handle->gameState.getClaimDeeplink(player).c_str();
}

void sb_request_top_scores(sb_game_handle_t handle, sb_score_t score_filter,
                           sb_string_callback_t callback, void *user_data)
{
    handle->postCApiTask([handle, score_filter, callback, user_data] {
        handle->gameState.requestTopScores(
                score_filter, [callback, user_data](Error error, const std::string &reply) {
                    if (callback) {
                        callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                    }
                });
    });
}

void sb_request_pair_code(sb_game_handle_t handle, sb_string_callback_t callback, void *user_data)
{
    handle->postCApiTask([handle, callback, user_data] {
        handle->gameState.requestPairCode(
                [callback, user_data](Error error, const std::string &reply) {
                    if (callback) {
                        callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                    }
                });
    });
}

sb_auth_status_t sb_get_status(sb_game_handle_t handle)
{
    return static_cast<sb_auth_status_t>(handle->gameState.getStatus());
}

void sb_request_unpair(sb_game_handle_t handle, sb_string_callback_t callback, void *user_data)
{
    handle->postCApiTask([handle, callback, user_data] {
        handle->gameState.requestUnpair(
                [callback, user_data](Error error, const std::string &reply) {
                    if (callback) {
                        callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                    }
                });
    });
}

bool sb_is_players_info_updated(sb_game_handle_t handle)
{
    return handle->gameState.isPlayersInfoUpdated();
}

bool sb_has_player_info(sb_game_handle_t handle, sb_player_t player)
{
    return handle->gameState.getPlayerProfile(player).has_value();
}

int64_t sb_get_player_id(sb_game_handle_t /*handle*/, sb_player_t /*player*/)
{
    // FIXME: this should be string
    // if (auto profile = handle->gameState.getPlayerProfile(player)) {
    //     return static_cast<int>(profile->id);
    // }
    return -1; // Invalid player ID
}

const char *sb_get_player_preferred_name(sb_game_handle_t handle, sb_player_t player)
{
    if (auto profile = handle->gameState.getPlayerProfile(player)) {
        return profile->preferInitials ? profile->initials.c_str() : profile->name.c_str();
    }
    return nullptr;
}

const char *sb_get_player_name(sb_game_handle_t handle, sb_player_t player)
{
    if (auto profile = handle->gameState.getPlayerProfile(player)) {
        return profile->name.c_str();
    }
    return nullptr;
}

const char *sb_get_player_initials(sb_game_handle_t handle, sb_player_t player)
{
    if (auto profile = handle->gameState.getPlayerProfile(player)) {
        return profile->initials.c_str();
    }
    return nullptr;
}

const char *sb_get_player_picture_url(sb_game_handle_t handle, sb_player_t player)
{
    if (auto profile = handle->gameState.getPlayerProfile(player)) {
        return profile->pictureUrl.c_str();
    }
    return nullptr;
}

const uint8_t *sb_get_player_picture(sb_game_handle_t handle, sb_player_t player, size_t *size)
{
    const auto &picture = handle->gameState.getPlayerPicture(player);
    if (!picture.empty()) {
        *size = picture.size();
        return picture.data();
    }
    *size = 0;
    return nullptr;
}

void sb_set_capabilities(sb_game_handle_t handle, sb_capabilities_t capabilities)
{
    handle->postCApiTask(
            [handle, capabilities] { handle->gameState.setCapabilities(capabilities); });
}

void sb_game_request_pair_machine(sb_game_handle_t handle, const char *machine_uuid,
                                  const char *owner_uuid, sb_string_callback_t callback,
                                  void *user_data)
{
    const std::string machineUuid = copyCStr(machine_uuid);
    const std::string ownerUuid = copyCStr(owner_uuid);
    handle->postCApiTask([handle, machineUuid, ownerUuid, callback, user_data] {
        handle->gameState.requestPairMachine(
                machineUuid, ownerUuid,
                [callback, user_data](Error error, const std::string &reply) {
                    if (callback) {
                        callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                    }
                });
    });
}

void sb_set_credits_dropped(sb_game_handle_t handle, int credits, const char *transaction,
                            bool success)
{
    const std::string transactionCopy = copyCStr(transaction);
    handle->postCApiTask([handle, credits, transactionCopy, success] {
        handle->gameState.setCreditsDropped(credits, transactionCopy, success);
    });
}

void sb_set_credits_status(sb_game_handle_t handle, bool free_play, int credits, int max_credits,
                           const char *pricing)
{
    const std::string pricingCopy = copyCStr(pricing);
    handle->postCApiTask([handle, free_play, credits, max_credits, pricingCopy] {
        handle->gameState.setCreditsStatus(free_play, credits, max_credits, pricingCopy.c_str());
    });
}

void sb_download(sb_game_handle_t handle, const char *url, const char *filename,
                 const char *content_type, sb_string_callback_t callback, void *user_data)
{
    const std::string urlCopy = copyCStr(url);
    const std::string filenameCopy = copyCStr(filename);
    const std::string contentTypeCopy = copyCStr(content_type);
    handle->postCApiTask([handle, urlCopy, filenameCopy, contentTypeCopy, callback, user_data] {
        handle->gameState.download(
                [callback, user_data](Error error, const std::string &reply) {
                    if (callback) {
                        callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                    }
                },
                urlCopy, filenameCopy, contentTypeCopy);
    });
}

void sb_download_buffer(sb_game_handle_t handle, const char *url, size_t reserve_buffer_size,
                        const char *content_type, sb_buffer_callback_t callback, void *user_data)
{
    const std::string urlCopy = copyCStr(url);
    const std::string contentTypeCopy = copyCStr(content_type);
    handle->postCApiTask(
            [handle, urlCopy, reserve_buffer_size, contentTypeCopy, callback, user_data] {
                handle->gameState.downloadBuffer(
                        [callback, user_data](Error error, const std::vector<uint8_t> &data) {
                            if (callback) {
                                callback(static_cast<sb_error_t>(error), data.data(), data.size(),
                                         user_data);
                            }
                        },
                        urlCopy, reserve_buffer_size, contentTypeCopy);
            });
}
