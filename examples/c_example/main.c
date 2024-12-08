/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/scorbit_sdk_c.h>
#include "../signer_function/scorbit_crypt.h"

#include <stdio.h>
#include <time.h>
#include <string.h>

// ------------ Dummy functions to simulate game state just to get file compiled  --------------
int isGameFinished(void)
{
    return 1;
}

int isGameJustStarted(void)
{
    return 0;
}

int isGameActive(void)
{
    return 0;
}

int player1Score(void)
{
    return 1000;
}

int hasPlayer2(void)
{
    return 0;
}

int player2Score(void)
{
    return 2000;
}

int hasPlayer3(void)
{
    return 0;
}

int player3Score(void)
{
    return 3000;
}

int hasPlayer4(void)
{
    return 0;
}

int player4Score(void)
{
    return 4000;
}

int currentPlayer(void)
{
    return 1;
}

int currentBall(void)
{
    return 1;
}

int timeToClearModes(void)
{
    return 0;
}

// --------------- Example of logger callback ------------------

// This callback will be called in a thread-safe manner, so we don't worry about thread-safety
void loggerCallback(const char *message, sb_log_level_t level, const char *file, int line,
                    void *userData)
{
    (void)userData;
    (void)file;
    (void)line;

    // Get the current time
    time_t ct = time(NULL);               // Current time in seconds since epoch
    struct tm *timeInfo = localtime(&ct); // Convert to local time

    // Buffer for formatted time string
    char timeStr[30]; // Holds a string like "2024-10-01 12:34:56"
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeInfo);

    // Determine log level string
    const char *levelStr = "UNK"; // default to unknown
    switch (level) {
    case SB_DEBUG:
        levelStr = "DBG";
        break;
    case SB_INFO:
        levelStr = "INF";
        break;
    case SB_WARN:
        levelStr = "WRN";
        break;
    case SB_ERROR:
        levelStr = "ERR";
        break;
    default:
        break;
    }

    // Use printf to print the log message
    printf("[%s] [%s] %s\n", timeStr, levelStr, message);
}

// --------------- Implementation of signer callback ------------------
// Signer callback can be free function or lambda, or class member, in this example we use free
// function
int signer_callback(uint8_t signature[SB_SIGNATURE_MAX_LENGTH], size_t *signature_len,
                    const uint8_t digest[SB_DIGEST_LENGTH], void *user_data)
{
    (void)user_data; // Unused here. But it can be used to store some context, like public key, etc.

    // Private key, it's better if stored as garbled and decoded before use.
    // Here, for simplicity we keep it as is.
    const uint8_t key[] = {0x1e, 0xa9, 0x95, 0x77, 0x41, 0xd6, 0x89, 0x7d, 0xd2, 0xce, 0xa4,
                           0x41, 0x35, 0x27, 0xa0, 0xf8, 0x99, 0xe4, 0x23, 0xad, 0x6c, 0x23,
                           0x82, 0xb5, 0xa9, 0xf1, 0x87, 0x47, 0xeb, 0xca, 0x5b, 0xc0};

    return scorbit_sign(signature, signature_len, digest, key);
}

sb_game_handle_t setup_game_state(void)
{
    // Setup device info
    sb_device_info_t device_info = {
            .provider = "vscorbitron", // This is required, set to your provider name
            .hostname = "staging",     // Optional, if NULL, it will be production
            .uuid = "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce", // Optional, if NULL, will be
                                                            // automatically derived from device
            .serial_number = 12345, // If no serial number available, set to 0
    };

    // Another example with default values:
    sb_device_info_t device_info2 = {
            .provider = "vscorbitron", // This is required, set to your provider name
            .hostname = NULL,          // NULL, it will be production, or can set to "production"
            .uuid = NULL,              // NULL, will be automatically derived from device
            .serial_number = 0,        // no serial number available, set to 0
    };
    (void)device_info2;

    // Create game state object. Device info will be copied, so it's safe to create it in the stack
    return sb_create_game_state(signer_callback, NULL, &device_info);
}

int main(void)
{

    printf("Simple example of Scorbit SDK usage\n");

    // Setup logger
    sb_add_logger_callback(loggerCallback, NULL);

    sb_game_handle_t gs = setup_game_state();

    // Main loop which is typically an infinite loop, but this example runs for 10 cycles
    for (int i = 0; i < 10; ++i) {
        // Next game cycle started. First check if game is finished, because it might happen,
        // that in the same cycle one game finished and started new game
        if (isGameFinished()) {
            // This will close current active session and do commit.
            sb_set_game_finished(gs);
        }

        if (isGameJustStarted()) {
            // This will start new game session with player1 score 0 and current ball 1.

            // In the same game cycle before commit it can be set new score, active player, etc.
            // So, player1's initial score will be not 0, but the one set in the current cycle
            sb_set_game_started(gs);
        }

        if (isGameActive()) {
            // Set player1 score, no problem, if it was not changed in the current cycle
            sb_set_score(gs, 1, player1Score(), 0);

            if (hasPlayer2()) {
                // Set player2 score if player2 is present
                sb_set_score(gs, 2, player2Score(), 0);
            }

            if (hasPlayer3()) {
                // Set player3 score if player3 is present
                sb_set_score(gs, 3, player3Score(), 0);
            }

            if (hasPlayer4()) {
                // Set player4 score if player4 is present
                sb_set_score(gs, 4, player4Score(), 0);
            }

            // Set active player
            sb_set_active_player(gs, currentPlayer());

            // Set current ball
            sb_set_current_ball(gs, currentBall());

            // Add/remove game modes:
            sb_add_mode(gs, "MB:Multiball");
            sb_add_mode(gs, "NA:SomeMode");
            sb_remove_mode(gs, "NA:AnotherMode");

            // Sometimes we might need to clear all modes
            if (timeToClearModes()) {
                sb_clear_modes(gs);
            }
        }

        // Commit game state at the end of each cycle. This ensures that any changes
        // in the game state are captured and sent to the cloud. If no changes occurred,
        // the commit will be ignored, avoiding unnecessary uploads.
        sb_commit(gs);
    }

    // Cleanup
    sb_destroy_game_state(gs);

    printf("Example finished\n");
    return 0;
}
