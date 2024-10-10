/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/game_state.h>
#include <scorbit_sdk/net_base.h>

#include "game_data.h"
#include "trompeloeil_printer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

class MockNetBase : public scorbit::NetBase
{
public:
    MAKE_MOCK1(sendGameData, void(const scorbit::detail::GameData &), override);
};

TEST_CASE("Create and destroy GameState")
{
    {
        GameState gs;
    }
    CHECK(true); // It constructed and destructed without crash
}

TEST_CASE("GameState - setGameStarted commits game start and default settings", "[GameState]")
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

    SECTION("setGameStarted without setting players or scores")
    {
        // Expect sendGameData to be called once when the game starts
        REQUIRE_CALL(mockNetRef, sendGameData(eq(expected))).IN_SEQUENCE(seq).TIMES(1);

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
        REQUIRE_CALL(mockNetRef, sendGameData(eq(expected))).IN_SEQUENCE(seq).TIMES(1);

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
        REQUIRE_CALL(mockNetRef, sendGameData(eq(expected))).IN_SEQUENCE(seq).TIMES(1);

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

    SECTION("Does nothing for invalid ball numbers")
    {
        // Assert: Check that the ball number is set correctly
        REQUIRE_CALL(mockNetRef, sendGameData(_)).WITH(_1.ball == 3).IN_SEQUENCE(seq).TIMES(2);
        // Act: Set an initial valid ball number
        gameState.setCurrentBall(3);
        gameState.commit();

        // Now set an invalid ball number (0) and set score for player 1. Ball should be still 3
        // Act: Attempt to set an invalid ball number (0)
        gameState.setCurrentBall(0); // Wrong ball, will be ignored and ball will be 3
        gameState.setScore(1, 1000); // This is to make some change, so it will be commited
        gameState.commit();          // This commit change ball to 2
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
        // Assert that active player is 2
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.activePlayer == 2)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Set an initial valid player
        gameState.setActivePlayer(2);
        gameState.commit();

        // Attempt to set an invalid player number (e.g., 0)
        // Assert: The active player should remain unchanged (2)
        REQUIRE_CALL(mockNetRef, sendGameData(_))
                .WITH(_1.activePlayer == 2)
                .IN_SEQUENCE(seq)
                .TIMES(1);
        // Act: Set an initial valid player
        gameState.setActivePlayer(0);
        gameState.setCurrentBall(2); // To make some change, so it will be commited
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
