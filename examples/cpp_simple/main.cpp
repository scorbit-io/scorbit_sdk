/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/scorbit_sdk.h>
#include "../common_signer/scorbit_crypt.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

using namespace std;

// ------------ Dummy functions to simulate game state just to get file compiled  --------------
bool isGameFinsihed()
{
    return false;
}

bool isGameJustStarted()
{
    return false;
}

bool isGameActive()
{
    return false;
}

int player1Score()
{
    return 1000;
}

bool hasPlayer2()
{
    return false;
}

int player2Score()
{
    return 2000;
}

bool hasPlayer3()
{
    return false;
}

int player3Score()
{
    return 3000;
}

bool hasPlayer4()
{
    return false;
}

int player4Score()
{
    return 4000;
}

int currentPlayer()
{
    return 1;
}

int currentBall()
{
    return 1;
}

bool timeToClearModes()
{
    return false;
}

// --------------- Example of logger callback ------------------

// This callback will be called in a thread-safe manner, so we don't worry about thread-safety
void loggerCallback(const std::string &message, scorbit::LogLevel level, const char *file, int line,
                    void *userData)
{
    (void)userData;
    (void)file;
    (void)line;

    const std::time_t ct = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    // Format currentTime to string like [2024-10-01 12:34:56]
    std::cout << '[' << std::put_time(std::localtime(&ct), "%Y-%m-%d %H:%M:%S") << "] [";

    switch (level) {
        case scorbit::LogLevel::Debug:
            std::cout << "DBG";
            break;
        case scorbit::LogLevel::Info:
            std::cout << "INF";
            break;
        case scorbit::LogLevel::Warn:
            std::cout << "WRN";
            break;
        case scorbit::LogLevel::Error:
            std::cout << "ERR";
            break;
    }

    std::cout << "] " << message << "\n";
    // We don't flush buffer, so it will not slow down the program
}

// --------------- Implementation of signer callback ------------------
// Signer callback can be free function or lambda, or class member, in this example we use free
// function
bool signerCallback(scorbit::Signature &signature, size_t &signatureLen,
                    const scorbit::Digest &digest)
{
    // Private key, it's better if stored as garbled and decoded before use.
    // Here, for simplicity we keep it as is.
    scorbit::Key key = {0x1e, 0xa9, 0x95, 0x77, 0x41, 0xd6, 0x89, 0x7d, 0xd2, 0xce, 0xa4,
                        0x41, 0x35, 0x27, 0xa0, 0xf8, 0x99, 0xe4, 0x23, 0xad, 0x6c, 0x23,
                        0x82, 0xb5, 0xa9, 0xf1, 0x87, 0x47, 0xeb, 0xca, 0x5b, 0xc0};

    return SIGN_OK == scorbit_sign(signature.data(), &signatureLen, digest.data(), key.data());
}

int main()
{
    cout << "Simple example of Scorbit SDK usage" << endl;

    // Setup logger
    scorbit::addLoggerCallback(loggerCallback);

    // Create game state object
    scorbit::GameState gs = scorbit::createGameState(signerCallback);

    // Main loop which is typically an infinite loop, but this example runs for 10 cycles
    for (int i = 0; i < 10; ++i) {
        // Next game cycle started. First check if game is finished, because it might happen,
        // that in the same cycle one game finished and started new game
        if (isGameFinished()) {
            // This will close current active session and do commit.
            gs.setGameFinished();
        }

        if (isGameJustStarted()) {
            // This will start new game session with player1 score 0 and current ball 1.

            // In the same game cycle before commit it can be set new score, active player, etc.
            // So, player1's initial score will be not 0, but the one set in the current cycle
            gs.setGameStarted();
        }

        if (isGameActive()) {
            // Set player1 score, no problem, if it was not changed in the current cycle
            gs.setScore(1, player1Score());

            if (hasPlayer2()) {
                // Set player2 score if player2 is present
                gs.setScore(2, player2Score());
            }

            if (hasPlayer3()) {
                // Set player3 score if player3 is present
                gs.setScore(3, player3Score());
            }

            if (hasPlayer4()) {
                // Set player4 score if player4 is present
                gs.setScore(4, player4Score());
            }

            // Set active player
            gs.setActivePlayer(currentPlayer());

            // Set current ball
            gs.setCurrentBall(currentBall());

            // Add/remove game modes:
            gs.addMode("MB:Multiball");
            gs.addMode("NA:SomeMode");
            gs.removeMode("NA:AnotherMode");

            // Sometimes we might need to clear all modes
            if (timeToClearModes()) {
                gs.clearModes();
            }
        }

        // Commit game state at the end of each cycle. This ensures that any changes
        // in the game state are captured and sent to the cloud. If no changes occurred,
        // the commit will be ignored, avoiding unnecessary uploads.
        gs.commit();
    }

    cout << "Example finished" << endl;
    return 0;
}
