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

#define _POSIX_C_SOURCE 200809L
#include <scorbit_sdk/scorbit_sdk_c.h>

#include "c_api_bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

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
    return i >= 90000;
}

int isGameJustStartedByStartButton(int i)
{
    return i == 5;
}

int isGameActive(int i)
{
    return i >= 5 && i < 90000;
}

int player1Score(int i)
{
    if (i == 5)
        return 0;
    return 1010 + i;
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
    return i / 33000 + 1;
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
        // return; // Skip info messages
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
    sb_config_set_provider(config, "pedretti");
    sb_config_set_machine_id(config, 4365);
    sb_config_set_game_code_version(config, "0.1.0");

    // Set optional parameters
    sb_config_set_hostname(config, "http://192.168.8.34:8000"); // Optional, default is "production"
    sb_config_set_cf_hostname(config, "ws://192.168.8.34:8888");
    sb_config_set_auto_download_player_pics(config, false);
    sb_config_set_score_features(config, G_SCORE_FEATURES, G_SCORE_FEATURES_COUNT,
                                 G_SCORE_FEATURES_VERSION);

    // Provider's encrypted private key (generated using encrypt_tool).
    // Used for V2 provisioning authentication to prove provider identity.
    sb_config_set_encrypted_key(config, "C3DgX1/NpOG8qf8giUG0c0ZrvFe4wobdso02KRsobW2FWIgElq3cZRVt3wAH1zZQOvXF3KsHmOZU7wYeHl+7564Bcimgs3KurvJr8w==");

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

// Helper to compute time difference in microseconds
// Helper to compute time difference in microseconds (handles nsec borrow)
static inline uint64_t time_diff_us(struct timespec start, struct timespec end)
{
    int64_t sec = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nsec = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;

    if (nsec < 0) {
        sec -= 1;
        nsec += 1000000000LL; // borrow 1 second
    }

    if (sec < 0) {
        // end < start; shouldn't happen with CLOCK_MONOTONIC, but guard anyway
        return 0;
    }

    return (uint64_t)sec * 1000000ULL + (uint64_t)(nsec / 1000LL);
}

static uint64_t time_diff_ns(struct timespec start, struct timespec end)
{
    int64_t sec = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nsec = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;

    if (nsec < 0) {
        sec -= 1;
        nsec += 1000000000LL;
    }

    if (sec < 0) {
        return 0;
    }

    return (uint64_t)sec * 1000000000ULL + (uint64_t)nsec;
}

static void mode_call_bench_record(CApiBenchTick *tick, uint64_t *wall_buf, uint64_t *cpu_buf,
                                   uint64_t *tsc_buf, size_t *count, size_t cap)
{
    CApiBenchSpan span;
    c_api_bench_tock(tick, &span);
    if (*count < cap) {
        wall_buf[*count] = span.wall_ns;
        cpu_buf[*count] = span.cpu_ns;
        tsc_buf[*count] = span.tsc_delta;
        ++*count;
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

    // ---------------- GIVE TIME TO CONNECT BEFORE BENCHMARKING ----------------
    struct timespec ts;
    ts.tv_sec = 5;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);

    // ---------------- BENCHMARK VARIABLES ----------------
    struct timespec t_start, t_end, cycle_start, cycle_end;
    uint64_t total_us = 0;
    uint64_t max_cycle_us = 0;
    const int total_cycles = 30000;
    const size_t n_samples = (size_t)total_cycles;

    uint64_t *commit_wall_ns = (uint64_t *)calloc(n_samples, sizeof(uint64_t));
    uint64_t *commit_cpu_ns = (uint64_t *)calloc(n_samples, sizeof(uint64_t));
    uint64_t *commit_tsc = (uint64_t *)calloc(n_samples, sizeof(uint64_t));
    uint64_t *sdk_wall_ns = (uint64_t *)calloc(n_samples, sizeof(uint64_t));
    uint64_t *sdk_cpu_ns = (uint64_t *)calloc(n_samples, sizeof(uint64_t));
    uint64_t *sdk_tsc = (uint64_t *)calloc(n_samples, sizeof(uint64_t));
    uint64_t *cycle_wall_ns = (uint64_t *)calloc(n_samples, sizeof(uint64_t));

    /* Upper bound: per active iteration at most 3 sb_add_mode + 2 sb_remove_mode */
    const size_t mode_sample_cap = n_samples * 4u;
    uint64_t *add_mode_wall_ns = (uint64_t *)calloc(mode_sample_cap, sizeof(uint64_t));
    uint64_t *add_mode_cpu_ns = (uint64_t *)calloc(mode_sample_cap, sizeof(uint64_t));
    uint64_t *add_mode_tsc = (uint64_t *)calloc(mode_sample_cap, sizeof(uint64_t));
    uint64_t *remove_mode_wall_ns = (uint64_t *)calloc(mode_sample_cap, sizeof(uint64_t));
    uint64_t *remove_mode_cpu_ns = (uint64_t *)calloc(mode_sample_cap, sizeof(uint64_t));
    uint64_t *remove_mode_tsc = (uint64_t *)calloc(mode_sample_cap, sizeof(uint64_t));
    size_t add_mode_n = 0;
    size_t remove_mode_n = 0;

    if (!commit_wall_ns || !commit_cpu_ns || !commit_tsc || !sdk_wall_ns || !sdk_cpu_ns || !sdk_tsc
        || !cycle_wall_ns || !add_mode_wall_ns || !add_mode_cpu_ns || !add_mode_tsc
        || !remove_mode_wall_ns || !remove_mode_cpu_ns || !remove_mode_tsc) {
        fprintf(stderr, "Benchmark: out of memory for sample buffers\n");
        free(commit_wall_ns);
        free(commit_cpu_ns);
        free(commit_tsc);
        free(sdk_wall_ns);
        free(sdk_cpu_ns);
        free(sdk_tsc);
        free(cycle_wall_ns);
        free(add_mode_wall_ns);
        free(add_mode_cpu_ns);
        free(add_mode_tsc);
        free(remove_mode_wall_ns);
        free(remove_mode_cpu_ns);
        free(remove_mode_tsc);
        sb_destroy_game_state(gs);
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (int session = 0; session < 1; ++session) {
        for (int i = 0; i < total_cycles; ++i) {
            clock_gettime(CLOCK_MONOTONIC, &cycle_start);
            CApiBenchTick sdk_tick;
            c_api_bench_tick(&sdk_tick);

            // if (i % 1000 == 0) {
            //     sb_auth_status_t status = sb_get_status(gs);
            //     printf("Networking status: %d\n", status);
            // }

            if (isGameFinished(i)) {
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
                // int players_num = 1;

                // if (sb_is_players_info_updated(gs)) {
                //     for (int j = 1; j <= players_num; ++j) {
                //         if (sb_has_player_info(gs, j)) {
                //             const char *name = sb_get_player_preferred_name(gs, j);
                //             snprintf(player_names[j], sizeof(player_names[j]), "%s", name);
                //             size_t pic_size;
                //             (void)sb_get_player_picture(gs, j, &pic_size);
                //         } else {
                //             player_names[j][0] = 0;
                //         }
                //     }
                // }

                // for (int j = 1; j <= players_num; ++j)
                //     printf("Player %d: %s\n", j, player_names[j]);

                if (i % 10 == 0) {
                    sb_set_score(gs, 1, i, 2);
                    if (i % 10000 == 0) {
                        printf("Player 1 score: %d\n", i);
                    }
                }
                sb_set_active_player(gs, currentPlayer());
                sb_set_current_ball(gs, currentBall(i));

                {
                    CApiBenchTick mode_tick;
                    if (i % 10 == 0) {
                        c_api_bench_tick(&mode_tick);
                        sb_add_mode(gs, "MB:Multiball");
                        mode_call_bench_record(&mode_tick, add_mode_wall_ns, add_mode_cpu_ns,
                                               add_mode_tsc, &add_mode_n, mode_sample_cap);
                    } else {
                        c_api_bench_tick(&mode_tick);
                        sb_remove_mode(gs, "MB:Multiball");
                        mode_call_bench_record(&mode_tick, remove_mode_wall_ns, remove_mode_cpu_ns,
                                               remove_mode_tsc, &remove_mode_n, mode_sample_cap);
                    }

                    c_api_bench_tick(&mode_tick);
                    sb_add_mode(gs, "MB:Multiball");
                    mode_call_bench_record(&mode_tick, add_mode_wall_ns, add_mode_cpu_ns,
                                           add_mode_tsc, &add_mode_n, mode_sample_cap);

                    c_api_bench_tick(&mode_tick);
                    sb_add_mode(gs, "NA:SomeMode");
                    mode_call_bench_record(&mode_tick, add_mode_wall_ns, add_mode_cpu_ns,
                                           add_mode_tsc, &add_mode_n, mode_sample_cap);

                    c_api_bench_tick(&mode_tick);
                    sb_remove_mode(gs, "NA:AnotherMode");
                    mode_call_bench_record(&mode_tick, remove_mode_wall_ns, remove_mode_cpu_ns,
                                           remove_mode_tsc, &remove_mode_n, mode_sample_cap);
                }

                if (timeToClearModes())
                    sb_clear_modes(gs);
            }

            {
                CApiBenchTick commit_tick;
                c_api_bench_tick(&commit_tick);
                sb_commit(gs);
                CApiBenchSpan commit_span;
                c_api_bench_tock(&commit_tick, &commit_span);
                commit_wall_ns[(size_t)i] = commit_span.wall_ns;
                commit_cpu_ns[(size_t)i] = commit_span.cpu_ns;
                commit_tsc[(size_t)i] = commit_span.tsc_delta;

                CApiBenchSpan sdk_span;
                c_api_bench_tock(&sdk_tick, &sdk_span);
                sdk_wall_ns[(size_t)i] = sdk_span.wall_ns;
                sdk_cpu_ns[(size_t)i] = sdk_span.cpu_ns;
                sdk_tsc[(size_t)i] = sdk_span.tsc_delta;
            }

#ifdef _WIN32
            Sleep(1);
#else
            struct timespec ts_sleep;
            ts_sleep.tv_sec = 0;
            ts_sleep.tv_nsec = 300 * 1000L;
            nanosleep(&ts_sleep, NULL);
#endif

            clock_gettime(CLOCK_MONOTONIC, &cycle_end);
            uint64_t cycle_us = time_diff_us(cycle_start, cycle_end);
            cycle_wall_ns[(size_t)i] = time_diff_ns(cycle_start, cycle_end);
            total_us += cycle_us;
            if (cycle_us > max_cycle_us)
                max_cycle_us = cycle_us;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    uint64_t total_runtime_us = time_diff_us(t_start, t_end);

    if (isUnpairTriggeredByUser()) {
        sb_request_unpair(gs, &unpair_callback, NULL);
    }

    sb_destroy_game_state(gs);

    // ------------- PRINT RESULTS -------------
    printf("\nBenchmark results (call-site; void APIs measure return latency, not async work done):\n");
    fputs("Total cycles: ", stdout);
    c_api_bench_fprint_u64_grouped(stdout, (uint64_t)total_cycles);
    fputc('\n', stdout);
    fputs("Total runtime: ", stdout);
    c_api_bench_fprint_u64_grouped(stdout, total_runtime_us);
    fputs(" µs (", stdout);
    c_api_bench_fprint_double_grouped(stdout, (double)total_runtime_us / 1e6, 3);
    fputs(" s)\n", stdout);
    fputs("Average cycle wall (incl. sleep): ", stdout);
    c_api_bench_fprint_double_grouped(stdout, (double)total_us / (double)total_cycles, 3);
    fputs(" µs\n", stdout);
    fputs("Max cycle wall (incl. sleep): ", stdout);
    c_api_bench_fprint_double_grouped(stdout, (double)max_cycle_us, 3);
    fputs(" µs\n", stdout);

    if (!c_api_bench_thread_cpu_supported()) {
        printf("Note: CLOCK_THREAD_CPUTIME_ID not available; CPU columns are zero.\n");
    }
    c_api_bench_print_distribution("sb_commit() wall time (ns)", commit_wall_ns, n_samples);
    c_api_bench_print_distribution("sb_commit() thread CPU time (ns)", commit_cpu_ns, n_samples);
    if (c_api_bench_tsc_supported()) {
        c_api_bench_print_distribution("sb_commit() TSC delta (cycles)", commit_tsc, n_samples);
    } else {
        printf("\nsb_commit() TSC: not available on this platform\n");
    }

    c_api_bench_print_distribution(
            "Per-iteration SDK wall (ns), cycle start through after sb_commit, excludes nanosleep",
            sdk_wall_ns, n_samples);
    c_api_bench_print_distribution(
            "Per-iteration SDK thread CPU time (ns), same span as SDK wall", sdk_cpu_ns, n_samples);
    if (c_api_bench_tsc_supported()) {
        c_api_bench_print_distribution("Per-iteration SDK TSC delta (same span)", sdk_tsc, n_samples);
    }

    c_api_bench_print_distribution("Full cycle wall (ns), includes nanosleep", cycle_wall_ns,
                                   n_samples);

    c_api_bench_print_distribution("sb_add_mode() wall time (ns), per call", add_mode_wall_ns,
                                   add_mode_n);
    c_api_bench_print_distribution("sb_add_mode() thread CPU time (ns), per call", add_mode_cpu_ns,
                                   add_mode_n);
    if (c_api_bench_tsc_supported()) {
        c_api_bench_print_distribution("sb_add_mode() TSC delta (cycles), per call", add_mode_tsc,
                                       add_mode_n);
    }

    c_api_bench_print_distribution("sb_remove_mode() wall time (ns), per call", remove_mode_wall_ns,
                                   remove_mode_n);
    c_api_bench_print_distribution("sb_remove_mode() thread CPU time (ns), per call",
                                   remove_mode_cpu_ns, remove_mode_n);
    if (c_api_bench_tsc_supported()) {
        c_api_bench_print_distribution("sb_remove_mode() TSC delta (cycles), per call",
                                       remove_mode_tsc, remove_mode_n);
    }

    if (add_mode_n == mode_sample_cap || remove_mode_n == mode_sample_cap) {
        fputs("Warning: mode call sample cap reached; add_mode samples=", stdout);
        c_api_bench_fprint_size_grouped(stdout, add_mode_n);
        fputs(", remove_mode samples=", stdout);
        c_api_bench_fprint_size_grouped(stdout, remove_mode_n);
        fputs(", cap=", stdout);
        c_api_bench_fprint_size_grouped(stdout, mode_sample_cap);
        fputs("\n", stdout);
    }

    free(commit_wall_ns);
    free(commit_cpu_ns);
    free(commit_tsc);
    free(sdk_wall_ns);
    free(sdk_cpu_ns);
    free(sdk_tsc);
    free(cycle_wall_ns);
    free(add_mode_wall_ns);
    free(add_mode_cpu_ns);
    free(add_mode_tsc);
    free(remove_mode_wall_ns);
    free(remove_mode_cpu_ns);
    free(remove_mode_tsc);

    printf("Example finished\n");
    return 0;
}
