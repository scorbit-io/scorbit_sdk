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
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace scorbit_godot {

struct Bridge;

/// A request's callback user data: what its answering signal reports.
struct RequestContext {
    Bridge *bridge;
    int64_t id;
    godot::String url;  // downloads
    godot::String path; // download(), as the caller gave it
    godot::String type; // report_device_state()
};

/// What the SDK's threads reach through their callbacks' user data. It outlives the game state,
/// which is destroyed before this is.
struct Bridge {
    std::mutex mutex;
    std::vector<godot::Dictionary> events; // drained on the main thread
    std::string keyPath;                   // where the device key is kept between runs
    // Owned here, not by the callback: the SDK may drop a request without answering it.
    std::map<int64_t, std::unique_ptr<RequestContext>> pending;
};

struct LogSink;

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
    void set_capabilities(int capabilities);
    void set_credits_dropped(int credits, const godot::String &transaction, bool success);
    void set_credits_status(bool freePlay, int credits, int maxCredits,
                            const godot::String &pricing);
    // Each request returns an id that its answering signal carries first; 0 if nothing was sent.
    int64_t request_pair_code();
    int64_t request_unpair();
    int64_t request_pair_machine(const godot::String &machineUuid, const godot::String &ownerUuid);
    int64_t request_top_scores(int scope, int period, const godot::String &since, int vpinFilter);
    int64_t get_machine_serial() const;
    int64_t _pending_request_count() const; // for tests
    int64_t download(const godot::String &url, const godot::String &filename,
                     const godot::Dictionary &headers);
    int64_t download_buffer(const godot::String &url, int reserveSize,
                            const godot::Dictionary &headers);
    int64_t upload_diagnostics(const godot::PackedStringArray &logPaths,
                               const godot::PackedStringArray &recordingPaths,
                               const godot::String &logString, int64_t requestGeneration);
    int64_t report_device_state(const godot::String &type, const godot::String &version,
                                bool installed, const godot::String &log);
    bool logger_callbacks_supported() const;
    void enable_log_messages(int maxLength);
    void disable_log_messages();
    void commit();

    int get_status() const;
    godot::String get_machine_uuid() const;
    godot::String get_pair_deeplink() const;

protected:
    static void _bind_methods();

private:
    struct Upload {
        int64_t id;
        godot::PackedStringArray logPaths;
        godot::PackedStringArray recordingPaths;
        godot::String logString;
        int64_t requestGeneration;
    };

    void emitEvent(const godot::Dictionary &event);
    void sendUpload(const Upload &upload);
    void updateProcessing();

    sb_game_handle_t m_handle {nullptr};
    std::shared_ptr<Bridge> m_bridge;
    int64_t m_lastRequest {0};
    // The SDK's upload-finished event names no request, so one upload is in flight at a time: the
    // front one, which the next event answers.
    std::deque<Upload> m_uploads;
    std::shared_ptr<LogSink> m_logs; // set while log_message is enabled
};

} // namespace scorbit_godot
