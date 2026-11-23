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
#include "event_manager.h"
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
    const auto fields = [&] { return NetTestAccess::scoreEntryFields(net, 1, 1); };
    CHECK(fields() == nlohmann::json {{"tournament_id", tournament}});

    SECTION("a null tournament, as after the entry is returned")
    {
        NetTestAccess::processScores(net, 1, reply(nullptr));
        CHECK(fields() == nlohmann::json {{"tournament_id", nullptr}});
    }
    SECTION("no tournament key at all")
    {
        NetTestAccess::processScores(net, 1, reply(nullptr, false));
        CHECK(fields() == nlohmann::json {{"tournament_id", nullptr}});
    }
    SECTION("a different tournament replaces it")
    {
        const std::string other = "0e1d2c3b-4a59-4687-b5c4-d3e2f1a0b9c8";
        NetTestAccess::processScores(net, 1, reply(other));
        CHECK(fields() == nlohmann::json {{"tournament_id", other}});
    }
}

TEST_CASE("Achievement entries publish separately from tournaments and still attribute seats")
{
    using json = nlohmann::json;
    const std::string session = "267f7995-3ce4-4837-90e2-3b6824ef0d71";
    const std::string user = "7ffa90aa-4dc9-4795-aeb7-45041ea7ddfe";
    const std::string competition = "0e1d2c3b-4a59-4687-b5c4-d3e2f1a0b9c8";
    const std::string tournament = "5f0c4a7e-2b1d-4c3e-9a8b-7d6e5f4c3b2a";

    boost::asio::io_context io;
    Net net(DeviceInfo {}, {});
    NetTestAccess::setEventManager(net,
                                   std::make_shared<EventManager>(boost::asio::make_strand(io)));
    NetTestAccess::seedGameSession(net, 1, session, false);
    SessionSeats seats;
    net.setSessionPlayersCallback([&](const std::string &sessionUuid, SessionSeats replySeats) {
        CHECK(sessionUuid == session);
        seats = std::move(replySeats);
    });
    const auto process = [&](const json &entryFields) {
        json score {{"position", 1U},
                    {"id", 41},
                    {"is_nfc_verified", true},
                    {"player",
                     {{"id", user},
                      {"username", "player"},
                      {"display_name", "Player One"},
                      {"initials", "P1"},
                      {"url", ""}}}};
        score.update(entryFields);
        seats.clear();
        NetTestAccess::processScores(net, 1, json::array({score}));
        REQUIRE(seats.size() == 1);
        CHECK(seats.at(1).userId == user);
        return NetTestAccess::scoreEntryFields(net, 1, 1);
    };

    const json achievementFields {{"tournament_id", nullptr}, {"competition_id", competition}};
    CHECK(process(achievementFields) == achievementFields);
    CHECK(seats.at(1).competition == competition);

    SECTION("an explicit competition wins if both fields are present")
    {
        CHECK(process({{"tournament_id", tournament}, {"competition_id", competition}})
              == achievementFields);
        CHECK(seats.at(1).competition == competition);
    }
    SECTION("a legacy reply still attributes seats using the tournament")
    {
        const json legacyFields {{"tournament_id", tournament}};
        CHECK(process(legacyFields) == legacyFields);
        CHECK(seats.at(1).competition == tournament);
    }
    SECTION("null clears the competition")
    {
        CHECK(process({{"tournament_id", nullptr}, {"competition_id", nullptr}})
              == json {{"tournament_id", nullptr}});
        CHECK(seats.at(1).competition.empty());
    }
    SECTION("omitting both fields clears the competition")
    {
        CHECK(process(json::object()) == json {{"tournament_id", nullptr}});
        CHECK(seats.at(1).competition.empty());
    }
    SECTION("malformed fields clear both entry IDs")
    {
        process({{"tournament_id", tournament}, {"competition_id", competition}});
        CHECK(process({{"tournament_id", "invalid uuid"}, {"competition_id", 17}})
              == json {{"tournament_id", nullptr}});
        CHECK(seats.at(1).competition.empty());
    }
}
