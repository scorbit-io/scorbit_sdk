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

#include "game_state_impl.h"
#include "net_base.h"
#include <scorbit_sdk/version.h>
#include "game_data.h"
#include "trompeloeil_printer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <boost/uuid.hpp>
#include <chrono>
#include <functional>
#include <optional>
#include <thread>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

namespace {

class MockNetBase : public NetBase
{
public:
    virtual ~MockNetBase() = default;

    AuthStatus status() const override { return AuthStatus::NotAuthenticated; };
    void sendHeartbeat() override { };
    void requestPairCode(StringCallback) override {};
    const std::string &getMachineUuid() const override
    {
        static std::string rv;
        return rv;
    };
    std::uint64_t getMachineSerial() const override { return 0; };
    const std::string &getPairDeeplink() const override
    {
        static std::string rv;
        return rv;
    };
    const DeviceInfo &deviceInfo() const override
    {
        static DeviceInfo info;
        info.gameCodeVersion = "1.2.3";
        return info;
    };
    void requestTopScores(sb_score_t, StringCallback) override {};
    void requestUnpair(StringCallback) override {};
    MAKE_MOCK2(submitGameData, void(const scorbit::detail::GameData &, SessionFlags), override);
    MAKE_MOCK0(authenticate, void(), override);
    void sessionCreate(const scorbit::detail::GameData &, GameStartOrigin,
                       std::function<void()>) override { };
    void getConfig() override { };
    MAKE_MOCK4(updateConfig,
               void(const std::string &, const std::string &, bool, std::optional<std::string>),
               override);
    void download(StringCallback, const std::string &, const std::string &,
                  const std::string &) override { };
    void downloadBuffer(VectorCallback, const std::string &, size_t, const std::string &) override {
    };
    PlayerProfilesManager &playersManager() override { return m_playersManager; };
    void patchScorbitron(std::string, StringCallback, std::vector<AuthStatus>) override {};
    std::string consumeNonce() override { return {}; };
    void requestPairMachine(const std::string &, const std::string &, StringCallback) override { };
    void setCapabilities(Capabilities) override {};
    void setCreditsDropped(int, const std::string &, bool) override { };
    void setCreditsStatus(bool, int, int, const char *) override { };

    void scheduleDelayedOnWorker(std::chrono::steady_clock::duration delay,
                                 std::function<void()> fn) override
    {
        lastModeExpiryScheduleDuration = delay;
        modeExpiryScheduledCallback = std::move(fn);
    }

    void cancelModeExpiryTimer() override
    {
        ++modeExpiryCancelCount;
        modeExpiryScheduledCallback = {};
    }

    /** Invokes the last callback passed to scheduleDelayedOnWorker (for mode expiry tests). */
    void fireModeExpiryTimer()
    {
        if (modeExpiryScheduledCallback) {
            modeExpiryScheduledCallback();
        }
    }

    std::chrono::steady_clock::duration lastModeExpiryScheduleDuration {};
    std::function<void()> modeExpiryScheduledCallback;
    int modeExpiryCancelCount {0};

private:
    PlayerProfilesManager m_playersManager;
};
} // namespace

// We need custom GameDataMatcher for comparing GameData objects
struct GameDataMatcher {
    GameData expected;

    GameDataMatcher(const GameData &expected)
        : expected(expected)
    {
    }

    bool operator()(const GameData &actual) const
    {
        return actual.isGameActive == expected.isGameActive && actual.ball == expected.ball
            && actual.activePlayer == expected.activePlayer && actual.players == expected.players
            && actual.modes == expected.modes;
    }

    friend std::ostream &operator<<(std::ostream &os, const GameDataMatcher &matcher)
    {
        os << "GameData { isGameActive: " << matcher.expected.isGameActive
           << ", ball: " << matcher.expected.ball
           << ", activePlayer: " << matcher.expected.activePlayer
           << ", num of players: " << matcher.expected.players.size()
           << ", modes: " << matcher.expected.modes.str() << " }";
        return os;
    }
};

// =============================================================================

