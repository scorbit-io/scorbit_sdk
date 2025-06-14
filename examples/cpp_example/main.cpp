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



// ------------ Dummy functions to simulate game state just to get file compiled  --------------
bool isGameFinished(int i)
{
    return i == 99;
}

bool isGameJustStarted(int i)
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
    return false;
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

scorbit::GameState setupGameState()
{
    // Create device info struct which will be used to setup. SDK will copy all data from this struct
    // so this struct is needed only to create GameState object and then can be discarded.
    scorbit::DeviceInfo info;

    info.provider = "dilshodpinball"; // This is required, set to your provider name
    info.machineId = 4379;            // This is required, set to your machine id
    info.hostname = "staging";        // Optional, if not set, it will be "production"
    // Another example: info.hostname = "https://api.scorbit.io";

    info.gameCodeVersion = "0.1.0"; // game version

    // If not set, will be 0, however, it there is serial number attached to the device, set it here
    // info.serialNumber = 12345;

    // UUID is optional, if not set will be automatically derived from device's mac address
    // However, if there is known uuid attached to the device, set it here:
    info.uuid = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab"; // dilshodpinball test machine

    // Automatically download pictures, will be available in PlayerInfo::picture
    info.autoDownloadPlayerPics = true;

    info.scoreFeatures = G_SCORE_FEATURES;
    info.scoreFeaturesVersion = G_SCORE_FEATURES_VERSION;

    // encrypted key is generated by encrypt_tool
    std::string encryptedKey = "8qWNpMPeO1AbgcoPSsdeUORGmO/hyB70oyrpFyRlYWbaVx4Kuan0CAGaXZWS3JWdgmPL7p9k3UFTwAp5y16L8O1tYaHLGkW4p/yWmA==";

    // Create game state object. Normally, device info will be copied.
    // However, it can be moved, because we don't need this struct anymore.

    return scorbit::createGameState(encryptedKey, info);
}

using namespace std::chrono_literals;

int main()
{
    std::map<sb_player_t, scorbit::PlayerInfo> players;

    cout << "Simple example of Scorbit SDK usage" << endl;

    // Setup logger
    scorbit::addLoggerCallback(loggerCallback);

    // Create game state object
    scorbit::GameState gs = setupGameState();
    gs.requestPairCode([](scorbit::Error error, const std::string &shortCode) {
        if (error == scorbit::Error::Success) {
            cout << "Pairing short code: " << shortCode << endl;
        } else {
            cout << "Error: " << static_cast<int>(error) << endl;
        }
    });

    gs.requestTopScores(0, [](scorbit::Error error, std::string reply) {
        switch (error) {
        case scorbit::Error::Success:
            cout << "Top scores: " << reply << endl;
            break;
        case scorbit::Error::NotPaired:
            cout << "Device is not paired" << endl;
            break;
        case scorbit::Error::ApiError:
            cout << "API error: " << reply << endl;
            break;
        default:
            cout << "Error: " << static_cast<int> (error) << endl;
        }
    });

    cout << "Deeplink for pairing " << gs.getPairDeeplink() << endl;

    // Main loop which is typically an infinite loop, but this example runs for 10 cycles
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

        if (i == 50) {
            cout << "Deeplink for claiming " << gs.getClaimDeeplink(1) << endl;
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
                    cout << "Error: " << static_cast<int> (error) << endl;
                }
            });
        }

        if (isGameJustStarted(i)) {
            // This will start new game session with player1 score 0 and current ball 1.

            // In the same game cycle before commit it can be set new score, active player, etc.
            // So, player1's initial score will be not 0, but the one set in the current cycle
            gs.setGameStarted();
        }

        if (isGameActive(i)) {
            // Let's pretend that this playersNum is current number of players in the game
            int playersNum = 1;

            // Check if players info was updated and if yes, they will be prepared for retrieval
            if (gs.isPlayersInfoUpdated()) {
                // Get players infos
                for (int j = 1; j <= playersNum; ++j) {
                    if (gs.hasPlayerInfo(j)) {
                        players[j] = gs.getPlayerInfo(j);
                    } else {
                        players.erase(j);
                    }
                }
            }

            // Display players names / pictures
            for (const auto &item : players) {
                const auto &info = item.second;
                cout << "Player " << item.first << ": " << info.preferredName
                     << ", id: " << item.second.id << endl;
                if (!info.picture.empty()) {
                    // Display picture
                    cout << "Picture size: " << info.picture.size()
                         << ", url: " << item.second.pictureUrl << endl;
                    // display first 32 bytes of the picture
                    cout << "Picture (first 32 bytes): ";
                    const auto pictureEnd = info.picture.begin()
                                          + std::min(info.picture.size(), static_cast<size_t>(32));
                    for (auto it = info.picture.begin(); it != pictureEnd; ++it) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0')
                                  << static_cast<int>(*it) << ' ';
                    }
                    cout << std::dec << endl;
                }
            }

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
        std::cout << "Commit cycle " << i << std::endl;
        gs.commit();

        std::this_thread::sleep_for(500ms);
    }

    cout << "Example finished" << endl;
    return 0;
}
