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

#pragma once

#include "config_c.h"
#include "event.h"
#include "net_types.h"
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace scorbit {

using EventCallback = std::function<void(const Event &event)>;
// SaveKeyCallback and LoadKeyCallback are defined in net_types.h

/**
 * @brief Configuration class for creating game state.
 *
 * This class wraps the C sb_config_t handle and provides a convenient C++ interface
 * for configuring the SDK. The class is designed to be ABI-stable - new configuration
 * options can be added in future SDK versions without breaking existing code.
 *
 * Example usage:
 * @code
 * scorbit::Config config;
 * config.setProvider("myprovider");
 * config.setMachineId(4419);
 * config.setGameCodeVersion("1.0.0");
 *
 * auto gameState = scorbit::createGameState(encryptedKey, config);
 * @endcode
 */
class Config {
public:
    /**
     * @brief Construct a new Config object with default values.
     */
    Config()
        : m_handle(sb_config_create(), sb_config_destroy)
    {
    }

    /**
     * @brief Check if the config is valid.
     * @return true if the config was created successfully.
     */
    bool isValid() const { return m_handle != nullptr; }

    /**
     * @brief Get the underlying C handle.
     * @return The sb_config_t handle.
     */
    sb_config_t handle() const { return m_handle.get(); }

    // ---- Setters with method chaining support ----

    /**
     * @brief Set the provider name.
     * @param provider The provider name, e.g., "scorbitron", "vpin". Mandatory.
     * @return Reference to this Config for method chaining.
     */
    Config &setProvider(const std::string &provider)
    {
        sb_config_set_provider(m_handle.get(), provider.c_str());
        return *this;
    }

    /**
     * @brief Set the machine ID.
     * @param machineId The Machine ID assigned by Scorbit. Mandatory for manufacturers.
     * @return Reference to this Config for method chaining.
     */
    Config &setMachineId(int32_t machineId)
    {
        sb_config_set_machine_id(m_handle.get(), machineId);
        return *this;
    }

    /**
     * @brief Set the game code version.
     * @param version The game code version string, e.g., "1.12.3". Mandatory.
     * @return Reference to this Config for method chaining.
     */
    Config &setGameCodeVersion(const std::string &version)
    {
        sb_config_set_game_code_version(m_handle.get(), version.c_str());
        return *this;
    }

    /**
     * @brief Set the server hostname.
     * @param hostname The hostname of the server. Optional.
     *                 Examples: "production", "staging", "https://api.scorbit.io"
     * @return Reference to this Config for method chaining.
     */
    Config &setHostname(const std::string &hostname)
    {
        sb_config_set_hostname(m_handle.get(), hostname.c_str());
        return *this;
    }

    /**
     * @brief Set the Centrifugo server hostname.
     * @param hostname The hostname of the Centrifugo server. Optional.
     *                 If not set, it will be derived from the main hostname.
     * @return Reference to this Config for method chaining.
     */
    Config &setCfHostname(const std::string &hostname)
    {
        sb_config_set_cf_hostname(m_handle.get(), hostname.c_str());
        return *this;
    }

    /**
     * @brief Set the device UUID.
     * @param uuid The device's UUID. Optional.
     * @return Reference to this Config for method chaining.
     */
    Config &setUuid(const std::string &uuid)
    {
        sb_config_set_uuid(m_handle.get(), uuid.c_str());
        return *this;
    }

    /**
     * @brief Set the device serial number.
     * @param serialNumber The serial number. Optional, use 0 if unavailable.
     * @return Reference to this Config for method chaining.
     */
    Config &setSerialNumber(uint64_t serialNumber)
    {
        sb_config_set_serial_number(m_handle.get(), serialNumber);
        return *this;
    }

    /**
     * @brief Enable or disable automatic player picture downloads.
     * @param enable If true, player pictures are downloaded automatically.
     * @return Reference to this Config for method chaining.
     */
    Config &setAutoDownloadPlayerPics(bool enable)
    {
        sb_config_set_auto_download_player_pics(m_handle.get(), enable);
        return *this;
    }

    /**
     * @brief Set score features.
     * @param features Vector of feature strings.
     * @param version The version number for the score features.
     * @return Reference to this Config for method chaining.
     */
    Config &setScoreFeatures(const std::vector<std::string> &features, int version)
    {
        std::vector<const char *> ptrs;
        ptrs.reserve(features.size());
        for (const auto &f : features) {
            ptrs.push_back(f.c_str());
        }
        sb_config_set_score_features(m_handle.get(), ptrs.data(), ptrs.size(), version);
        return *this;
    }

    // ---- Authentication (one of these must be set) ----

    /**
     * @brief Set the encrypted key for authentication.
     *
     * Use this for machines without TPM. The encrypted key is generated using
     * the encrypt_tool provided with the SDK.
     *
     * @param encryptedKey The encrypted private key string.
     * @return Reference to this Config for method chaining.
     */
    Config &setEncryptedKey(const std::string &encryptedKey)
    {
        sb_config_set_encrypted_key(m_handle.get(), encryptedKey.c_str());
        return *this;
    }

    /**
     * @brief Set the signer callback for TPM-based authentication.
     * @param signer The callback function to sign digests.
     * @param userData User data passed to the signer callback.
     * @return Reference to this Config for method chaining.
     */
    Config &setSigner(sb_signer_callback_t signer, void *userData = nullptr)
    {
        sb_config_set_signer(m_handle.get(), signer, userData);
        return *this;
    }

    // ---- Event callback ----

    /**
     * @brief Set the event callback for handling incoming events.
     *
     * Events include game start requests, credit addition requests, and other
     * notifications from the Scorbit backend.
     *
     * @param callback The callback function to handle events.
     * @return Reference to this Config for method chaining.
     */
    Config &setEventCallback(EventCallback callback)
    {
        // Store callback locally; ownership will be moved to GameState
        m_eventCallbackStorage = std::make_unique<EventCallback>(std::move(callback));
        sb_config_set_event_callback(m_handle.get(), &Config::event_callback_c,
                                     m_eventCallbackStorage.get());
        return *this;
    }

    // ---- Key persistence callbacks ----

    /**
     * @brief Set the callback for saving a key to persistent storage.
     *
     * The SDK will call this callback when it needs to persist a key.
     *
     * @param callback The callback function to save the key.
     * @return Reference to this Config for method chaining.
     */
    Config &setSaveKeyCallback(SaveKeyCallback callback)
    {
        // Store callback locally; ownership will be moved to GameState
        m_saveKeyCallbackStorage = std::make_unique<SaveKeyCallback>(std::move(callback));
        sb_config_set_save_key_callback(m_handle.get(), &Config::save_key_callback_c,
                                        m_saveKeyCallbackStorage.get());
        return *this;
    }

    /**
     * @brief Set the callback for loading a key from persistent storage.
     *
     * The SDK will call this callback when it needs to load a previously saved key.
     * If no key is stored, the callback should return an empty string.
     *
     * @param callback The callback function to load the key.
     * @return Reference to this Config for method chaining.
     */
    Config &setLoadKeyCallback(LoadKeyCallback callback)
    {
        // Store callback locally; ownership will be moved to GameState
        m_loadKeyCallbackStorage = std::make_unique<LoadKeyCallback>(std::move(callback));
        sb_config_set_load_key_callback(m_handle.get(), &Config::load_key_callback_c,
                                        m_loadKeyCallbackStorage.get());
        return *this;
    }

    // ------------------------------------------------------------------------------------------------
    // Below is not a public interface, for internal use only!
    // ------------------------------------------------------------------------------------------------

    Config &setScorbitdVersion(const std::string &version)
    {
        sb_config_set_scorbitd_version(m_handle.get(), version.c_str());
        return *this;
    }

    Config &setScorbitdPlatformId(const std::string &platformId)
    {
        sb_config_set_scorbitd_platform_id(m_handle.get(), platformId.c_str());
        return *this;
    }

    Config &setMachineTitle(const std::string &title)
    {
        sb_config_set_machine_title(m_handle.get(), title.c_str());
        return *this;
    }

    // Move semantics
    Config(Config &&) = default;
    Config &operator=(Config &&) = default;

    // No copy (unique ownership of handle)
    Config(const Config &) = delete;
    Config &operator=(const Config &) = delete;

private:
    // Allow GameState to access private members for callback transfer
    friend class GameState;

    static void event_callback_c(const sb_event_t *event, void *user_data)
    {
        auto *cb = static_cast<EventCallback *>(user_data);
        if (cb && *cb && event) {
            (*cb)(Event(const_cast<sb_event_t *>(event)));
        }
    }

    static void save_key_callback_c(const char *key, void *user_data)
    {
        auto *cb = static_cast<SaveKeyCallback *>(user_data);
        if (cb && *cb && key) {
            (*cb)(std::string(key));
        }
    }

    static int load_key_callback_c(char *buffer, size_t buffer_size, void *user_data)
    {
        auto *cb = static_cast<LoadKeyCallback *>(user_data);
        if (cb && *cb) {
            std::string key = (*cb)();
            if (key.empty()) {
                return 0;
            }
            if (key.size() + 1 > buffer_size) {
                return -1; // Buffer too small
            }
            std::memcpy(buffer, key.c_str(), key.size() + 1);
            return static_cast<int>(key.size());
        }
        return 0;
    }

    // Callback storage - allocated on the client side (header code), ownership
    // transferred to GameState in createGameState(). This ensures new/delete
    // both happen on the same side of the shared library boundary.
    std::unique_ptr<EventCallback> m_eventCallbackStorage;
    std::unique_ptr<SaveKeyCallback> m_saveKeyCallbackStorage;
    std::unique_ptr<LoadKeyCallback> m_loadKeyCallbackStorage;

    std::unique_ptr<std::remove_pointer<sb_config_t>::type, decltype(&sb_config_destroy)> m_handle;
};

} // namespace scorbit
