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

#include <scorbit_sdk/scorbit_sdk.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <map>
#include <atomic>
#include <fstream>
#include <utility>

using namespace std;

// Set score features - optional, but if you have features that help identify what triggered a
// score increase in GameState::setScore().
// WARNING: in the future releasees we can ONLY add new features, but not remove existing ones,
// otherwise indices of score features of old game sesions will be broken.
// Also we should increment G_SCORE_FEATURES_VERSION when new feature(s) added.
const std::vector<std::string> G_SCORE_FEATURES {"ramp", "left spinner", "right spinner",
                                                 "left slingshot", "right slingshot"};

// Increment only if new entries added in new game releases
constexpr int G_SCORE_FEATURES_VERSION = 1;

atomic_int gNumberOfPlayersRequested;
atomic_bool gGameStartRequestedFromLobby {false};

// Global pointer to GameState for event callback (set after GameState is created)
scorbit::GameState *gGameStatePtr = nullptr;

// ------------ Dummy functions to simulate game state just to get file compiled  --------------
bool isGameFinished(int i)
{
    return i == 99;
}

bool isGameJustStartedByStartButton(int i)
{
    return i == 5;
}

bool isGameActive(int i)
{
    return i >= 5 && i < 99;
}

int player1Score(int i)
{
    if (i == 5)
        return 0;
    return 1000 + i * 500;
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

int currentBall(int i)
{
    return i / 33 + 1;
}

bool timeToClearModes()
{
    return false;
}

bool isUnpairTriggeredByUser()
{
    static bool unpair = false; // Set to true to trigger unpairing once
    return std::exchange(unpair, false);
}

// --------------- Example of logger callback ------------------

// This callback will be called in a thread-safe manner, so we don't worry about thread-safety
void loggerCallback(const std::string &message, scorbit::LogLevel level, const char *file, int line,
                    int64_t timestamp)
{
    (void)file;
    (void)line;

    // Ignore debug messages
    if (level == scorbit::LogLevel::Debug) {
        return;
    }

    constexpr int64_t MILLIS_IN_SECOND = 1000;

    // Convert to time_t for calendar time (seconds precision)
    std::time_t t = timestamp / MILLIS_IN_SECOND; // Convert milliseconds to seconds

    // Convert to milliseconds since epoch
    int millis = timestamp % MILLIS_IN_SECOND; // Get milliseconds part

    // Format the time
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    // Add milliseconds
    oss << '.' << std::setw(3) << std::setfill('0') << millis;

    std::cout << '[' << oss.str() << "] [";

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

    std::cout << "] " << message << '\n';
    std::cout.flush(); // Maybe we should not flush buffer, so it will not slow down the program
}

// --------------- Example of key persistence callbacks ------------------
// These callbacks are used to save and load a key to/from persistent storage.
// The SDK will call these when it needs to persist or retrieve the key.

const std::string KEY_FILE_PATH = "scorbit_key.txt";

void saveKeyCallback(const std::string &key)
{
    cout << "Saving key to file: " << KEY_FILE_PATH << endl;
    std::ofstream file(KEY_FILE_PATH);
    if (file.is_open()) {
        file << key;
        file.close();
    } else {
        cerr << "Failed to save key to file" << endl;
    }
}

std::string loadKeyCallback()
{
    cout << "Loading key from file: " << KEY_FILE_PATH << endl;
    std::ifstream file(KEY_FILE_PATH);
    if (file.is_open()) {
        // Read all content to string
        std::string key((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return key;
    }
    // Return empty string if file doesn't exist (no key saved yet)
    return "";
}

void eventsCallback(const scorbit::Event &event)
{
    cout << "Event received: " << static_cast<int>(event.type()) << endl;

    switch (event.type()) {
    case scorbit::EventType::GameStartRequested: {
        int playersCount = 0;
        if (event.getGameStartRequested(playersCount)) {
            cout << "Game start requested with " << playersCount << " player(s)" << endl;
            gGameStartRequestedFromLobby = true;
            gNumberOfPlayersRequested = playersCount;
        }
    } break;

    case scorbit::EventType::CreditsAddRequested: {
        int creditsToAdd = 0;
        std::string transaction;
        if (event.getCreditsAddRequested(creditsToAdd, transaction)) {
            cout << "Credits add requested: " << creditsToAdd
                 << " credit(s), transaction: " << transaction << endl;
            // Add credits to the machine ... and then call
            if (gGameStatePtr) {
                gGameStatePtr->setCreditsDropped(creditsToAdd, transaction, true);
            }
        }
    } break;

    case scorbit::EventType::CreditsStatusRequested: {
        // This event is sent when backend requests credits status.
        // Using GameState::setCreditsStatus()
        cout << "Credits status requested" << endl;
        if (gGameStatePtr) {
            gGameStatePtr->setCreditsStatus(false, 10, 20, nullptr);
        }
    } break;

    case scorbit::EventType::PlayersUpdated: {
        std::map<sb_player_t, scorbit::PlayerInfo> players;
        if (event.getPlayersUpdated(players)) {
            cout << "Players updated, count: " << players.size() << endl;
            for (const auto &item : players) {
                if (item.second.hasInfo()) {
                    cout << "  Player " << item.first << ": " << item.second.preferredName
                         << " (id: " << item.second.id << ")" << endl;
                } else {
                    cout << "  Player " << item.first << ": unclaimed, claim at "
                         << item.second.claimDeeplink << endl;
                }
            }
        }
    } break;

    case scorbit::EventType::PlayerPictureReady: {
        sb_player_t player = 0;
        std::vector<uint8_t> picture;
        if (event.getPlayerPictureReady(player, picture)) {
            cout << "Player " << player << " picture ready: " << picture.size() << " bytes" << endl;
        }
    } break;

    case scorbit::EventType::DiagnosticsUploadRequested: {
        bool includeRecordings = false;
        if (event.getDiagnosticsUploadRequested(includeRecordings)) {
            cout << "Diagnostics upload requested, includeRecordings: " << includeRecordings
                 << endl;
            if (gGameStatePtr) {
                gGameStatePtr->uploadDiagnostics({}, {}, "example diagnostics log");
                // In a real implementation, you may send log files:
                // gGameStatePtr->uploadDiagnostics({"path/to/log1.txt", "path/to/log2.txt"}, {},
                //                                  "extra string log message");
            }
        }
    } break;

    case scorbit::EventType::DiagnosticsUploaded: {
        bool success = false;
        if (event.getDiagnosticsUploaded(success)) {
            cout << "Diagnostics upload " << (success ? "succeeded" : "failed") << endl;
        }
    } break;

    case scorbit::EventType::PricingReceived: {
        scorbit::PricingInfo pricing;
        if (event.getPricingReceived(pricing)) {
            cout << "Pricing received: free_play=" << pricing.freePlay << endl;
            if (!pricing.creditPrice.empty()) {
                cout << "  Credit price: " << pricing.creditPrice
                     << " (regular: " << pricing.creditRegularPrice << ")" << endl;
                if (!pricing.creditSalePrice.empty()) {
                    cout << "  Credit sale price: " << pricing.creditSalePrice << endl;
                }
            }
            for (const auto &bundle : pricing.bundles) {
                cout << "  Bundle: " << bundle.credits << " credits for " << bundle.price << endl;
            }
        }
    } break;

    case scorbit::EventType::PairingStatusChanged: {
        bool isPaired = false;
        if (event.getPairingStatusChanged(isPaired)) {
            cout << "Pairing status changed: " << (isPaired ? "paired" : "unpaired") << endl;
        }
    } break;

        // ----- OEM providers can ignore the events below, they are mostly for scorbitron ------

    case scorbit::EventType::ConfigReceived: {
        std::string configJson;
        if (event.eventConfigReceived(configJson)) {
            cout << "Config received: " << configJson << endl;
        } else {
            cout << "Error getting config event" << endl;
        }
    } break;

    default:
        break;
    }
}

scorbit::GameState setupGameState()
{
    // Create Config with all settings including authentication
    scorbit::Config config;
    config.setProvider("dilshodpinball")
            .setMachineId(4379)
            .setGameCodeVersion("0.1.0")
            .setHostname("staging")
            .setAutoDownloadPlayerPics(true)
            .setScoreFeatures(G_SCORE_FEATURES, G_SCORE_FEATURES_VERSION)

            // Optional: .setThreadsPriority(n) - omit or use 0 for no change to SDK background
            // thread scheduling; use a positive value (e.g. 10) on Linux to lower CPU priority of
            // those threads
            // .setThreadsPriority(10)

            // Provider's encrypted private key (generated using encrypt_tool).
            // Used for V2 provisioning authentication to prove provider identity.
            .setEncryptedKey(
                    "8qWNpMPeO1AbgcoPSsdeUORGmO/"
                    "hyB70oyrpFyRlYWbaVx4Kuan0CAGaXZWS3JWdgmPL7p9k3UFTwAp5y16L8O1tYaHLGkW4p/yWmA==")
            .setEventCallback(eventsCallback)
            // Key persistence callbacks - required for software key provisioning.
            // On first run, the SDK provisions a new device key and saves it.
            // On subsequent runs, the saved key is loaded for authentication.
            .setSaveKeyCallback(saveKeyCallback)
            .setLoadKeyCallback(loadKeyCallback);

    return scorbit::createGameState(config);
}

using namespace std::chrono_literals;
using namespace std::placeholders;

int main()
{
    cout << "Simple example of Scorbit SDK usage" << endl;

    // Setup logger with max 512 chars message length
    scorbit::addLoggerCallback(loggerCallback, 512);

    // Create game state object (event callback is set in Config before creation)
    scorbit::GameState gs = setupGameState();

    // Set the global pointer so event callback can access the GameState
    gGameStatePtr = &gs;

    // Set capabilities. Here we set both start game and credit drop capabilities
    gs.setCapabilities(scorbit::Capability::StartGame | scorbit::Capability::CreditDrop);

    gs.requestPairCode([](scorbit::Error error, const std::string &shortCode) {
        if (error == scorbit::Error::Success) {
            cout << "Pairing short code: " << shortCode << endl;
        } else {
            cout << "Error: " << static_cast<int>(error) << endl;
        }
    });

    gs.requestTopScores(scorbit::LeaderboardScope::Machine, scorbit::LeaderboardPeriod::AllTime,
                        "", scorbit::LeaderboardVpinFilter::RealOnly,
                        [](scorbit::Error error, const scorbit::LeaderboardResult &leaderboard) {
        switch (error) {
        case scorbit::Error::Success:
            cout << "Top scores count: " << leaderboard.entries.size() << endl;
            for (const auto &entry : leaderboard.entries) {
                cout << "#" << entry.rank << " " << entry.player.initials << " "
                     << entry.player.displayName << " " << entry.highScore << endl;
            }
            break;
        case scorbit::Error::NotPaired:
            cout << "Device is not paired" << endl;
            break;
        case scorbit::Error::ApiError:
            cout << "API error" << endl;
            break;
        default:
            cout << "Error: " << static_cast<int>(error) << endl;
        }
    });

    cout << "Deeplink for pairing " << gs.getPairDeeplink() << endl;

    // UUID / serial often fill in after networking; poll after each commit, log once when set.
    bool isLoggedMachineSerial = false;

    // Main loop which is typically an infinite loop, but this example runs for 100 cycles
    for (int i = 0; i < 100; ++i) {
        // Check the auth (networking) status. It's not necessary, just for demo
        if (i % 10 == 0) {
            auto status = gs.getStatus();
            cout << "Networking status: " << static_cast<int>(status) << endl;
        }

        // Next game cycle started. First check if game is finished, because it might happen,
        // that in the same cycle one game finished and started new game
        if (isGameFinished(i)) {
            // This will close current active session and do commit.
            gs.setGameFinished();
        }

        if (isUnpairTriggeredByUser()) {
            // Request unpairing
            gs.requestUnpair([](scorbit::Error error, std::string reply) {
                switch (error) {
                case scorbit::Error::Success:
                    cout << "Unpairing successful" << endl;
                    break;
                case scorbit::Error::NotPaired:
                    cout << "Device is not paired" << endl;
                    break;
                case scorbit::Error::ApiError:
                    cout << "API error: " << reply << endl;
                    break;
                default:
                    cout << "Error: " << static_cast<int>(error) << endl;
                }
            });
        }

        if (isGameJustStartedByStartButton(i)) {
            // Game was just started by player pressing start button

            // In the same game cycle before commit it can be set new score, active player, etc.
            // So, player1's initial score will be not 0, but the one set in the current cycle
            // This will start new game session with player1 score 0 and current ball 1.
            gs.setGameStarted(scorbit::GameStartOrigin::StartButton);

        } else if (gGameStartRequestedFromLobby.exchange(false)) { // Reset the flag

            for (int i = 1; i <= gNumberOfPlayersRequested; ++i) {
                gs.setScore(i, 0);
            }
            gs.setGameStarted(scorbit::GameStartOrigin::FromLobby);

            // It's not necessary to call setGameStarted, as it's automaticlly called when
            // request arrived and will be be ignored here
            cout << "Started from mobile app with " << gNumberOfPlayersRequested.load()
                 << " player(s)!" << endl;
        }

        if (isGameActive(i)) {
            // Set player1 score, no problem, if it was not changed in the current cycle
            gs.setScore(1, player1Score(i), 2); // 2 is a feature, e.g., right spinner

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
            gs.setCurrentBall(currentBall(i));

            // Add/remove game modes:
            if (i % 10 == 0) {
                gs.addMode("MB:Multiball");
            } else {
                gs.removeMode("MB:Multiball");
            }

            // Expiring modes (recommended duration: 3 seconds, max 10 seconds).
            // No need to call removeMode() for this expiring mode.
            gs.addModeExpiring("MB:Multiball", 3);
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
        std::cout << "Commit cycle " << i << std::endl;
        gs.commit();

        // Log machine serial and UUID at most once, when they become available (non 0 serial)
        if (!isLoggedMachineSerial) {
            const auto serial = gs.getMachineSerial();
            if (serial != 0) {
                cout << "Machine serial: " << serial << endl;
                const auto uuid = gs.getMachineUuid();
                if (!uuid.empty()) {
                    cout << "Machine UUID: " << uuid << endl;
                }
                isLoggedMachineSerial = true;
            }
        }

        std::this_thread::sleep_for(500ms);
    }

    scorbit::resetLogger();
    cout << "Example finished" << endl;
    return 0;
}
