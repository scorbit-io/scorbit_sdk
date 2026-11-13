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

#include "identifiers.h"
#include <scorbit_sdk/net_types.h>
#include <centrifugo/error.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace scorbit {
namespace detail {

/// The scorbitronLiveDmd signal's metadata, passed on unchanged.
struct LiveDmdSignal {
    std::string channel;
    std::int64_t until {0};
};

/// Reads the metadata of a control-channel scorbitronLiveDmd message; nullopt when malformed.
inline auto parseLiveDmdSignal(const nlohmann::json &message) -> std::optional<LiveDmdSignal>
{
    const auto it = message.find(JKEY_CHN_METADATA);
    if (it == message.end() || !it->is_object()) {
        return std::nullopt;
    }
    const auto channel = it->find(JKEY_LIVE_DMD_CHANNEL);
    const auto until = it->find(JKEY_LIVE_DMD_UNTIL);
    if (channel == it->end() || !channel->is_string() || channel->get<std::string>().empty()
        || until == it->end() || !until->is_number_integer()) {
        return std::nullopt;
    }
    return LiveDmdSignal {channel->get<std::string>(), until->get<std::int64_t>()};
}

/// Checks a publish before anything is sent; on Ok, @p parsed holds @p data.
inline auto checkPublishData(const std::string &channel, const std::string &data,
                             nlohmann::json &parsed) -> PublishResult
{
    if (data.size() > static_cast<std::size_t>(PUBLISH_MAX_BYTES)) {
        return PublishResult::TooLarge;
    }
    parsed = nlohmann::json::parse(data, nullptr, false);
    if (channel.empty() || parsed.is_discarded()) {
        return PublishResult::Invalid;
    }
    return PublishResult::Ok;
}

/// Maps centrifugo's per-publish result: only its own NotConnected is local, every other error
/// is the server's code.
inline auto toPublishResult(const centrifugo::Error *error) -> std::pair<PublishResult, int>
{
    if (!error) {
        return {PublishResult::Ok, 0};
    }
    if (error->ec == make_error_code(centrifugo::ErrorType::NotConnected)) {
        return {PublishResult::NotConnected, 0};
    }
    return {PublishResult::Refused, error->ec.value()};
}

} // namespace detail
} // namespace scorbit
