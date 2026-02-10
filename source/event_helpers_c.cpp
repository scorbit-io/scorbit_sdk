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

#include <scorbit_sdk/event_helpers_c.h>
#include <scorbit_sdk/event_types.h>
#include "event_classes.h"

using namespace scorbit;

// ------------------------------  Public API implementation -------------------------------

sb_event_type_t sb_event_type(const sb_event_t *event)
{
    const auto eventBase = static_cast<const detail::EventBase *>(event);
    return static_cast<sb_event_type_t>(eventBase->type());
}

bool sb_event_game_start_requested(const sb_event_t *event, int *players_count)
{
    if (!event || !players_count) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::GameStartRequestedEvent *>(event);
    if (!derived) {
        return false;
    }

    *players_count = derived->playersCount();
    return true;
}

bool sb_event_credits_add_requested(const sb_event_t *event, int *credits, const char **transaction)
{
    if (!event || !credits || !transaction) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::CreditsAddRequestedEvent *>(event);
    if (!derived) {
        return false;
    }

    *credits = derived->credits();
    *transaction = derived->transaction().c_str();
    return true;
}

bool sb_event_config_received(const sb_event_t *event, const char **config_json)
{
    if (!event || !config_json) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::ConfigReceivedEvent *>(event);
    if (!derived) {
        return false;
    }

    *config_json = derived->configJson().c_str();
    return true;
}

bool sb_event_scorbitd_update_received(const sb_event_t *event, const char **update_json)
{
    if (!event || !update_json) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::ScorbitdUpdateReceivedEvent *>(event);
    if (!derived) {
        return false;
    }

    *update_json = derived->updateJson().c_str();
    return true;
}

bool sb_event_scorbitd_updated(const sb_event_t *event, const char **version,
                               const char **executable_path)
{
    if (!event || !version || !executable_path) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::ScorbitdUpdatedEvent *>(event);
    if (!derived) {
        return false;
    }

    *version = derived->version().c_str();
    *executable_path = derived->executable().c_str();
    return true;
}
