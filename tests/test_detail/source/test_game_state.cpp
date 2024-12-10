/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include "scorbit_sdk/game_state.h"
#include "scorbit_sdk/net_base.h"
#include "game_data.h"
#include "trompeloeil_printer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <boost/uuid.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

class MockNetBase : public NetBase
{
public:
    void authenticate() override { };
    virtual void sendInstalled(const std::string &, const std::string &, bool = true) override { };
    virtual void sendHeartbeat() override { };
    MAKE_MOCK1(sendGameData, void(const scorbit::detail::GameData &), override);
};

// We need custom GameDataMatcher, because sessionUuid is randomly generated
struct GameDataMatcher {
    GameData expected;

    GameDataMatcher(const GameData &expected)
        : expected(expected)
    {
    }

    bool operator()(const GameData &actual) const
    {
        // sessionUuid should be parsed ok, otherwise it will throw an exception
        try {
            boost::uuids::uuid actualUuid = boost::uuids::string_generator()(actual.sessionUuid);
            (void)actualUuid;
        } catch (...) {
            FAIL("Invalid UUID: '" << actual.sessionUuid << "'");
        }
        return actual.isGameStarted == expected.isGameStarted && actual.ball == expected.ball
            && actual.activePlayer == expected.activePlayer && actual.players == expected.players
            && actual.modes == expected.modes;
    }

    friend std::ostream &operator<<(std::ostream &os, const GameDataMatcher &matcher)
    {
        os << "GameData { isGameStarted: " << matcher.expected.isGameStarted
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

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));

    GameData expected;
    expected.isGameStarted = true;
    expected.ball = 1;
    expected.activePlayer = 1;
    expected.players.insert(std::make_pair(1, PlayerState {1}));
    expected.players.at(1).setScore(0);

    SECTION("Start a game without setting players or scores")
    {
        // Expect sendGameData to be called once when the game starts
        REQUIRE_CALL(mockNetRef, sendGameData(ANY(GameData)))
                .WITH(GameDataMatcher(expected)(_1))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // When setGameStarted is called without setting any scores or players,
        // it should set player 1 as the active player with a score of 0.
        gameState.setGameStarted();
        gameState.commit();

        gameState.setGameStarted(); // This should do nothing, since it's already started
        gameState.commit();
    }

    SECTION("Setting score, player, ball will be reset after setGameStarted")
    {
        // Set up expectations for the game data after setting the active player and score
        REQUIRE_CALL(mockNetRef, sendGameData(ANY(GameData)))
                .WITH(GameDataMatcher(expected)(_1))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Set ball and active player and score before starting the game
        gameState.setCurrentBall(2);
        gameState.setActivePlayer(1);
        gameState.setScore(1, 1000);
        gameState.setScore(2, 2000);

        // Call setGameStarted, which should commit the changes and send the updated game state
        gameState.setGameStarted(); // This should trigger sendGameData
        gameState.commit();

        gameState.setGameStarted(); // This should do nothing, since it's already started
        gameState.commit();
    }
}