TEST_CASE("setGameStarted functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));

    GameData expected;
    expected.isGameActive = true;
    expected.ball = 1;
    expected.activePlayer = 1;
    expected.players.insert(std::make_pair(1, PlayerState {1}));
    expected.players.at(1).setScore(0);

    SECTION("Start a game without setting players or scores")
    {
        // Expect submitGameData to be called once when the game starts
        REQUIRE_CALL(mockNetRef, submitGameData(ANY(GameData), _))
                .WITH(GameDataMatcher(expected)(_1))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // When setGameStarted is called without setting any scores or players,
        // it should set player 1 as the active player with a score of 0.
        gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
        gameState.commit();

        // This should do nothing, since it's already started
        gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
        gameState.commit();
    }

    SECTION("Setting score, player, ball will be reset after setGameStarted")
    {
        // Set up expectations for the game data after setting the active player and score
        REQUIRE_CALL(mockNetRef, submitGameData(ANY(GameData), _))
                .WITH(GameDataMatcher(expected)(_1))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Set ball and active player and score before starting the game
        gameState.setCurrentBall(2);
        gameState.setActivePlayer(1);
        gameState.setScore(1, 1000);
        gameState.setScore(2, 2000);

        // Call setGameStarted, which should commit the changes and send the updated game state
        gameState.setGameStarted(
                GameStartOrigin::StartButton); // This should trigger submitGameData
        gameState.commit();

        // This should do nothing, since it's already started
        gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
        gameState.commit();
    }
}

TEST_CASE("setGameFinished functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));

    SECTION("Marks the game as finished and commits changes")
    {
        GameData expected;
        expected.isGameActive = false;
        expected.ball = 3;
        expected.activePlayer = 2;
        expected.players.insert(std::make_pair(1, PlayerState {1, 2000}));
        expected.players.insert(std::make_pair(2, PlayerState {2, 1000}));

        // First call will be when game is started
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.isGameActive == true)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Bonus score for previous player (player 1) whose score changed during player switch
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.isGameActive == false && _1.activePlayer == 1 && _1.ball == 1)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Final call will be when the game is finished
        REQUIRE_CALL(mockNetRef, submitGameData(ANY(GameData), _))
                .WITH(GameDataMatcher(expected)(_1))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act
        gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
        gameState.commit();

        gameState.setCurrentBall(3);
        gameState.setActivePlayer(2);
        gameState.setScore(1, 2000);
        gameState.setScore(2, 1000);
        gameState.setGameFinished();
    }

    SECTION("Prevents changes to player scores after finishing")
    {
        gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);

        REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);

        // Start the game and set some initial player scores
        gameState.setScore(1, 100);
        gameState.setScore(2, 200);

        // Mark the game as finished
        gameState.setGameFinished();

        // Attempt to change the player scores and commit after the game is finished doesn't call
        // submitGameData
        gameState.setScore(1, 150);
        gameState.setScore(2, 250);
        gameState.addMode("NA:Multiball");
        gameState.setCurrentBall(3);
        gameState.commit();
    }
}

TEST_CASE("setCurrentBall functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));
    gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);

    SECTION("Sets a valid ball number")
    {
        // Assert: Check that the ball number is set correctly 1
        REQUIRE_CALL(mockNetRef, submitGameData(_, _)).WITH(_1.ball == 1).IN_SEQUENCE(seq).TIMES(1);
        // Act: Set a valid ball number
        gameState.setCurrentBall(1);
        gameState.commit();

        // Assert: Check that the ball number is set correctly to 3
        REQUIRE_CALL(mockNetRef, submitGameData(_, _)).WITH(_1.ball == 3).IN_SEQUENCE(seq).TIMES(1);
        // Set another valid ball number
        gameState.setCurrentBall(3);
        gameState.commit();
    }

    SECTION("Invalid ball numbers ignored")
    {
        // Assert: Check that the ball number is set correctly
        REQUIRE_CALL(mockNetRef, submitGameData(_, _)).WITH(_1.ball == 9).IN_SEQUENCE(seq).TIMES(3);
        // Act: Set an initial valid ball number 9
        gameState.setCurrentBall(9);
        gameState.commit();

        // Now set an invalid ball number (0) and set score for player 1. Ball should be still 9
        // Act: Attempt to set an invalid ball number (0)
        gameState.setCurrentBall(0); // Wrong ball, will be ignored and ball will be 9
        gameState.setScore(1, 1000); // This is to make some change, so it will be commited
        gameState.commit();

        // Now set an invalid ball number (10) and set score for player 1. Ball should be still 9
        // Act: Attempt to set an invalid ball number (10)
        gameState.setCurrentBall(10); // Wrong ball, will be ignored and ball will be 9
        gameState.setScore(1, 2000);  // This is to make some change, so it will be commited
        gameState.commit();
    }
}

