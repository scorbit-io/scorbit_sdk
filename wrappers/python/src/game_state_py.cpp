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
#include <scorbit_sdk/game_state_factory.h>

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include <sstream>
#include <iostream>

namespace py = pybind11;

using namespace scorbit;

// Convert C++ callbacks to Python-safe callbacks
template<typename Func>
auto makeSafeCallback(Func &&callback)
{
    using FuncType = std::decay_t<Func>;
    // Create a shared_ptr to store the Python callback
    // auto callbackPtr = std::make_shared<Func>(std::forward<Func>(callback));
    auto callbackPtr = std::shared_ptr<FuncType>(
            new FuncType(std::forward<Func>(callback)), [](FuncType *ptr) {
                if (!Py_IsInitialized()) {
                    std::cerr << "Skip deleting callback: Python is finalizing" << std::endl;
                    // We don't delete ptr here, otherwise it will cause deadlock and will never return
                    return;
                }

                // Ensure we hold the GIL before deleting, otherwise there will be exception
                py::gil_scoped_acquire gil;
                delete ptr;
            });

    // Return a lambda that will safely call the Python function
    return [callbackPtr](auto &&...args) {
        if (!Py_IsInitialized()) {
            std::cerr << "Skip invoking callback: Python is finalizing" << std::endl;
            return;
        }

        // Get the GIL before calling into Python
        py::gil_scoped_acquire gil;

        try {
            // Call the Python callback with the provided arguments
            (*callbackPtr)(std::forward<decltype(args)>(args)...);
        } catch (...) {
            // Ignore any exceptions
        }
    };
}

