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

#include <scorbit_sdk/scorbit_sdk_c.h>

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

// Set score features - optional, but if you have features that help identify what
// triggered a score increase in sb_set_score().
// WARNING: in the future releasees we can ONLY add new features, but not remove
// existing ones, otherwise indices of score features of old game sesions will be
// broken. Also we should increment scoreFeaturesVersion when new feature(s) added.
const char *G_SCORE_FEATURES[] = {"ramp", "left spinner", "right spinner", "left slingshot",
                                  "right slingshot"};
const size_t G_SCORE_FEATURES_COUNT = sizeof(G_SCORE_FEATURES) / sizeof(G_SCORE_FEATURES[0]);

// Increment only if new entries added in new game releases
const int G_SCORE_FEATURES_VERSION = 1;

int gNumberOfPlayersRequested;
bool gGameStartRequestedFromLobby = false;
pthread_mutex_t gGameStartRequestMutex = PTHREAD_MUTEX_INITIALIZER;

// Global pointer to game state for event callback (set after game state is created)
sb_game_handle_t gGameStatePtr = NULL;

void set_game_start_requested(bool val)
{
    pthread_mutex_lock(&gGameStartRequestMutex);
    gGameStartRequestedFromLobby = val;
    pthread_mutex_unlock(&gGameStartRequestMutex);
}

bool is_game_start_requested(void)
{
    pthread_mutex_lock(&gGameStartRequestMutex);
    bool val = gGameStartRequestedFromLobby;
    gGameStartRequestedFromLobby = false; // reset after read
    pthread_mutex_unlock(&gGameStartRequestMutex);
    return val;
}

// ------------ Dummy functions to simulate game state just to get file compiled  --------------
int isGameFinished(int i)
{
    return i == 99;
}

int isGameJustStartedByStartButton(int i)
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
                    int64_t timestamp, void *userData)
{
    (void)userData;
    (void)file;
    (void)line;
    (void)timestamp;

    // Get the current time
    time_t ct = timestamp / 1000;         // Convert milliseconds since epoch to seconds
    struct tm *timeInfo = localtime(&ct); // Convert to local time

    // Buffer for formatted time string
    char timeStr[30]; // Holds a string like "2024-10-01 12:34:56"
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeInfo);

    int millis = timestamp % 1000; // Get milliseconds part

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
    printf("[%s.%03d] [%s] %s\n", timeStr, millis, levelStr, message);
    fflush(stdout); // Maybe we should not flush buffer, so it will not slow down the program
}

// --------------- Example of key persistence callbacks ------------------
// These callbacks are used to save and load a key to/from persistent storage.
// The SDK will call these when it needs to persist or retrieve the key.

static const char *KEY_FILE_PATH = "scorbit_device_key.json";

void saveKeyCallback(const char *key, void *user_data)
{
    (void)user_data;
    printf("Saving key to file: %s\n", KEY_FILE_PATH);

    FILE *file = fopen(KEY_FILE_PATH, "w");
    if (file) {
        fprintf(file, "%s", key);
        fclose(file);
    } else {
        printf("Failed to save key to file\n");
    }
}

int loadKeyCallback(char *buffer, size_t buffer_size, void *user_data)
{
    (void)user_data;
    printf("Loading key from file: %s\n", KEY_FILE_PATH);

    FILE *file = fopen(KEY_FILE_PATH, "r");
    if (!file) {
        // Return 0 if file doesn't exist or can't be opened
        return 0;
    }

    // Read the key from file
    size_t len = fread(buffer, 1, buffer_size - 1, file);

    if (len > 0) {
        // Check if there's more data in the file (buffer too small)
        if (fgetc(file) != EOF) {
            fclose(file);
            return -1; // Buffer size is smaller than file content
        }

        buffer[len] = '\0'; // Null-terminate
        fclose(file);
        return (int)len; // Length of read key string
    }

    fclose(file);
    return 0; // Empty file or read error
}

