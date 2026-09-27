/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include <scorbit_sdk/common_types_c.h>
#include <functional>

namespace scorbit {
namespace detail {

class GameStateImpl;

/**
 * Access to a C API game handle for the other C API translation units. Game state lives on the
 * handle's dispatcher thread: only read-only, thread-safe members may be used directly, anything
 * else goes through @ref postToGameState.
 */
GameStateImpl &gameStateOf(sb_game_handle_t handle);

/** Runs @p fn on the handle's dispatcher thread, after the calls queued before it. */
void postToGameState(sb_game_handle_t handle, std::function<void()> fn);

} // namespace detail
} // namespace scorbit
