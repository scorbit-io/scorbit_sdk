/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "game_state.h"
#include "net_types.h"
#include <string>

namespace scorbit {

class GameState;

/**
 * @brief Create a new game state.
 *
 * Initializes and returns a new game state object. This object can be used to modify various
 * aspects of the game state, including the active player, scores, and active modes.
 *
 * @param signer A callback function that will be used to authenticate to the cloud. It should match
 * the signature specified by @ref SignerCallback. See @see examples/cpp_example/main.cpp for an
 * example implementation.
 *
 * @param hostname The server hostname to connect to. If not provided, the default "production"
 * hostname is used. The two standard options are "production" and "staging", each corresponding
 * to pre-defined URLs. To use a custom URL, provide it in the format "http[s]://<url>[:port]".
 * Examples: https://api.scorbit.io, https://staging.scorbit.io, http://localhost:8080
 *
 * @return A new game state object.
 */
SCORBIT_SDK_EXPORT
GameState createGameState(scorbit::SignerCallback signer,
                          const std::string &hostname = std::string {"production"});

} // namespace scorbit
