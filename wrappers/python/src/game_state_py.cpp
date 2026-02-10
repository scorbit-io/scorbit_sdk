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
                    // We don't delete ptr here, otherwise it will cause deadlock and will never
                    // return
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

    // GameStateOrigin enum
    py::enum_<GameStartOrigin>(m, "GameStartOrigin", "Enumeration of game start origins.")
            .value("StartButton", GameStartOrigin::StartButton,
                   "Game started by machine when player presses Start button.")
            .value("FromLobby", GameStartOrigin::FromLobby,
                   "Game started explicitly via Scorbit app request. See "
                   "EventType.GameStartRequested.");

    // Capability enum
    py::enum_<Capability>(m, "Capability", "Enumeration of device capabilities.")
            .value("StartGame", Capability::StartGame,
                   "Game can be started remotely.")
            .value("CreditDrop", Capability::CreditDrop,
                   "Machine can accept coin drop events.");

    // Bind EventType enum
    py::enum_<EventType>(m, "EventType", "Enumeration of event types.")
            .value("GameStartRequested", EventType::GameStartRequested,
                   "Game start requested from mobile app.")
            .value("CreditsAddRequested", EventType::CreditsAddRequested,
                   "Credits add requested from mobile app.")
            .value("CreditsStatusRequested", EventType::CreditsStatusRequested,
                   "Credits status requested.")
            .value("None", EventType::None, "No event (should not be used).")
            .value("ConfigReceived", EventType::ConfigReceived,
                   "Configuration received from server.");

    // Event class
    py::class_<Event>(m, "Event", R"doc(
            Event class for handling game events.

            This class provides methods to process different types of events that can occur during
            gameplay, such as game start requests or credit addition requests.
        )doc")
            .def("type", &Event::type, R"doc(
                    Get the type of the event.

                    Returns:
                        EventType: The type of the event.
                )doc")
            .def(
                    "get_game_start_requested",
                    [](const Event &self) {
                        int players_count = 0;
                        bool success = self.getGameStartRequested(players_count);
                        return std::make_tuple(success, players_count);
                    },
                    R"doc(
                        Process a game start requested event.

                        This function processes an event representing a game start request.
                        The event type must be EventType.GameStartRequested, otherwise the function
                        returns (False, 0).

                        Returns:
                            tuple: (success, players_count)
                                - success (bool): True on success, or False if an error occurs (e.g., wrong event type was given).
                                - players_count (int): The number of players requested (valid only if success=True).

                        Example:
                            if event.type() == scorbit.EventType.GameStartRequested:
                                success, players_count = event.get_game_start_requested()
                                if success:
                                    print(f"Game start requested with {players_count} players")
                    )doc")
            .def(
                    "get_credits_add_requested",
                    [](const Event &self) {
                        int credits_to_add = 0;
                        std::string transaction;
                        bool success = self.getCreditsAddRequested(credits_to_add, transaction);
                        return std::make_tuple(success, credits_to_add, transaction);
                    },
                    R"doc(
                        Process a credits add requested event.

                        This function processes an event representing a credits add request.
                        The event type must be EventType.CreditsAddRequested, otherwise the function
                        returns (False, 0).

                        After adding requested credits, call GameState.set_credits_dropped to notify the system.

                        Returns:
                            tuple: (success, credits_to_add, transaction)
                                - success (bool): True on success, or False if an error occurs (e.g., wrong event type was given).
                                - credits_to_add (int): The number of credits to add (valid only if success=True).
                                - transaction (str): The transaction identifier associated with the credit add request (valid only if success=True).

                        Example:
                            if event.type() == scorbit.EventType.CreditsAddRequested:
                                success, credits, transaction = event.get_credits_add_requested()
                                if success:
                                    print(f"Credits add requested: {credits}, transaction: {transaction}")
                    )doc")
            .def(
                    "event_config_received",
                    [](const Event &self) {
                        std::string config_json;
                        bool success = self.eventConfigReceived(config_json);
                        return std::make_tuple(success, config_json);
                    },
                    R"doc(
                        Process a configuration received event.

                        This function processes an event representing configuration data received from the server.
                        The event type must be EventType.ConfigReceived, otherwise the function returns (False, "").

                        Returns:
                            tuple: (success, config_json)
                                - success (bool): True on success, or False if an error occurs (e.g., wrong event type was given).
                                - config_json (str): The configuration JSON data (valid only if success=True).

                        Example:
                            if event.type() == scorbit.EventType.ConfigReceived:
                                success, config = event.event_config_received()
                                if success:
                                    print(f"Config received: {config}")
                    )doc")
            .def(
                    "get_config_payments_enabled",
                    [](const Event &self) {
                        bool payments_enabled = false;
                        bool success = self.getConfigPaymentsEnabled(payments_enabled);
                        return std::make_tuple(success, payments_enabled);
                    },
                    R"doc(
                        Extract payments enabled status from a config received event.

                        This function extracts the payments_enabled field from a config received event.
                        The event type must be EventType.ConfigReceived, otherwise the function
                        returns (False, False).

                        Returns:
                            tuple: (success, payments_enabled)
                                - success (bool): True on success, or False if an error occurs.
                                - payments_enabled (bool): Whether payments are enabled (valid only if success=True).

                        Example:
                            if event.type() == scorbit.EventType.ConfigReceived:
                                success, payments_enabled = event.get_config_payments_enabled()
                                if success:
                                    print(f"Payments enabled: {payments_enabled}")
                    )doc");

