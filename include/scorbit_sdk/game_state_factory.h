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

#pragma once

#include "config.h"
#include "game_state.h"
#include "game_state_c.h"

namespace scorbit {

/**
 * @brief Create a new game state using Config object.
 *
 * This is the primary method for creating a game state. The Config object must have:
 * - Required settings: provider, machine_id, game_code_version
 * - Authentication: either setEncryptedKey() or setSigner()
 *
 * @param config The Config object with SDK settings and authentication.
 *
 * @return A new game state object.
 *
 * Example:
 * @code
 * Config config;
 * config.setProvider("myprovider")
 *       .setMachineId(4419)
 *       .setGameCodeVersion("1.0.0")
 *       .setEncryptedKey(encryptedKey);
 *
 * auto gameState = createGameState(config);
 * @endcode
 */
inline GameState createGameState(const Config &config)
{
    return GameState {sb_create_game_state(config.handle())};
}

} // namespace scorbit
