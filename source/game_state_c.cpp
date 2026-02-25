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
#include "net.h"
#include "key_resolver.h"
#include "nfc_tpm_key_resolver.h"
#include "soft_key_resolver.h"
#include <logger/logger.h>
#include <string>
#include <memory>
#include <vector>

using namespace scorbit;
using namespace detail;

struct sb_game_state_struct {
    detail::GameStateImpl gameState;
};

namespace {

GameStateImpl createGameStateImpl(SignerCallback signer, const DeviceInfo &deviceInfo,
                                  bool useEncryptedKey)
{
    auto net = std::make_unique<Net>(std::move(signer), deviceInfo, useEncryptedKey);
    return GameStateImpl(std::move(net));
}

}

sb_game_handle_t sb_create_game_state(sb_config_t config)
{
    if (!config) {
        return nullptr;
    }

    // Path 1: Signer callback (scorbitd)
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

        return new sb_game_state_struct {createGameStateImpl(std::move(cb), *config, false)};
    }

    // Path 2: Key resolver chain
    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    resolvers.push_back(std::make_unique<NfcTpmKeyResolver>());
    resolvers.push_back(std::make_unique<SoftKeyResolver>());

    for (const auto &resolver : resolvers) {
        if (resolver->tryResolve(*config)) {
            return new sb_game_state_struct {
                    createGameStateImpl(resolver->createSigner(), *config, true)};
        }
    }

    ERR("No authentication resolver succeeded");
    return nullptr;
}

void sb_destroy_game_state(sb_game_handle_t handle)
{
    delete handle;
}

void sb_set_game_started(sb_game_handle_t handle, sb_game_start_origin_t origin)
{
    handle->gameState.setGameStarted(static_cast<GameStartOrigin>(origin));
}

void sb_set_game_finished(sb_game_handle_t handle)
{
    handle->gameState.setGameFinished();
}

void sb_set_current_ball(sb_game_handle_t handle, sb_ball_t ball)
{
    handle->gameState.setCurrentBall(ball);
}

void sb_set_active_player(sb_game_handle_t handle, sb_player_t player)
{
    handle->gameState.setActivePlayer(player);
}

void sb_set_score(sb_game_handle_t handle, sb_player_t player, sb_score_t score,
                  sb_score_feature_t feature)
{
    handle->gameState.setScore(player, score, feature);
}

void sb_add_mode(sb_game_handle_t handle, const char *mode)
{
    handle->gameState.addMode(mode);
}

void sb_remove_mode(sb_game_handle_t handle, const char *mode)
{
    handle->gameState.removeMode(mode);
}

void sb_clear_modes(sb_game_handle_t handle)
{
    handle->gameState.clearModes();
}

void sb_commit(sb_game_handle_t handle)
{
    handle->gameState.commit();
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
    handle->gameState.requestTopScores(
            score_filter, [callback, user_data](Error error, const std::string &reply) {
                if (callback) {
                    callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                }
            });
}

void sb_request_pair_code(sb_game_handle_t handle, sb_string_callback_t callback, void *user_data)
{
    handle->gameState.requestPairCode([callback, user_data](Error error, const std::string &reply) {
        if (callback) {
            callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
        }
    });
}

sb_auth_status_t sb_get_status(sb_game_handle_t handle)
{
    return static_cast<sb_auth_status_t>(handle->gameState.getStatus());
}

void sb_request_unpair(sb_game_handle_t handle, sb_string_callback_t callback, void *user_data)
{
    handle->gameState.requestUnpair([callback, user_data](Error error, const std::string &reply) {
        if (callback) {
            callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
        }
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
    handle->gameState.setCapabilities(capabilities);
}

void sb_game_request_pair_machine(sb_game_handle_t handle, const char *machine_uuid,
                                  const char *owner_uuid, sb_string_callback_t callback,
                                  void *user_data)
{
    handle->gameState.requestPairMachine(
            machine_uuid, owner_uuid, [callback, user_data](Error error, const std::string &reply) {
                if (callback) {
                    callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                }
            });
}

void sb_set_credits_dropped(sb_game_handle_t handle, int credits, const char *transaction,
                            bool success)
{
    handle->gameState.setCreditsDropped(credits, transaction, success);
}

void sb_set_credits_status(sb_game_handle_t handle, bool free_play, int credits, int max_credits,
                           const char *pricing)
{
    handle->gameState.setCreditsStatus(free_play, credits, max_credits, pricing);
}

void sb_download(sb_game_handle_t handle, const char *url, const char *filename,
                 const char *content_type, sb_string_callback_t callback, void *user_data)
{
    handle->gameState.download(
            [callback, user_data](Error error, const std::string &reply) {
                if (callback) {
                    callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                }
            },
            url, filename, content_type ? content_type : std::string {});
}

void sb_download_buffer(sb_game_handle_t handle, const char *url, size_t reserve_buffer_size,
                        const char *content_type, sb_buffer_callback_t callback, void *user_data)
{
    handle->gameState.downloadBuffer(
            [callback, user_data](Error error, const std::vector<uint8_t> &data) {
                if (callback) {
                    callback(static_cast<sb_error_t>(error), data.data(), data.size(), user_data);
                }
            },
            url, reserve_buffer_size, content_type ? content_type : std::string {});
}