void eventsCallback(const sb_event_t *event, void *user_data)
{
    (void)user_data; // Use global gGameStatePtr instead

    sb_event_type_t event_type = sb_event_type(event);

    printf("Event %d received\n", event_type);

    switch (event_type) {
    case SB_EVT_GAME_START_REQUESTED: {
        int players = 0;
        if (sb_event_game_start_requested(event, &players)) {
            printf("Game start requested with %d player(s)\n", players);
            gNumberOfPlayersRequested = players;
            set_game_start_requested(true);
        }
    } break;

    case SB_EVT_CREDITS_ADD_REQUESTED: {
        int credits_to_add;
        const char *transaction;
        if (sb_event_credits_add_requested(event, &credits_to_add, &transaction)) {
            printf("Credits add requested: %d, transaction: %s\n", credits_to_add, transaction);
            // Copy transaction to your own buffer, as this transaction will be gone after this
            // callback

            // Add credits to the machine ... and then call
            if (gGameStatePtr) {
                sb_set_credits_dropped(gGameStatePtr, credits_to_add, transaction, true);
            }
        }
    } break;

    case SB_EVT_CREDITS_STATUS_REQUESTED: {
        printf("Credits status requested\n");
        if (gGameStatePtr) {
            sb_set_credits_status(gGameStatePtr, false, 10, 20, NULL);
        }
    } break;

    case SB_EVT_PLAYERS_UPDATED: {
        // The client should copy player data to its own buffer if needed, since the event data
        // becomes invalid after this callback returns.
        int count = 0;
        if (sb_event_players_updated(event, &count)) {
            printf("Players updated, count: %d\n", count);
            for (sb_player_t p = 1; p <= (sb_player_t)count; ++p) {
                bool has_info = false;
                if (sb_event_player_has_info(event, p, &has_info) && has_info) {
                    const char *preferred_name = NULL;
                    if (sb_event_player_preferred_name(event, p, &preferred_name)) {
                        printf("  Player %d: %s\n", p, preferred_name);
                    }
                } else {
                    const char *claim_url = NULL;
                    if (sb_event_player_claim_deeplink(event, p, &claim_url)) {
                        printf("  Player %d: unclaimed, claim at %s\n", p, claim_url);
                    }
                }
            }
        }
    } break;

    case SB_EVT_PLAYER_PICTURE_READY: {
        sb_player_t player_num = 0;
        const uint8_t *pic_data = NULL;
        size_t pic_size = 0;
        if (sb_event_player_picture_ready(event, &player_num, &pic_data, &pic_size)) {
            printf("Player %d picture ready: %zu bytes\n", player_num, pic_size);
        }
    } break;

    // -------- OEM providers can ignore the events below, they are mostly for scorbitron ----------
    case SB_EVT_CONFIG_RECEIVED: {
        const char *config_json = NULL;
        if (sb_event_config_received(event, &config_json)) {
            printf("Config received: %s\n", config_json ? config_json : "NULL");
            // Process the config JSON string here or copy it for further use
        }

        bool payments_enabled = false;
        if (sb_event_config_payments_enabled(event, &payments_enabled)) {
            printf("Payments enabled: %s\n", payments_enabled ? "true" : "false");
        }
    } break;

    default:
        break;
    }
}