TEST_CASE("setActivePlayer functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));
    gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);

    SECTION("Sets a valid active player")
    {
        // Assert that active player is 1
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.activePlayer == 1)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Set player 1 as active
        gameState.setActivePlayer(1);
        gameState.commit();

        // Assert that active player is 3
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.activePlayer == 3)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Set player 3 as active
        gameState.setActivePlayer(3);
        gameState.commit();
    }

    SECTION("Does nothing for an invalid player number")
    {
        // Assert that active player is 9
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.activePlayer == 9)
                .IN_SEQUENCE(seq)
                .TIMES(3);
        // Act: Set an initial valid player
        gameState.setActivePlayer(9);
        gameState.commit();

        // Attempt to set an invalid player number (e.g., 0)
        // The active player should remain unchanged (9)
        // Act: Set an initial valid player
        gameState.setActivePlayer(0);
        gameState.setCurrentBall(2); // To make some change, so it will be commited
        gameState.commit();

        // Attempt to set an invalid player number (e.g., 10)
        // The active player should remain unchanged (9)
        // Act: Set an initial valid player
        gameState.setActivePlayer(10);
        gameState.setCurrentBall(3); // To make some change, so it will be commited
        gameState.commit();
    }

    SECTION("Adds a new player if the active player does not exist")
    {
        // Assert: Player 4 should now exist with score 0 and be the active player
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.activePlayer == 4 && _1.players.at(4).score() == 0)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Set player 4 as active when player 4 doesn't exist yet
        gameState.setActivePlayer(4);
        gameState.commit();

        // Assert: Player 2 should now exist with score 0 and be the active player
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.activePlayer == 2 && _1.players.at(2).score() == 1000)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Set player 2 as active (another non-existing player)
        gameState.setActivePlayer(2);
        gameState.setScore(2, 1000);
        gameState.commit();
    }
}

TEST_CASE("setScore functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    REQUIRE_CALL(mockNetRef, submitGameData(_, _))
            .WITH(_1.players.at(1).score() == 0)
            .IN_SEQUENCE(seq)
            .TIMES(1);

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));
    gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
    gameState.commit();

    SECTION("Sets the score for an existing player")
    {
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.players.at(1).score() == 500)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Add player 1 with initial score
        gameState.setScore(1, 500);
        gameState.commit();

        // Assert that score of player 1 is updated to 1000
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.players.at(1).score() == 1000)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Update score for player 1
        gameState.setScore(1, 1000);
        gameState.commit();
    }

    SECTION("Does nothing if the new score is the same as the current score")
    {
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.players.at(2).score() == 1500)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Add player 2 with initial score
        gameState.setScore(2, 1500);
        gameState.commit();

        // No update should be made since the score is the same
        FORBID_CALL(mockNetRef, submitGameData(_, _));

        // Act: Set the same score for player 2
        gameState.setScore(2, 1500);
        gameState.commit();
    }

    SECTION("Adds a new player with the specified score if the player does not exist")
    {
        // Assert that player 3 is added with score 800
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.players.at(3).score() == 800)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Add player 3 with a score of 800
        gameState.setScore(3, 800);
        gameState.commit();
    }

    SECTION("Does nothing if the player number is out of range")
    {
        // No update should be made if the player number is invalid
        FORBID_CALL(mockNetRef, submitGameData(_, _));

        // Act: Try to set the score for an invalid player number (0)
        gameState.setScore(0, 1000);
        gameState.commit();
    }

    SECTION("Updates the score for multiple players")
    {
        // Add player 1 with an initial score
        // Assert: score of player 1 to 500
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.players.at(1).score() == 500)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        gameState.setScore(1, 500);
        gameState.commit();

        // Assert: Update the score of player 1 to 700
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.players.at(1).score() == 700)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Update score for player 1
        gameState.setScore(1, 700);
        gameState.commit();

        // Assert: Add player 2 with score 1500
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.players.at(2).score() == 1500)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Add player 2
        gameState.setScore(2, 1500);
        gameState.commit();
    }
}

TEST_CASE("addMode functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));
    gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
    gameState.commit();

    SECTION("Adds a new mode to the active modes list")
    {
        // Assert that mode "MB:Multiball" is added
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.contains("MB:Multiball"))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Add mode "MB:Multiball"
        gameState.addMode("MB:Multiball");
        gameState.commit();

        // Assert that another mode "SP:SuperPlay" is added
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.contains("SP:SuperPlay"))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Add another mode "SP:SuperPlay"
        gameState.addMode("SP:SuperPlay");
        gameState.commit();
    }

    SECTION("Does nothing if the mode already exists")
    {
        // Assert that mode "MB:Multiball" is added
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.str() == "MB:Multiball")
                .IN_SEQUENCE(seq)
                .TIMES(2);

        // Add mode "MB:Multiball"
        gameState.addMode("MB:Multiball");
        gameState.commit();

        // Act: Try to add the same mode "MB:Multiball"
        gameState.addMode("MB:Multiball");
        gameState.setCurrentBall(2); // to make some change, so it will be commited
        gameState.commit();
    }
}

