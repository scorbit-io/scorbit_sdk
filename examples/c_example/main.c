/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/scorbit_sdk_c.h>

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

// ------------ Dummy functions to simulate game state just to get file compiled  --------------
int isGameFinished(int i)
{
    return i == 99;
}

int isGameJustStarted(int i)
{
    return i == 5;
}

int isGameActive(int i)
{
    return i >= 5 && i < 99;
}

int player1Score(int i)
{
    if (i == 5)
        return 0;
    return 1010 + i * 500;
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

int currentBall(int i)
{
    return i / 33 + 1;
}

int timeToClearModes(void)
{
    return 0;
}

int isUnpairTriggeredByUser(void)
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
        return; // Skip debug messages
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
    fflush(stdout); // Maybe we should not flush buffer, so it will not slow down the program
}

sb_game_handle_t setup_game_state(void)
{
    // Setup device info
    sb_device_info_t device_info = {
            .provider = "dilshodpinball", // This is required, set to your provider name
            .machine_id = 4379,
            .game_code_version = "0.1.0", // game version
            .hostname = "staging",        // Optional, if NULL, it will be production

            // UUID is optional, if NULL, will be automatically derived from device's mac address
            // However, if there is known uuid attached to the device, set it here:
            .uuid = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab", // dilshodpinball test machine
            .serial_number = 0, // If no serial number available, set to 0
    };

    // Another example with default values:
    sb_device_info_t device_info2 = {
            .provider = "vscorbitron",    // This is required, set to your provider name
            .game_code_version = "0.1.0", // game version
            .hostname = NULL,             // NULL, it will be production, or can set to "production"
            .uuid = NULL,                 // NULL, will be automatically derived from device
            .serial_number = 0,           // no serial number available, set to 0
    };
    (void)device_info2;

    // Setup encrypted key
    const char *encrypted_key = "8qWNpMPeO1AbgcoPSsdeUORGmO/hyB70oyrpFyRlYWbaVx4Kuan0CAGaXZWS3JWdgmPL7p9k3UFTwAp5y16L8O1tYaHLGkW4p/yWmA==";

    // Create game state object. Device info will be copied, so it's safe to create it in the stack
    return sb_create_game_state2(encrypted_key, &device_info);
}

void top_scores_callback(sb_error_t error, const char *reply, void *user_data)
{
    (void)user_data;

    switch (error) {
    case SB_EC_SUCCESS:
        printf("Top scores: %s\n", reply);
        break;
    case SB_EC_NOT_PAIRED:
        printf("Device is not paired\n");
        break;
    case SB_EC_API_ERROR:
        printf("API error: %s\n", reply);
        return;
    default:
        printf("Error: %d\n", error);
        break;
    }
}

void unpair_callback(sb_error_t error, const char *reply, void *user_data)
{
    (void)user_data;

    if (error == SB_EC_SUCCESS) {
        printf("Unpairing successful\n");
    } else {
        printf("Unpairing failed: %s\n", reply);
    }
}

void shortcode_callback(sb_error_t error, const char *shortcode, void *user_data)
{
    (void)user_data;

    switch (error) {
    case SB_EC_SUCCESS:
        printf("Pairing short code: %s\n", shortcode);
        break;
    case SB_EC_API_ERROR:
        printf("API error: %s\n", shortcode);
        break;
    default:
        printf("Error: %d\n", error);
        break;
    }
}

int main(void)
{
    printf("Simple example of Scorbit SDK usage\n");

    // Setup logger
    sb_add_logger_callback(loggerCallback, NULL);

    sb_game_handle_t gs = setup_game_state();

    // Request top scores
    sb_request_top_scores(gs, 0, &top_scores_callback, NULL);

    // Request deep link for pairing. This is useful if machine can display QR code.
    printf("Deeplink for pairing %s\n", sb_get_pair_deeplink(gs));

    // Alternatively, request short code for pairing which is alphanumeric 6 chars and display it
    sb_request_pair_code(gs, &shortcode_callback, NULL);

    // Main loop which is typically an infinite loop, but this example runs for 10 cycles
    for (int i = 0; i < 100; ++i) {
        // Check the auth (networking) status. It's not necessary, just for demo
        if (i % 10 == 0) {
            sb_auth_status_t status = sb_get_status(gs);
            printf("Networking status: %d\n", status);
        }

        // Next game cycle started. First check if game is finished, because it might happen,
        // that in the same cycle one game finished and started new game
        if (isGameFinished(i)) {
            // This will close current active session and do commit.
            sb_set_game_finished(gs);
        }

        if (isGameJustStarted(i)) {
            // This will start new game session with player1 score 0 and current ball 1.

            // In the same game cycle before commit it can be set new score, active player, etc.
            // So, player1's initial score will be not 0, but the one set in the current cycle
            sb_set_game_started(gs);
        }

        if (isGameActive(i)) {
            // Set player1 score, no problem, if it was not changed in the current cycle
            sb_set_score(gs, 1, player1Score(i), 0);

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
            sb_set_current_ball(gs, currentBall(i));

            // Add/remove game modes:
            if (i % 10 == 0) {
                sb_add_mode(gs, "MB:Multiball");
            } else {
                sb_remove_mode(gs, "MB:Multiball");
            }
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

        usleep(300 * 1000); // Sleep for 300 ms
    }

    if (isUnpairTriggeredByUser()) {
        sb_request_unpair(gs, &unpair_callback, NULL);
    }

    // Cleanup
    sb_destroy_game_state(gs);

    printf("Example finished\n");
    return 0;
}
