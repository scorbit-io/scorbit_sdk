/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "game_state.h"
#include "game_state_c.h"
#include "net_types.h"

namespace scorbit {

/**
 * @brief Create a new game state.
 *
 * Initializes and returns a new game state object. This object can be used to modify various
 * aspects of the game state, including the active player, scores, and active modes.
 *
 * @param signer A callback function that will be used to authenticate to the cloud. It should match
 * the signature specified by @ref sb_signer_callback_t.
 *
 * @param userData User data that will be passed to the signer callback.
 *
 * @param deviceInfo The device information of type @ref DeviceInfo used to authenticate to the
 * cloud.
 *
 * @see examples/cpp_example/main.cpp for an example implementation.
 *
 * @return A new game state object.
 */
inline GameState createGameState(sb_signer_callback_t signer, void *userData,
                                 const DeviceInfo &deviceInfo)
{
    sb_device_info_t di = deviceInfo;
    di.provider = deviceInfo.provider.c_str();
    return GameState {sb_create_game_state(signer, userData, &di)};
}

inline GameState createGameState(std::string encryptedKey, const DeviceInfo &deviceInfo)
{
    sb_device_info_t di = deviceInfo;
    return GameState {sb_create_game_state2(encryptedKey.c_str(), &di)};
}

} // namespace scorbit
