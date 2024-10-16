/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "game_state.h"
#include "net_types.h"

namespace scorbit {

class GameState;

/**
 * @brief Create a new game state.
 *
 * Initializes and returns a new game state object. This object can be used to modify various
 * aspects of the game state, including the active player, scores, and active modes.
 *
 * @param signer A callback function that will be used to authenticate to the cloud. It should match
 * the signature specified by @ref SignerCallback.
 *
 * @param deviceInfo The device information of type @ref DeviceInfo used to authenticate to the
 * cloud.
 *
 * @see examples/cpp_example/main.cpp for an example implementation.
 *
 * @return A new game state object.
 */
SCORBIT_SDK_EXPORT
GameState createGameState(scorbit::SignerCallback signer, const DeviceInfo &deviceInfo);

} // namespace scorbit
