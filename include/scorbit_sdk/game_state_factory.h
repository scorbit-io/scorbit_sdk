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
 * Creates a new game state object. The game state object can be used to modify the game state
 * such as the active player, scores, and active modes.
 *
 * @param signer A callback function that will be used to sign the game state. The function should
 * have the signature specified by @ref SignerCallback. @see examples/cpp_example/main.cpp
 *
 * @return A new game state object.
 */
SCORBIT_SDK_EXPORT
GameState createGameState(scorbit::SignerCallback signer);

} // namespace scorbit
