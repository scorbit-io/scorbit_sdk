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

TEST_CASE("A session reply without a tournament clears the one cached for that score")
{
    // One score row as a session reply carries it; @p withKey false leaves tournament_id out.
    const auto reply = [](nlohmann::json tournament, bool withKey = true) {
        nlohmann::json score {
                {"position", 1}, {"id", 41}, {"is_nfc_verified", true}, {"player", nullptr}};
        if (withKey) {
            score["tournament_id"] = std::move(tournament);
        }
        return nlohmann::json::array({score});
    };
    const std::string tournament = "5f0c4a7e-2b1d-4c3e-9a8b-7d6e5f4c3b2a";
    Net net(DeviceInfo {}, {});
    NetTestAccess::processScores(net, 1, reply(tournament));
    REQUIRE(NetTestAccess::scoreTournament(net, 1, 1) == tournament);

    SECTION("a null tournament, as after the entry is returned")
    {
        NetTestAccess::processScores(net, 1, reply(nullptr));
        const auto cached = NetTestAccess::scoreTournament(net, 1, 1);
        CHECK_FALSE(cached);
        // The score row is built as {key, cached}: an empty cache is sent as null.
        CHECK(nlohmann::json {{"tournament_id", cached}}.at("tournament_id").is_null());
    }
    SECTION("no tournament key at all")
    {
        NetTestAccess::processScores(net, 1, reply(nullptr, false));
        CHECK_FALSE(NetTestAccess::scoreTournament(net, 1, 1));
    }
    SECTION("a different tournament replaces it")
    {
        const std::string other = "0e1d2c3b-4a59-4687-b5c4-d3e2f1a0b9c8";
        NetTestAccess::processScores(net, 1, reply(other));
        CHECK(NetTestAccess::scoreTournament(net, 1, 1) == other);
    }
}
