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

using namespace scorbit;

struct sb_game_state_struct {
    GameState gameState;
};

sb_game_handle_t sb_create_game_state(sb_signer_callback_t signer, void *signer_user_data,
                                      const sb_device_info_t *device_info)
{
    SignerCallback cb = [signer, signer_user_data](Signature &signature, size_t &signatureLen,
                                                   const Digest &digest) {
        return 0 == signer(signature.data(), &signatureLen, digest.data(), signer_user_data);
    };

    DeviceInfo deviceInfo {device_info->provider, device_info->hostname, device_info->uuid,
                           device_info->serial_number};

    return new sb_game_state_struct {createGameState(std::move(cb), std::move(deviceInfo))};
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

void sb_set_score(sb_game_handle_t handle, sb_player_t player, sb_score_t score)
{
    handle->gameState.setScore(player, score);
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