TEST_CASE("removeMode functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));
    gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
    gameState.commit();

    SECTION("Removes an existing mode from the active modes list")
    {
        // Assert: Mode "MB:Multiball" is removed
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.contains("MB:Multiball"))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Add mode "MB:Multiball"
        gameState.addMode("MB:Multiball");
        gameState.commit();

        // Assert: Mode "MB:Multiball" is removed
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(!_1.modes.contains("MB:Multiball"))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Remove mode "MB:Multiball"
        gameState.removeMode("MB:Multiball");
        gameState.commit();
    }

    SECTION("Does nothing if the mode does not exist")
    {
        // Assert: No update should occur as the mode doesn't exist
        FORBID_CALL(mockNetRef, submitGameData(_, _));

        // Act: Try to remove a mode that doesn't exist
        gameState.removeMode("SP:SuperPlay");
        gameState.commit();
    }
}

TEST_CASE("clearModes functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));
    gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
    gameState.commit();

    SECTION("Removes all modes from the active modes list")
    {
        // Assert: All modes should be removed
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.str() == "MB:Multiball;SP:SuperPlay")
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Add some modes
        gameState.addMode("MB:Multiball");
        gameState.addMode("SP:SuperPlay");
        gameState.commit();

        // Assert: All modes should be removed
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.isEmpty())
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Clear all modes
        gameState.clearModes();
        gameState.commit();
    }

    SECTION("Does nothing if there are no modes to clear")
    {
        // Assert: No update should occur as there are no modes
        FORBID_CALL(mockNetRef, submitGameData(_, _));

        // Act: Clear modes when no modes exist
        gameState.clearModes();
        gameState.commit();
    }
}

TEST_CASE("commit functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));

    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);

    // Create GameState object with mocked NetBase
    GameStateImpl gameState(std::move(mockNet));
    gameState.setGameStarted(scorbit::GameStartOrigin::StartButton);
    gameState.commit(); // Initial commit after starting the game

    SECTION("Commits changes when the game state is modified")
    {
        // Add mode "MB:Multiball"
        gameState.addMode("MB:Multiball");

        // Assert: commit should trigger submitGameData with "MB:Multiball" added
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.contains("MB:Multiball"))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Call commit to apply changes
        gameState.commit();
    }

    SECTION("Does not commit if no changes were made")
    {
        // Assert: No submitGameData should be called since nothing was modified
        FORBID_CALL(mockNetRef, submitGameData(_, _));

        // Act: Call commit without making any changes
        gameState.commit();
    }

    SECTION("Commits after multiple changes to the game state")
    {
        // Make several modifications
        gameState.addMode("MB:Multiball");
        gameState.setScore(1, 500);
        gameState.setActivePlayer(2);

        // Bonus score for previous player (player 1) whose score changed during player switch
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.activePlayer == 1 && _1.players.at(1).score() == 500)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Assert: commit should trigger submitGameData with the appropriate game state
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.contains("MB:Multiball") && _1.players.at(1).score() == 500
                      && _1.activePlayer == 2)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Call commit to apply all changes
        gameState.commit();
    }

    SECTION("Commits only once if the same changes are made repeatedly")
    {
        REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);
        // Add a mode and call commit
        gameState.addMode("MB:Multiball");
        gameState.commit();

        // Assert: No further commit should occur if the state didn't change
        FORBID_CALL(mockNetRef, submitGameData(_, _));

        // Act: Call commit again without any new changes
        gameState.commit();
    }

    SECTION("Commits after clearing modes")
    {
        REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);
        // Add modes first
        gameState.addMode("MB:Multiball");
        gameState.addMode("SP:SuperPlay");
        gameState.commit();

        // Clear all modes
        gameState.clearModes();

        // Assert: commit should trigger submitGameData with no modes remaining
        REQUIRE_CALL(mockNetRef, submitGameData(_, _))
                .WITH(_1.modes.isEmpty())
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Call commit to apply changes
        gameState.commit();
    }
}

TEST_CASE("Sending version of sdk and game_code")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref

    ALLOW_CALL(mockNetRef, authenticate());

    // Create GameState object with mocked NetBase
    // Note: Version information is now sent via sendScorbitronObject (patchScorbitron)
    // rather than updateConfig, so we don't need to verify updateConfig calls here
    GameStateImpl gameState(std::move(mockNet));
}

