/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

// SB-5673: game-session bookkeeping must never wait forever for a UUID that cannot arrive.
//
// Every Net here is built from an empty DeviceInfo, so its worker never starts: nothing posted
// runs, and the session tasks are run inline through NetTestAccess.

#include "net.h"
#include "game_data.h"
#include "session_flags.h"
#include <catch2/catch_test_macros.hpp>

using namespace scorbit;
using namespace scorbit::detail;

namespace {

GameData game(int id, bool active)
{
    GameData data;
    data.id = id;
    data.isGameActive = active;
    data.players.emplace(1, PlayerState {1, 0});
    return data;
}

SessionFlags flags(SessionFlag flag)
{
    SessionFlags f;
    f.set(flag);
    return f;
}

} // namespace

TEST_CASE("Game data for a session the SDK never created is dropped, not stored")
{
    Net net(DeviceInfo {}, {});

    NetTestAccess::applyGameData(net, game(0, false), flags(SessionFlag::PlayersAdd));

    CHECK_FALSE(NetTestAccess::hasGameSession(net, 0));
}

TEST_CASE("An update waiting for a UUID stops once the create request has finished without one")
{
    Net net(DeviceInfo {}, {});
    net.sessionCreate(game(1, true), GameStartOrigin::StartButton, {});
    NetTestAccess::applyGameData(net, game(1, false), flags(SessionFlag::UploadHistoryLogs));

    SECTION("while the create request is outstanding, it keeps waiting")
    {
        NetTestAccess::sessionUpdateTask(net, 1, flags(SessionFlag::UploadHistoryLogs))();
        CHECK(NetTestAccess::hasGameSession(net, 1));
    }
    SECTION("once the create request has failed, the finished session is dropped")
    {
        // A settled status the request does not allow: the create reply comes back as a failure.
        NetTestAccess::setStatus(net, AuthStatus::AuthenticationFailed);
        NetTestAccess::sessionCreateTask(net, 1)();
        REQUIRE(NetTestAccess::hasGameSession(net, 1));

        NetTestAccess::sessionUpdateTask(net, 1, flags(SessionFlag::UploadHistoryLogs))();
        CHECK_FALSE(NetTestAccess::hasGameSession(net, 1));
    }
}