TEST_CASE("setGameFinished functionality")
{
    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into GameState, so we keep the ref
    sequence seq;

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));

    SECTION("Marks the game as finished and commits changes")
    {
        GameData expected;
        expected.isGameStarted = false;
        expected.ball = 3;
        expected.activePlayer = 2;
        expected.players.insert(std::make_pair(1, PlayerState {1, 2000}));
        expected.players.insert(std::make_pair(2, PlayerState {2, 1000}));

        // First call will be when game is started
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.isGameStarted == true)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Second call will be when the game is finished
        REQUIRE_CALL(mockNetRef, sendGameData(ANY(GameData)))
                .WITH(GameDataMatcher(expected)(_1))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act
        gameState.setGameStarted();
        gameState.commit();

        gameState.setCurrentBall(3);
        gameState.setActivePlayer(2);
        gameState.setScore(1, 2000);
        gameState.setScore(2, 1000);
        gameState.setGameFinished();
    }

    SECTION("Prevents changes to player scores after finishing")
    {
        REQUIRE_CALL(mockNetRef, sendGameData(_)).IN_SEQUENCE(seq).TIMES(1);

        // Start the game and set some initial player scores
        gameState.setScore(1, 100);
        gameState.setScore(2, 200);

        // Mark the game as finished
        gameState.setGameFinished();

        // Attempt to change the player scores and commit after the game is finished doesn't call
        // sendGameData
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

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));
    gameState.setGameStarted();

    SECTION("Sets a valid ball number")
    {
        // Assert: Check that the ball number is set correctly 1
        REQUIRE_CALL(mockNetRef, sendGameData(_)).WITH(_1.ball == 1).IN_SEQUENCE(seq).TIMES(1);
        // Act: Set a valid ball number
        gameState.setCurrentBall(1);
        gameState.commit();

        // Assert: Check that the ball number is set correctly to 3
        REQUIRE_CALL(mockNetRef, sendGameData(_)).WITH(_1.ball == 3).IN_SEQUENCE(seq).TIMES(1);
        // Set another valid ball number
        gameState.setCurrentBall(3);
        gameState.commit();
    }

    SECTION("Invalid ball numbers ignored")
    {
        // Assert: Check that the ball number is set correctly
        REQUIRE_CALL(mockNetRef, sendGameData(_)).WITH(_1.ball == 9).IN_SEQUENCE(seq).TIMES(3);
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

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));
    gameState.setGameStarted();

    SECTION("Sets a valid active player")
    {
        // Assert that active player is 1
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.activePlayer == 1)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Set player 1 as active
        gameState.setActivePlayer(1);
        gameState.commit();

        // Assert that active player is 3
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.activePlayer == 4 && _1.players.at(4).score() == 0)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Set player 4 as active when player 4 doesn't exist yet
        gameState.setActivePlayer(4);
        gameState.commit();

        // Assert: Player 2 should now exist with score 0 and be the active player
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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

    REQUIRE_CALL(mockNetRef, sendGameData(_))
            .WITH(_1.players.at(1).score() == 0)
            .IN_SEQUENCE(seq)
            .TIMES(1);

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));
    gameState.setGameStarted();
    gameState.commit();

    SECTION("Sets the score for an existing player")
    {
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.players.at(1).score() == 500)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Add player 1 with initial score
        gameState.setScore(1, 500);
        gameState.commit();

        // Assert that score of player 1 is updated to 1000
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.players.at(1).score() == 1000)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Update score for player 1
        gameState.setScore(1, 1000);
        gameState.commit();
    }

    SECTION("Does nothing if the new score is the same as the current score")
    {
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.players.at(2).score() == 1500)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Add player 2 with initial score
        gameState.setScore(2, 1500);
        gameState.commit();

        // No update should be made since the score is the same
        FORBID_CALL(mockNetRef, sendGameData(_));

        // Act: Set the same score for player 2
        gameState.setScore(2, 1500);
        gameState.commit();
    }

    SECTION("Adds a new player with the specified score if the player does not exist")
    {
        // Assert that player 3 is added with score 800
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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
        FORBID_CALL(mockNetRef, sendGameData(_));

        // Act: Try to set the score for an invalid player number (0)
        gameState.setScore(0, 1000);
        gameState.commit();
    }

    SECTION("Updates the score for multiple players")
    {
        // Add player 1 with an initial score
        // Assert: score of player 1 to 500
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.players.at(1).score() == 500)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        gameState.setScore(1, 500);
        gameState.commit();

        // Assert: Update the score of player 1 to 700
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.players.at(1).score() == 700)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Update score for player 1
        gameState.setScore(1, 700);
        gameState.commit();

        // Assert: Add player 2 with score 1500
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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

    REQUIRE_CALL(mockNetRef, sendGameData(_)).IN_SEQUENCE(seq).TIMES(1);

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));
    gameState.setGameStarted();
    gameState.commit();

    SECTION("Adds a new mode to the active modes list")
    {
        // Assert that mode "MB:Multiball" is added
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.modes.contains("MB:Multiball"))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Add mode "MB:Multiball"
        gameState.addMode("MB:Multiball");
        gameState.commit();

        // Assert that another mode "SP:SuperPlay" is added
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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

    REQUIRE_CALL(mockNetRef, sendGameData(_)).IN_SEQUENCE(seq).TIMES(1);

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));
    gameState.setGameStarted();
    gameState.commit();

    SECTION("Removes an existing mode from the active modes list")
    {
        // Assert: Mode "MB:Multiball" is removed
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.modes.contains("MB:Multiball"))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Add mode "MB:Multiball"
        gameState.addMode("MB:Multiball");
        gameState.commit();

        // Assert: Mode "MB:Multiball" is removed
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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
        FORBID_CALL(mockNetRef, sendGameData(_));

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

    REQUIRE_CALL(mockNetRef, sendGameData(_)).IN_SEQUENCE(seq).TIMES(1);

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));
    gameState.setGameStarted();
    gameState.commit();

    SECTION("Removes all modes from the active modes list")
    {
        // Assert: All modes should be removed
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.modes.str() == "MB:Multiball;SP:SuperPlay")
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Add some modes
        gameState.addMode("MB:Multiball");
        gameState.addMode("SP:SuperPlay");
        gameState.commit();

        // Assert: All modes should be removed
        REQUIRE_CALL(mockNetRef, sendGameData(_))
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
        FORBID_CALL(mockNetRef, sendGameData(_));

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

    REQUIRE_CALL(mockNetRef, sendGameData(_)).IN_SEQUENCE(seq).TIMES(1);

    // Create GameState object with mocked NetBase
    GameState gameState(std::move(mockNet));
    gameState.setGameStarted();
    gameState.commit(); // Initial commit after starting the game

    SECTION("Commits changes when the game state is modified")
    {
        // Add mode "MB:Multiball"
        gameState.addMode("MB:Multiball");

        // Assert: commit should trigger sendGameData with "MB:Multiball" added
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.modes.contains("MB:Multiball"))
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Call commit to apply changes
        gameState.commit();
    }

    SECTION("Does not commit if no changes were made")
    {
        // Assert: No sendGameData should be called since nothing was modified
        FORBID_CALL(mockNetRef, sendGameData(_));

        // Act: Call commit without making any changes
        gameState.commit();
    }

    SECTION("Commits after multiple changes to the game state")
    {
        // Make several modifications
        gameState.addMode("MB:Multiball");
        gameState.setScore(1, 500);
        gameState.setActivePlayer(2);

        // Assert: commit should trigger sendGameData with the appropriate game state
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.modes.contains("MB:Multiball") && _1.players.at(1).score() == 500
                      && _1.activePlayer == 2)
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Call commit to apply all changes
        gameState.commit();
    }

    SECTION("Commits only once if the same changes are made repeatedly")
    {
        REQUIRE_CALL(mockNetRef, sendGameData(_)).IN_SEQUENCE(seq).TIMES(1);
        // Add a mode and call commit
        gameState.addMode("MB:Multiball");
        gameState.commit();

        // Assert: No further commit should occur if the state didn't change
        FORBID_CALL(mockNetRef, sendGameData(_));

        // Act: Call commit again without any new changes
        gameState.commit();
    }

    SECTION("Commits after clearing modes")
    {
        REQUIRE_CALL(mockNetRef, sendGameData(_)).IN_SEQUENCE(seq).TIMES(1);
        // Add modes first
        gameState.addMode("MB:Multiball");
        gameState.addMode("SP:SuperPlay");
        gameState.commit();

        // Clear all modes
        gameState.clearModes();

        // Assert: commit should trigger sendGameData with no modes remaining
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.modes.isEmpty())
                .IN_SEQUENCE(seq)
                .TIMES(1);

        // Act: Call commit to apply changes
        gameState.commit();
    }
}
