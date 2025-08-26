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

#include <scorbit_sdk/net_types.h>
#include <scorbit_sdk/common_types_c.h>
#include "player_profiles_manager.h"
#include <string>
#include <optional>

namespace scorbit {
namespace detail {

struct GameData;

class NetBase
{
public:
    NetBase() = default;
    virtual ~NetBase() = default;

    virtual AuthStatus status() const = 0;

    virtual void authenticate() = 0;

    virtual void sendInstalled(const std::string &type, const std::string &version,
                               std::optional<bool> installed,
                               std::optional<std::string> log = std::nullopt) = 0;
    virtual void sessionCreate(const detail::GameData &data) = 0;
    virtual void sendGameData(const detail::GameData &data) = 0;
    virtual void sendHeartbeat() = 0;
    virtual void requestPairCode(StringCallback cb) = 0;

    virtual const std::string &getMachineUuid() const = 0;
    virtual const std::string &getPairDeeplink() const = 0;
    virtual const std::string &getClaimDeeplink(int player) const = 0;

    virtual const DeviceInfo &deviceInfo() const = 0;

    virtual void requestTopScores(sb_score_t scoreFilter, StringCallback callback) = 0;
    virtual void requestUnpair(StringCallback callback) = 0;

    virtual void download(StringCallback callback, const std::string &url,
                          const std::string &filename) = 0;
    virtual void downloadBuffer(VectorCallback callback, const std::string &url,
                                size_t reserveBufferSize) = 0;

    virtual PlayerProfilesManager &playersManager() = 0;
};

} // namespace detail
} // namespace scorbit