PYBIND11_MODULE(scorbit, m)
{
    m.doc() = "Scorbit SDK";

    m.attr("__version__") = SCORBIT_SDK_VERSION;

    py::enum_<LogLevel>(m, "LogLevel", "Enumeration of log levels for the logger.")
            .value("Debug", LogLevel::Debug, "Detailed debugging information.")
            .value("Info", LogLevel::Info, "Informational messages about application progress.")
            .value("Warn", LogLevel::Warn,
                   "Indicates potential issues that do not prevent execution.")
            .value("Error", LogLevel::Error, "Critical issues that may cause application failure.");

    // Bind Error enum
    py::enum_<Error>(m, "Error", "Enumeration of possible error codes.")
            .value("Success", Error::Success, "Operation completed successfully.")
            .value("Unknown", Error::Unknown, "An unknown error occurred.")
            .value("AuthFailed", Error::AuthFailed, "Authentication failed.")
            .value("NotPaired", Error::NotPaired, "The device is not paired.")
            .value("ApiError", Error::ApiError, "An error occurred during an API call.");

    // Bind AuthStatus enum
    py::enum_<AuthStatus>(m, "AuthStatus", "Enumeration of authentication states.")
            .value("NotAuthenticated", AuthStatus::NotAuthenticated, "Not authenticated.")
            .value("Authenticating", AuthStatus::Authenticating, "Authentication in progress.")
            .value("AuthenticatedCheckingPairing", AuthStatus::AuthenticatedCheckingPairing,
                   "Authenticated but checking pairing status.")
            .value("AuthenticatedUnpaired", AuthStatus::AuthenticatedUnpaired,
                   "Authenticated but not paired.")
            .value("AuthenticatedPaired", AuthStatus::AuthenticatedPaired,
                   "Authenticated and paired.")
            .value("AuthenticationFailed", AuthStatus::AuthenticationFailed,
                   "Authentication process failed.");

    // Wrapper for addLoggerCallback
    m.def(
            "add_logger_callback",
            [](py::function callback) {
                auto safe_callback =
                        makeSafeCallback([callback = std::move(callback)](
                                                 const std::string &message, LogLevel level,
                                                 const char *file, int line, int64_t timestamp) {
                            callback(message, level, file, line, timestamp);
                        });

                // Call the C++ method with our wrapped callback.
                addLoggerCallback(std::move(safe_callback));
            },
            py::arg("callback"),
            R"doc(
                Add a logger callback function to be invoked for log messages.

                This function registers a custom logger callback that will be called
                whenever a log message is generated. The callback provides detailed
                information about the log event, including the message, log level,
                source file, line number, and any user-provided data.

                Args:
                    callback (Callable[[str, scorbit.LogLevel, str, int, Any], None]):
                        The logger callback function to be registered. It should match
                        the `LoggerCallback` signature.
                    user_data (Any, optional):
                        A user-defined data object that will be passed to the logger
                        callback each time it is invoked. Defaults to `None`.

                See Also:
                    - `reset_logger()`
            )doc");

    // Wrapper for resetLogger
    m.def("reset_logger", &resetLogger, R"doc(
            Clears all previously added logger callbacks.

            This function removes all logger callback functions that were previously added
            using `add_logger_callback()`. After this call, no logger callback will be
            invoked until a new one is added.

            See Also:
                - `add_logger_callback()`
        )doc");

    // DeviceInfo struct
    py::class_<DeviceInfo>(m, "DeviceInfo")
            .def(py::init<>(), "Creates an empty DeviceInfo instance.")

            .def_readwrite("provider", &DeviceInfo::provider,
                           "Mandatory. The provider name, e.g., 'scorbitron', 'vpin'.")

            .def_readwrite(
                    "machine_id", &DeviceInfo::machineId,
                    "Mandatory for manufacturers. The Machine ID assigned by Scorbit, e.g., 4419.")

            .def_readwrite("game_code_version", &DeviceInfo::gameCodeVersion,
                           "Mandatory. The game code version, e.g., '1.12.3'.")

            .def_readwrite("hostname", &DeviceInfo::hostname, R"doc(
                The hostname of the server to connect to (optional).

                If not set, the default `"production"` hostname will be used. The two standard options
                are `"production"` and `"staging"`, each mapped to pre-defined URLs. To use a custom URL,
                provide it in the format `"http[s]://<url>[:port]"`.

                Examples:
                    - `"production"`
                    - `"staging"`
                    - `"https://api.scorbit.io"`
                    - `"https://staging.scorbit.io"`
                    - `"http://localhost:8080"`
            )doc")

            .def_readwrite("uuid", &DeviceInfo::uuid, R"doc(
                The device's UUID (optional).

                If not set, the UUID will be derived from the device's MAC address. This is the
                preferred way to set the UUID for machines without TPM.
                If you have a known UUID, you can set it here.

                Examples:
                    - `"f0b188f8-9f2d-4f8d-abe4-c3107516e7ce"`
                    - `"f0b188f89f2d4f8dabe4c3107516e7ce"`
                    - `"F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE"`
                    - `"F0B188F89F2D4F8DABE4C3107516E7CE"`
            )doc")

            .def_readwrite("serial_number", &DeviceInfo::serialNumber,
                           "Optional. The serial number of the device. If not set it will be 0.")

            .def_readwrite("auto_download_player_pics", &DeviceInfo::autoDownloadPlayerPics,
                           "If true, the SDK will automatically download players' profile pictures.")

            .def_readwrite("score_features", &DeviceInfo::scoreFeatures, R"doc(
                Optional. The list of score features.
                A list of score features that help identify what triggered a score increase
                (e.g., ramp, spinner, target, etc.).

                Leave this vector empty if no specific features are provided.

                Example:
                    - `Example: ["ramp", "left spinner", "right spinner"]`)doc")

            .def_readwrite("score_features_version", &DeviceInfo::scoreFeaturesVersion, R"doc(
                Optional. Version number for the score features.
                Initially set to 1 if there are score features. It should be incremented if the
                score features array has new entries.
                If `score_features` is empty, this entry will be ignored.
            )doc")

            .def("__repr__", [](const DeviceInfo &d) {
                std::stringstream ss;
                ss << std::hex << std::showbase << reinterpret_cast<std::uintptr_t>(&d);
                return "<DeviceInfo provider='" + d.provider + "', machine_id="
                     + std::to_string(d.machineId) + ", game_code_version='" + d.gameCodeVersion
                     + "', hostname='" + d.hostname + "', uuid='" + d.uuid + "', serial_number="
                     + std::to_string(d.serialNumber) + ", address=" + ss.str() + ">";
            });

    // PlayerInfo struct
    py::class_<PlayerInfo>(m, "PlayerInfo", R"doc(
            Player's profile information class.

            This class provides an interface to access the player's profile information, including
            their preferred name, full name, initials, and profile picture. The profile picture is
            represented as bytearray (jpg format).

            Note:
                The profile picture is automatically downloaded if `auto_download_player_pics` is
                set to true in the `DeviceInfo` class.
        )doc")

            .def(py::init<>(), "Creates PlayerInfo instance with default values.")

            .def_readonly("id", &PlayerInfo::id, "The player's id in Scorbit system.")

            .def_readonly("preferred_name", &PlayerInfo::preferredName,
                          "The player's preferred name.")

            .def_readonly("name", &PlayerInfo::name, "The player's full name.")

            .def_readonly("initials", &PlayerInfo::initials, "The player's initials.")

            .def_readonly("picture_url", &PlayerInfo::pictureUrl,
                          "The player's profile picture url.")

            .def_property_readonly(
                    "picture",
                    [](const PlayerInfo &self) {
                        return py::bytes(reinterpret_cast<const char *>(self.picture.data()),
                                         self.picture.size());
                    },
                    "The player's profile picture in binary format (jpg).");

    // GameState
    // By default, pybind11 holds the GIL while destructing objects. However, this can cause a
    // deadlock in cases where destruction involves joining threads that trigger callbacks
    // (e.g., a logger), while those callbacks also attempt to acquire the GIL.
    // A workaround for older versions of pybind11 can be found here:
    // https://github.com/pybind/pybind11/issues/1446#issuecomment-406341510
    // In recent versions, pybind11 provides the `py::release_gil_before_calling_cpp_dtor()`
    // attribute, which releases the GIL before calling the destructor, preventing this issue.
    py::class_<GameState> {m, "GameState", py::release_gil_before_calling_cpp_dtor {}, R"doc(
            Game state class.

            This class provides an interface to modify the game state. The game state is a collection of
            information about the current state of the game, such as the active player, score, and active
            modes. The game state can be modified by setting the active player, updating the player's score,
            and adding or removing modes.

            The game must be marked as started by calling `set_game_started()` before any modifications to
            the state can be made.

            Note:
                To apply changes to the game state, the `commit()` function must be called. This function
                finalizes all modifications made to the game state. Until `commit()` is invoked, the game
                state remains unchanged. Ideally, `commit()` should be called at the end of each frame cycle.

            Warning:
                If the game is not active, all calls to modify the game state, as well as `commit()`, will
                be ignored.
        )doc"}

            .def("set_game_started", &GameState::setGameStarted, R"doc(
                Mark the game as started.

                This function sets the game session active, resetting the game state. It initializes the
                active player to Player 1 with a score of 0, and sets the current ball to 1.

                If the game is already in progress, this function has no effect.

                Note:
                    After starting the game, `commit()` must be called to notify the cloud. Optionally,
                    before calling `commit()`, the active player, scores, modes, or current ball can be modified.
            )doc")

            .def("set_game_finished", &GameState::setGameFinished, R"doc(
                Mark the game as finished.

                Marks the game as completed. Call this function when the game ends.

                Note:
                    This function automatically commits changes using `commit()`.

                Warning:
                    After the game is finished, you can't add any modes or change players' scores.
            )doc")

            .def("set_current_ball", &GameState::setCurrentBall, py::arg("ball"), R"doc(
                Set the current ball number.

                Updates the current ball number in the game. When the game starts, the ball number is
                automatically set to 1.

                Args:
                    ball (int): The ball number [1-9]. If the ball number is out of range, the function does
                    nothing.
            )doc")

            .def("set_active_player", &GameState::setActivePlayer, py::arg("player"), R"doc(
                Set the active player.

                Updates the current active player in the game. By default, Player 1 is the active player.

                Note:
                    If the active player is set while the player does not yet exist, a new player will be added
                    with a score of 0 and set as active.

                Args:
                    player (int): The player's number [1-9]. Typically, up to 6 players are supported in
                    pinball. If the player number is out of range, the function does nothing.
            )doc")

            .def("set_score", &GameState::setScore, py::arg("player"), py::arg("score"),
                 py::arg("feature") = 0, R"doc(
                Set the player's score.

                Updates the specified player's score. If the new score is the same as the current score,
                no update is made. If the player does not exist, a new player is added with the specified score.

                Args:
                    player (int): The player's number [1-9]. If the player number is out of range, the function
                        does nothing.
                    score (int): The player's new score.
                    feature (int, optional): The score feature (i.e., what game feature caused the score bump,
                        like a spinner, etc.). If not set, it defaults to 0.
            )doc")

            .def("add_mode", &GameState::addMode, py::arg("mode"), R"doc(
                Add a mode to the game.

                Adds a mode to the game's active mode list. If the mode already exists, it is skipped.
                To remove a mode, use `remove_mode()`. All modes can be cleared at once using `clear_modes()`.

                Args:
                    mode (str): The mode to add (e.g., "MB:Multiball").
            )doc")

            .def("remove_mode", &GameState::removeMode, py::arg("mode"), R"doc(
                Remove a mode from the game.

                Removes a mode from the game's active mode list. If the mode does not exist, it is skipped.
                To remove all modes at once, use `clear_modes()`.

                Args:
                    mode (str): The mode to remove (e.g., "MB:Multiball"). If the mode doesn't exist, the
                        function does nothing.
            )doc")

            .def("clear_modes", &GameState::clearModes, R"doc(
                Clear all modes.

                Removes all modes from the game's active mode list.
            )doc")

            .def("commit", &GameState::commit, R"doc(
                Commit changes to the game state.

                Applies all changes made to the game state. This function should be called after
                any modifications to the game state, such as `set_active_player()`, `set_score()`,
                `add_mode()`, `remove_mode()`, or `clear_modes()`.

                If nothing was changed, this function does nothing.
            )doc")

            .def("get_status", &GameState::getStatus, R"doc(
                Retrieve the current authentication status.

                Key statuses to consider:
                - `AuthStatus.AuthenticatedUnpaired`: Authentication succeeded, but pairing is not established.
                - `AuthStatus.AuthenticatedPaired`: Authentication succeeded, and pairing is established.
                - `AuthStatus.AuthenticationFailed`: The authentication process failed, indicating a signing error.

                Returns:
                    AuthStatus: The current authentication status.
            )doc")

            .def("get_machine_uuid", &GameState::getMachineUuid, R"doc(
                Retrieve the machine's UUID.

                If the machine UUID was not provided, it is derived from the MAC address.

                Returns:
                    str: The machine UUID.
            )doc")

            .def("get_pair_deeplink", &GameState::getPairDeeplink, R"doc(
                Retrieve the pairing deeplink.

                This link must be encoded and displayed as a QR code, allowing the user to scan it with
                a mobile app to complete pairing.

                Returns:
                    str: The pairing deeplink. If the machine is not paired or the SDK is not yet
                        authenticated, an empty string is returned.
            )doc")

            .def("get_claim_deeplink", &GameState::getClaimDeeplink, py::arg("player"), R"doc(
                Retrieve the claim and navigation deeplink.

                This link must be encoded and displayed as a QR code, allowing the user to scan it with
                a mobile app to claim the player's slot.

                Args:
                    player (int): The player number (starting from 1).

                Returns:
                    str: The claim deeplink string. If the machine is not paired or the SDK is not yet
                        authenticated, an empty string is returned.
            )doc")

            .def(
                    "request_top_scores",
                    [](GameState &self, int64_t score_filter, py::function callback) {
                        // Call the C++ method with safe-wrapped callback
                        self.requestTopScores(score_filter, makeSafeCallback(std::move(callback)));
                    },
                    py::arg("score_filter"), py::arg("callback"),
                    R"doc(
                        Retrieve the top scores from the leaderboard.

                        Fetches the top scores from the leaderboard. If a score filter is provided, the function
                        retrieves the ten scores above and ten scores below the specified value, allowing the user
                        to view their score in the leaderboard context.

                        Note:
                            The callback function is invoked asynchronously when the operation completes, running
                            in a separate thread from the main calling thread. It is recommended to use appropriate
                            locks (e.g., a mutex) when accessing shared data.

                        Args:
                            score_filter (int): A score value used to filter the leaderboard results. If set to 0,
                                no filter is applied.
                            callback (Callable[[str, scorbit.Error], None]): A callback function that receives the
                                leaderboard scores in JSON format as a string and a `scorbit.Error` status. If the
                                request is successful, the JSON string contains the leaderboard data. Otherwise, an
                                error code is returned:
                                - `scorbit.Error.NotPaired` if the machine is not paired.
                                - `scorbit.Error.ApiError` if the API call failed.

                        This function does not return a value. The result is delivered via the callback.

                        Example:
                            def on_top_scores_received(error, json_scores):
                                if error == scorbit.Error.Success:
                                    print(f"Leaderboard scores: {parse_json(json_scores)}")
                                else:
                                    print(f"Failed to retrieve scores: {error}")

                            game_state.request_top_scores(100000, on_scores_received)
                    )doc")

            .def(
                    "request_pair_code",
                    [](const GameState &self, std::function<void(Error, std::string)> callback) {
                        // Ensure Python callback is called safely in another thread
                        self.requestPairCode(makeSafeCallback(std::move(callback)));
                    },
                    py::arg("callback"),
                    R"doc(
                        Request a pairing short code (6 alphanumeric characters).

                        Requests a pairing short code from the server. The short code is used to pair the device with
                        the Scorbit service on machines that can display only alphanumeric characters. This is an
                        alternative to `get_pair_deeplink()`.

                        Note:
                            The callback function is invoked asynchronously when the operation completes, running
                            in a separate thread from the main calling thread. It is recommended to use appropriate
                            locks (e.g., a mutex) when accessing shared data.

                        Args:
                            callback (Callable[[str, scorbit.Error], None]): A callback function that receives the short
                                code as a string and a `scorbit.Error` status. If the request is successful, the short
                                code is provided. Otherwise, an error code is returned:
                                - `scorbit.Error.ApiError` if the API call failed.

                        This function does not return a value. The result is delivered via the callback.

                        Example:
                            def on_pair_code_received(error, code):
                                if error == scorbit.Error.Success:
                                    print(f"Pairing code received: {code}")
                                else:
                                    print(f"Failed to get pairing code: {error}")

                            game_state.request_pair_code(on_pair_code_received)
                    )doc")

            .def(
                    "request_unpair",
                    [](const GameState &self, py::function callback) {
                        self.requestUnpair(makeSafeCallback(std::move(callback)));
                    },
                    py::arg("callback"),
                    R"doc(
                        Request to unpair the device.

                        Sends a request to unpair the device from the Scorbit service. This function should be called
                        when the device is being reset by a new owner.

                        Note:
                            The callback function is invoked asynchronously when the operation completes, running
                            in a separate thread from the main calling thread. It is recommended to use appropriate
                            locks (e.g., a mutex) when accessing shared data.

                        Args:
                            callback (Callable[[str, scorbit.Error], None]): A callback function that receives the
                                API response as a string and a `scorbit.Error` status. If the request is successful,
                                it returns `scorbit.Error.Success`. Otherwise, an error code is returned:
                                - `scorbit.Error.ApiError` if the API call failed.

                        This function does not return a value. The result is delivered via the callback.

                        Example:
                            def on_unpair_response(error, response):
                                if error == scorbit.Error.Success:
                                    print("Device successfully unpaired.")
                                else:
                                    print(f"Failed to unpair device: {error}")

                            game_state.request_unpair(on_unpair_response)
                    )doc")

            // -------------------------- PLAYER PROFILE INFO --------------------------------------

            .def("is_players_info_updated", &GameState::isPlayersInfoUpdated,
                 R"doc(
                        Check if player profiles have been updated.

                        This function checks if any player profiles have been updated since the last call.
                        If there are updates, use `get_player_info()` to retrieve the updated data.

                        Returns:
                            bool: True if there are updates to any player profiles; False otherwise.
                    )doc")

            .def("has_player_info", &GameState::hasPlayerInfo, py::arg("player"),
                 R"doc(
                        Check if player information is available.

                        Args:
                            player (int): The player number (starting from 1).

                        Returns:
                            bool: True if player information is available; False otherwise.
                    )doc")

            .def("get_player_info", &GameState::getPlayerInfo, py::arg("player"),
                 R"doc(
                        Retrieve player information.

                        Args:
                            player (int): The player number (starting from 1).

                        Returns:
                            PlayerInfo: The player's profile information.
                    )doc");


    // Factory function
    m.def("create_game_state", py::overload_cast<std::string, const DeviceInfo &>(&createGameState),
          py::arg("encrypted_key"), py::arg("device_info"),
          "Factory function to create a GameState instance");
}