sb_game_handle_t setup_game_state(void)
{
    // Create a config object
    sb_config_t config = sb_config_create();

    // Set required parameters
    sb_config_set_provider(config, "dilshodpinball");
    sb_config_set_machine_id(config, 4379);
    sb_config_set_game_code_version(config, "0.1.0");

    // Set optional parameters
    sb_config_set_hostname(config, "staging"); // Optional, default is "production"
    sb_config_set_auto_download_player_pics(config, false);
    sb_config_set_score_features(config, G_SCORE_FEATURES, G_SCORE_FEATURES_COUNT,
                                 G_SCORE_FEATURES_VERSION);

    // Provider's encrypted private key (generated using encrypt_tool).
    // Used for V2 provisioning authentication to prove provider identity.
    sb_config_set_encrypted_key(config,
                                "8qWNpMPeO1AbgcoPSsdeUORGmO/"
                                "hyB70oyrpFyRlYWbaVx4Kuan0CAGaXZWS3JWdgmPL7p9k3UFTwAp5y16L8O1t"
                                "YaHLGkW4p/yWmA==");

    // Setup events callback - this must be done before creating the game state
    sb_config_set_event_callback(config, &eventsCallback, NULL);

    // Key persistence callbacks - required for software key provisioning.
    // On first run, the SDK provisions a new device key via the API and saves it.
    // On subsequent runs, the saved key is loaded and used for authentication.
    sb_config_set_save_key_callback(config, &saveKeyCallback, NULL);
    sb_config_set_load_key_callback(config, &loadKeyCallback, NULL);

    // Create game state object using the config
    sb_game_handle_t handle = sb_create_game_state(config);

    // Config can be destroyed after creating the game state (data is copied)
    sb_config_destroy(config);

    return handle;
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
    // Allocate memory for player names
    // Assuming max 4 players, each name max 31 chars + null terminator
    char player_names[4][32];
    memset(player_names, 0, sizeof(player_names));

    int players_count = 1;

    printf("Simple example of Scorbit SDK usage\n");

    // Setup logger with 512 chars max message length
    sb_add_logger_callback(loggerCallback, NULL, 512);

    // Create game state (event callback is set in config before creation)
    sb_game_handle_t gs = setup_game_state();

    // Set the global pointer so event callback can access the game state
    gGameStatePtr = gs;

    // Set capabilities. Here we set both start game and credit drop capabilities
    sb_set_capabilities(gs, SB_CAPABILITY_START_GAME | SB_CAPABILITY_CREDIT_DROP);

    // Request top scores
    sb_request_top_scores(gs, 0, &top_scores_callback, NULL);

    // Request deep link for pairing. This is useful if machine can display QR code.
    printf("Deeplink for pairing %s\n", sb_get_pair_deeplink(gs));

    // Alternatively, request short code for pairing which is alphanumeric 6 chars and display it
    sb_request_pair_code(gs, &shortcode_callback, NULL);

    // Main loop which is typically an infinite loop, but this example runs for 100 cycles
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

        if (isGameJustStartedByStartButton(i)) {
            // Game was just started by player pressing start button

            // In the same game cycle before commit it can be set new score, active player, etc.
            // This will start new game session with player1 score 0 and current ball 1.
            // So, player1's initial score will be not 0, but the one set in the current cycle
            sb_set_game_started(gs, SB_GAME_STARTED_BY_BUTTON);
        } else if (is_game_start_requested()) {
            // Game was started from the app and requested to start the game on the machine
            // call function to start the game on the machine with players_count players ...
            sb_set_game_started(gs, SB_GAME_STARTED_FROM_LOBBY);
            for (int i = 1; i <= gNumberOfPlayersRequested; ++i) {
                sb_set_score(gs, i, 0, 0);
            }
            printf("Started from mobile app with %d players!\n", players_count);
        }

        if (isGameActive(i)) {
            for (int j = 1; j <= players_count; ++j) {
                printf("Player %d: %s\n", j, player_names[j]);
            }

            // Set player1 score, no problem, if it was not changed in the current cycle
            sb_set_score(gs, 1, player1Score(i), 2); // 2 is a feature, e.g., right spinner

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

        const int sleep_ms = 300;
#ifdef _WIN32
        Sleep(sleep_ms);
#else
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = sleep_ms * 1000000L; // Convert milliseconds to nanoseconds
        nanosleep(&ts, NULL);
#endif
    }

    if (isUnpairTriggeredByUser()) {
        sb_request_unpair(gs, &unpair_callback, NULL);
    }

    // Cleanup
    sb_destroy_game_state(gs);

    struct timespec ts;
    ts.tv_sec = 5;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);

    printf("Example finished\n");
    return 0;
}
