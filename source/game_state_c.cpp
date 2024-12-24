/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/game_state_c.h>
#include <scorbit_sdk/game_state.h>
#include "scorbit_sdk/net_types.h"
#include "scorbit_sdk/net_types_c.h"
#include "scorbit_sdk/game_state_factory.h"
#include <string>
#include <map>

using namespace scorbit;

struct sb_game_state_struct {
    GameState gameState;

    std::string machineUuid;
    std::string pairDeeplink;
    std::map<int, std::string> claimDeeplinks;
};

sb_game_handle_t sb_create_game_state(sb_signer_callback_t signer, void *signer_user_data,
                                      const sb_device_info_t *device_info)
{
    SignerCallback cb = [signer, signer_user_data](Signature &signature, size_t &signatureLen,
                                                   const Digest &digest) {
        return 0 == signer(signature.data(), &signatureLen, digest.data(), signer_user_data);
    };

    DeviceInfo deviceInfo;

    if (device_info) {
        deviceInfo.provider = device_info->provider;
        deviceInfo.machineId = device_info->machine_id;
        if (device_info->game_code_version) {
            deviceInfo.gameCodeVersion = device_info->game_code_version;
        }
        if (device_info->hostname) {
            deviceInfo.hostname = device_info->hostname;
        }
        if (device_info->uuid) {
            deviceInfo.uuid = device_info->uuid;
        }
        deviceInfo.serialNumber = device_info->serial_number;
    }

    return new sb_game_state_struct {
            createGameState(std::move(cb), std::move(deviceInfo)), {}, {}, {}};
}

void sb_destroy_game_state(sb_game_handle_t handle)
{
    delete handle;
}

void sb_set_game_started(sb_game_handle_t handle)
{
    handle->gameState.setGameStarted();
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
    handle->machineUuid = handle->gameState.getMachineUuid();
    return handle->machineUuid.c_str();
}

const char *sb_get_pair_deeplink(sb_game_handle_t handle)
{
    handle->pairDeeplink = handle->gameState.getPairDeeplink();
    return handle->pairDeeplink.c_str();
}

const char *sb_get_claim_deeplink(sb_game_handle_t handle, int player)
{
    handle->claimDeeplinks[player] = handle->gameState.getClaimDeeplink(player);
    return handle->claimDeeplinks[player].c_str();
}

void sb_request_top_scores(sb_game_handle_t handle, sb_score_t score_filter,
                           sb_string_callback_t callback, void *user_data)
{
    handle->gameState.requestTopScores(
            score_filter, [callback, user_data](Error error, const std::string &reply) {
                return callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
            });
}