#ifdef SCORBIT_LOGGER_CALLBACK
    // Wrapper for addLoggerCallback
    m.def(
            "add_logger_callback",
            [](py::function callback, size_t max_length) {
                auto safe_callback =
                        makeSafeCallback([callback = std::move(callback)](
                                                 const std::string &message, LogLevel level,
                                                 const char *file, int line, int64_t timestamp) {
                            callback(message, level, file, line, timestamp);
                        });

                // Call the C++ method with our wrapped callback.
                addLoggerCallback(std::move(safe_callback), max_length);
            },
            py::arg("callback"),
            py::arg("max_length"),
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
                    max_length (int):
                        Maximum length of the log message that will be passed to the callback.

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
#endif // SCORBIT_LOGGER_CALLBACK

    // Config class - ABI-stable configuration (recommended)
    py::class_<Config>(m, "Config", R"doc(
            ABI-stable configuration class for creating game state.

            This class provides a stable interface for configuring the SDK. New configuration
            options can be added in future SDK versions without breaking existing code.

            Example:
                >>> config = scorbit.Config()
                >>> config.set_provider("myprovider")
                >>> config.set_machine_id(4419)
                >>> config.set_game_code_version("1.0.0")
                >>> game_state = scorbit.create_game_state(encrypted_key, config)

            Note:
                This is the recommended way to configure the SDK for new code.
        )doc")
            .def(py::init<>(), "Creates a new Config instance with default values.")

            .def("is_valid", &Config::isValid,
                 "Returns True if the config was created successfully.")

            .def(
                    "set_provider",
                    [](Config &self, const std::string &provider) -> Config & {
                        return self.setProvider(provider);
                    },
                    py::arg("provider"), py::return_value_policy::reference,
                    "Set the provider name. Mandatory.")

            .def(
                    "set_machine_id",
                    [](Config &self, int32_t machineId) -> Config & {
                        return self.setMachineId(machineId);
                    },
                    py::arg("machine_id"), py::return_value_policy::reference,
                    "Set the machine ID. Mandatory for manufacturers.")

            .def(
                    "set_game_code_version",
                    [](Config &self, const std::string &version) -> Config & {
                        return self.setGameCodeVersion(version);
                    },
                    py::arg("version"), py::return_value_policy::reference,
                    "Set the game code version. Mandatory.")

            .def(
                    "set_hostname",
                    [](Config &self, const std::string &hostname) -> Config & {
                        return self.setHostname(hostname);
                    },
                    py::arg("hostname"), py::return_value_policy::reference,
                    "Set the server hostname. Optional. Examples: 'production', 'staging'.")

            .def(
                    "set_uuid",
                    [](Config &self, const std::string &uuid) -> Config & {
                        return self.setUuid(uuid);
                    },
                    py::arg("uuid"), py::return_value_policy::reference,
                    "Set the device UUID. Optional.")

            .def(
                    "set_serial_number",
                    [](Config &self, uint64_t serialNumber) -> Config & {
                        return self.setSerialNumber(serialNumber);
                    },
                    py::arg("serial_number"), py::return_value_policy::reference,
                    "Set the device serial number. Optional.")

            .def(
                    "set_auto_download_player_pics",
                    [](Config &self, bool enable) -> Config & {
                        return self.setAutoDownloadPlayerPics(enable);
                    },
                    py::arg("enable"), py::return_value_policy::reference,
                    "Enable or disable automatic player picture downloads.")

            .def(
                    "set_score_features",
                    [](Config &self, const std::vector<std::string> &features,
                       int version) -> Config & { return self.setScoreFeatures(features, version); },
                    py::arg("features"), py::arg("version"), py::return_value_policy::reference,
                    "Set score features and version.")

            .def(
                    "set_encrypted_key",
                    [](Config &self, const std::string &key) -> Config & {
                        return self.setEncryptedKey(key);
                    },
                    py::arg("encrypted_key"), py::return_value_policy::reference,
                    R"doc(
                        Set the encrypted key for authentication.

                        Use this for machines without TPM. The encrypted key is generated using
                        the encrypt_tool provided with the SDK.

                        Args:
                            encrypted_key: The encrypted private key string.
                    )doc")

            .def(
                    "set_event_callback",
                    [](Config &self, py::function callback) -> Config & {
                        // Use makeSafeCallback to handle thread safety and GIL management
                        return self.setEventCallback(makeSafeCallback(std::move(callback)));
                    },
                    py::arg("callback"), py::return_value_policy::reference,
                    R"doc(
                        Set the event callback function.

                        This function sets a callback that will be invoked when game events occur. The callback
                        receives an Event object containing information about the event.

                        Note:
                            The callback function is invoked asynchronously when events occur, running
                            in a separate thread from the main calling thread. The wrapper automatically
                            handles thread safety and GIL management.

                            This callback must be set before creating the game state.

                        Args:
                            callback (Callable[[scorbit.Event], None]): A callback function that receives an Event
                                object when a game event occurs.

                        Example:
                            def on_event(event):
                                print(f"Game event: {event}")

                            config = scorbit.Config()
                            config.set_event_callback(on_event)
                    )doc")

            .def(
                    "set_save_key_callback",
                    [](Config &self, py::function callback) -> Config & {
                        // Wrap Python callback to handle GIL
                        auto safeCallback = makeSafeCallback(
                                [callback = std::move(callback)](const std::string &key) {
                                    callback(key);
                                });
                        return self.setSaveKeyCallback(std::move(safeCallback));
                    },
                    py::arg("callback"), py::return_value_policy::reference,
                    R"doc(
                        Set the callback for saving a key to persistent storage.

                        The SDK will call this callback when it needs to persist a key. The implementation
                        should save the key to a secure persistent storage location (e.g., file, database).

                        Note:
                            The callback function may be invoked asynchronously from a separate thread.
                            The wrapper automatically handles thread safety and GIL management.

                        Args:
                            callback (Callable[[str], None]): A callback function that receives the key
                                string to save.

                        Example:
                            def save_key(key):
                                with open('key.txt', 'w') as f:
                                    f.write(key)

                            config = scorbit.Config()
                            config.set_save_key_callback(save_key)
                    )doc")

            .def(
                    "set_load_key_callback",
                    [](Config &self, py::function callback) -> Config & {
                        // Create a shared_ptr to store the Python callback with GIL-safe deleter
                        auto callbackPtr = std::shared_ptr<py::function>(
                                new py::function(std::move(callback)), [](py::function *ptr) {
                                    if (!Py_IsInitialized()) {
                                        return;
                                    }
                                    py::gil_scoped_acquire gil;
                                    delete ptr;
                                });

                        // Create a C++ callback that acquires GIL and returns the result
                        auto loadCallback = [callbackPtr]() -> std::string {
                            if (!Py_IsInitialized()) {
                                return "";
                            }
                            py::gil_scoped_acquire gil;
                            try {
                                py::object result = (*callbackPtr)();
                                if (result.is_none()) {
                                    return "";
                                }
                                return result.cast<std::string>();
                            } catch (...) {
                                return "";
                            }
                        };
                        return self.setLoadKeyCallback(std::move(loadCallback));
                    },
                    py::arg("callback"), py::return_value_policy::reference,
                    R"doc(
                        Set the callback for loading a key from persistent storage.

                        The SDK will call this callback when it needs to load a previously saved key.
                        The implementation should read the key from persistent storage and return it.

                        Note:
                            The callback function may be invoked asynchronously from a separate thread.
                            The wrapper automatically handles thread safety and GIL management.

                        Args:
                            callback (Callable[[], str]): A callback function that returns the stored key
                                string, or an empty string (or None) if no key is stored.

                        Example:
                            def load_key():
                                try:
                                    with open('key.txt', 'r') as f:
                                        return f.read()
                                except FileNotFoundError:
                                    return ""

                            config = scorbit.Config()
                            config.set_load_key_callback(load_key)
                    )doc");

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

            .def("set_game_started", &GameState::setGameStarted, py::arg("origin"), R"doc(
                Mark the game as started.

                This function sets the game session active, resetting the game state. It initializes the
                active player to Player 1 with a score of 0, and sets the current ball to 1.

                If the game is already in progress, this function has no effect.

                Note:
                    After starting the game, `commit()` must be called to notify the cloud. Optionally,
                    before calling `commit()`, the active player, scores, modes, or current ball can be modified.

                Args:
                    origin (GameStartOrigin): The origin of the game start. This indicates how the game was
                        started, such as by pressing the start button or via a request from the lobby
                        (mobile app). See `GameStartOrigin` for details and `EventType.GameStartRequested`
                        event.
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
                    )doc")

            .def("set_capabilities", &GameState::setCapabilities, py::arg("capabilities"),
                 R"doc(
                        Sets the device capabilities.

                        Configures the device with the features it supports. The `capabilities`
                        argument should be a bitwise OR of one or more values from `scorbit.Capability`.

                        If this function is not called, all capabilities are assumed to be disabled
                        by default.

                        Parameters
                        ----------
                        capabilities : int
                            Bitwise OR of capability flags supported by the device.
                    )doc")

            .def("set_credits_dropped", &GameState::setCreditsDropped, py::arg("credits"),
                 py::arg("transaction"), py::arg("success"),
                 R"doc(
                        Sets the number of credits dropped into the machine.

                        This function should be called when EventType.CreditsAddRequested event
                        received and credits added to machine. It notifies the Scorbit cloud service
                        and mobile app dropped credits count and if it was successful.

                        Note: it should not be called if physical coins dropped in to machine.

                        Args:
                            credits (int): The number of credits that were actually dropped.
                            transaction (str): The transaction identifier associated with the credit drop request.
                            success (bool): Indicates whether the credit drop was successful.
                    )doc")

            .def("set_credits_status", &GameState::setCreditsStatus, py::arg("free_play"),
                 py::arg("credits"), py::arg("max_credits"), py::arg("pricing"),
                 R"doc(
                        Sets the current credits status.

                        This function should be called:
                        1. when EventType.CreditsStatusRequested event is received.
                        2. when the credits number changed in the machine (added or subtracted).

                        Args:
                            free_play (bool): True if the machine is in free play mode; False otherwise.
                            credits (int): The current number of credits available in the machine.
                            max_credits (int): The maximum number of credits allowed in the machine.
                            pricing (str): For future use. Currently should be set to an empty string.
                    )doc");

    // Factory function - primary API
    m.def(
            "create_game_state",
            [](Config &config) { return createGameState(config); },
            py::arg("config"),
            R"doc(
                Factory function to create a GameState instance using Config.

                This is the recommended way to create a GameState. The Config object must have:
                - Required settings: provider, machine_id, game_code_version
                - Authentication: set_encrypted_key()

                Args:
                    config: The Config object with SDK settings and authentication.

                Returns:
                    A new GameState instance.

                Example:
                    >>> config = scorbit.Config()
                    >>> config.set_provider("myprovider")
                    >>> config.set_machine_id(4419)
                    >>> config.set_game_code_version("1.0.0")
                    >>> config.set_encrypted_key(encrypted_key)
                    >>> game_state = scorbit.create_game_state(config)
            )doc");
}
