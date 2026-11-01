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

#include <scorbit_sdk/scorbit_sdk_c.h>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace scorbit_godot {

/// What the SDK's threads reach through their callbacks' user data. It outlives the game state,
/// which is destroyed before this is.
struct Bridge {
    std::mutex mutex;
    std::vector<godot::Dictionary> events; // drained on the main thread
    std::string keyPath;                   // where the device key is kept between runs
};

/// The SDK's game state as a Godot node. SDK events reach GDScript as signals, emitted from
/// _process on the main thread: the SDK raises them on its own threads, and the scene tree is not
/// thread-safe.
class ScorbitGameState : public godot::Node
{
    GDCLASS(ScorbitGameState, godot::Node)

public:
    ScorbitGameState();
    ~ScorbitGameState() override;

    void _process(double delta) override;

    bool start(const godot::Dictionary &config);
    void stop();
    bool is_started() const;

    void set_game_started(int origin);
    void set_game_finished();
    void set_current_ball(int ball);
    void set_active_player(int player);
    void set_score(int player, int64_t score, int feature);
    void add_mode(const godot::String &mode);
    void add_mode_expiring(const godot::String &mode, int seconds);
    void remove_mode(const godot::String &mode);
    void clear_modes();
    void set_mode_completed(const godot::String &mode);
    void commit();

    int get_status() const;
    godot::String get_machine_uuid() const;
    godot::String get_pair_deeplink() const;

protected:
    static void _bind_methods();

private:
    void emitEvent(const godot::Dictionary &event);

    sb_game_handle_t m_handle {nullptr};
    std::shared_ptr<Bridge> m_bridge;
};

} // namespace scorbit_godot
