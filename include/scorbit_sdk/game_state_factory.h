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