TEST_CASE("addModeExpiring — duration normalization and scheduling")
{
    using namespace std::chrono_literals;

    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet;
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));
    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);

    GameStateImpl gameState(std::move(mockNet));
    gameState.setModeExpiryPoster([&gameState]() { gameState.tickModeExpiries(); });
    gameState.setGameStarted(GameStartOrigin::StartButton);
    gameState.commit();

    auto approxEqMs = [](std::chrono::steady_clock::duration d, int expectedSec) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
        const auto want = static_cast<long long>(expectedSec) * 1000;
        const auto diff = ms - want;
        return diff <= 15 && diff >= -15;
    };

    SECTION("0 seconds is normalized to 3 seconds delay")
    {
        gameState.addModeExpiring("MB:Multiball", 0);
        REQUIRE(approxEqMs(mockNetRef.lastModeExpiryScheduleDuration, 3));
    }

    SECTION("Values above 10 clamp to 10 seconds delay")
    {
        gameState.addModeExpiring("MB:Multiball", 100);
        REQUIRE(approxEqMs(mockNetRef.lastModeExpiryScheduleDuration, 10));
    }

    SECTION("1–10 seconds pass through unchanged")
    {
        gameState.addModeExpiring("MB:Multiball", 7);
        REQUIRE(approxEqMs(mockNetRef.lastModeExpiryScheduleDuration, 7));
    }
}

TEST_CASE("addModeExpiring — promote to front on repeat add")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet;
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));
    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);

    GameStateImpl gameState(std::move(mockNet));
    gameState.setModeExpiryPoster([&gameState]() { gameState.tickModeExpiries(); });
    gameState.setGameStarted(GameStartOrigin::StartButton);
    gameState.commit();

    REQUIRE_CALL(mockNetRef, submitGameData(_, _))
            .WITH(_1.modes.str() == "MB:First;SP:Second")
            .IN_SEQUENCE(seq)
            .TIMES(1);
    gameState.addMode("MB:First");
    gameState.addMode("SP:Second");
    gameState.commit();

    REQUIRE_CALL(mockNetRef, submitGameData(_, _))
            .WITH(_1.modes.str() == "SP:Second;MB:First")
            .IN_SEQUENCE(seq)
            .TIMES(1);
    gameState.addModeExpiring("SP:Second", 7);
    gameState.commit();
}

TEST_CASE("addModeExpiring — tick removes mode after delay; clearModes cancels timer")
{
    using namespace std::chrono_literals;

    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet;
    sequence seq;

    ALLOW_CALL(mockNetRef, authenticate());
    ALLOW_CALL(mockNetRef, updateConfig(_, _, _, _));
    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).IN_SEQUENCE(seq).TIMES(1);

    GameStateImpl gameState(std::move(mockNet));
    gameState.setModeExpiryPoster([&gameState]() { gameState.tickModeExpiries(); });
    gameState.setGameStarted(GameStartOrigin::StartButton);
    gameState.commit();

    REQUIRE_CALL(mockNetRef, submitGameData(_, _))
            .WITH(_1.modes.contains("MB:Multiball"))
            .IN_SEQUENCE(seq)
            .TIMES(1);
    gameState.addModeExpiring("MB:Multiball", 3);
    const auto ms2 =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    mockNetRef.lastModeExpiryScheduleDuration)
                    .count();
    const auto d2 = ms2 - 3000;
    REQUIRE((d2 <= 15 && d2 >= -15));
    gameState.commit();

    std::this_thread::sleep_for(3100ms);

    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).WITH(_1.modes.isEmpty()).IN_SEQUENCE(seq).TIMES(1);
    mockNetRef.fireModeExpiryTimer();
    gameState.commit();

    const int cancelsAfterClear = mockNetRef.modeExpiryCancelCount;
    REQUIRE_CALL(mockNetRef, submitGameData(_, _))
            .WITH(_1.modes.contains("X:Temp"))
            .IN_SEQUENCE(seq)
            .TIMES(1);
    gameState.addModeExpiring("X:Temp", 3);
    REQUIRE(mockNetRef.modeExpiryScheduledCallback);
    gameState.commit();

    REQUIRE_CALL(mockNetRef, submitGameData(_, _)).WITH(_1.modes.isEmpty()).IN_SEQUENCE(seq).TIMES(1);
    gameState.clearModes();
    gameState.commit();

    REQUIRE(mockNetRef.modeExpiryCancelCount > cancelsAfterClear);
}

TEST_CASE("GameStateImpl getMachineSerial delegates to Net")
{
    auto mockNet = std::make_unique<MockNetBase>();
    GameStateImpl gameState(std::move(mockNet));
    CHECK(gameState.getMachineSerial() == 0);
}
