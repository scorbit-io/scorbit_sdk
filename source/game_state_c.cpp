/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/game_state_c.h>

class GameState
{
};

sb_game_handle_t sb_create_game_state()
{
    return reinterpret_cast<sb_game_handle_t>(new GameState());
}

void sb_destroy_game_state(sb_game_handle_t handle)
{
    delete reinterpret_cast<GameState *>(handle);
}

sb_status_t sb_set_game_started(sb_game_handle_t handle)
{
    (void)handle;
    return SB_SUCCESS;
}

sb_status_t sb_set_game_finished(sb_game_handle_t handle)
{
    (void)handle;
    return SB_SUCCESS;
}

sb_status_t sb_set_active_player(sb_game_handle_t handle, sb_player_num_t player)
{
    (void)handle;
    (void)player;
    return SB_SUCCESS;
}

sb_status_t sb_set_score(sb_game_handle_t handle, sb_player_num_t player, sb_score_t score)
{
    (void)handle;
    (void)player;
    (void)score;
    return SB_SUCCESS;
}

sb_status_t sb_add_mode(sb_game_handle_t handle, const char *mode)
{
    (void)handle;
    (void)mode;
    return SB_SUCCESS;
}

sb_status_t sb_remove_mode(sb_game_handle_t handle, const char *mode)
{
    (void)handle;
    (void)mode;
    return SB_SUCCESS;
}

sb_status_t sb_clear_all_modes(sb_game_handle_t handle)
{
    (void)handle;
    return SB_SUCCESS;
}
