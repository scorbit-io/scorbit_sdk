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
#include "event_impl.h"

using namespace scorbit;

namespace {

// Overload for int type
template<typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
inline bool assignParam(const sb_event_t &event, T *out, size_t &intIndex, size_t &)
{
    if (intIndex >= event.ints().size() || out == nullptr)
        return false;
    *out = event.ints()[intIndex++];
    return true;
}

inline bool assignParam(const sb_event_t &event, const char **out, size_t &, size_t &stringIndex)
{
    if (stringIndex >= event.strings().size() || out == nullptr)
        return false;
    *out = event.strings()[stringIndex++].c_str();
    return true;
}

// --- Variadic extractor ---
template<typename... Args>
bool extractParameters(const sb_event_t &event, Args... args)
{
    size_t intIndex = 0;
    size_t stringIndex = 0;

    bool ok = (assignParam(event, args, intIndex, stringIndex) && ...);

    return ok;
}

} // namespace

// ------------------------------  Public API implementation -------------------------------

sb_event_type_t sb_event_type(const sb_event_t *event)
{
    return static_cast<sb_event_type_t>(event->type());
}

bool sb_event_game_start_requested(const sb_event_t *event, int *players_count)
{
    return event->type() == EventType::GameStartRequested
        && extractParameters(*event, players_count);
}

bool sb_event_credits_add_requested(const sb_event_t *event, int *credits_to_add)
{
    return event->type() == EventType::CreditsAddRequested
        && extractParameters(*event, credits_to_add);
}

bool sb_event_config_received(const sb_event_t *event, const char **config_json)
{
    return event->type() == EventType::ConfigReceived && extractParameters(*event, config_json);
}
