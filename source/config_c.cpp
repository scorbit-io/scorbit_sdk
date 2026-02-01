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

#include "device_info.h"
#include <new>

sb_config_t sb_config_create(void)
{
    return new (std::nothrow) sb_config_s();
}

void sb_config_destroy(sb_config_t config)
{
    delete config;
}

void sb_config_set_provider(sb_config_t config, const char *provider)
{
    if (config && provider) {
        config->provider = provider;
    }
}

void sb_config_set_machine_id(sb_config_t config, int32_t machine_id)
{
    if (config) {
        config->machineId = machine_id;
    }
}

void sb_config_set_game_code_version(sb_config_t config, const char *version)
{
    if (config && version) {
        config->gameCodeVersion = version;
    }
}

void sb_config_set_hostname(sb_config_t config, const char *hostname)
{
    if (config) {
        config->hostname = hostname ? hostname : std::string {};
    }
}

void sb_config_set_uuid(sb_config_t config, const char *uuid)
{
    if (config) {
        config->uuid = uuid ? uuid : std::string {};
    }
}

void sb_config_set_serial_number(sb_config_t config, uint64_t serial_number)
{
    if (config) {
        config->serialNumber = serial_number;
    }
}

void sb_config_set_auto_download_player_pics(sb_config_t config, bool enable)
{
    if (config) {
        config->autoDownloadPlayerPics = enable;
    }
}

void sb_config_set_score_features(sb_config_t config, const char **features, size_t count,
                                  int version)
{
    if (config) {
        config->scoreFeatures.clear();
        if (features && count > 0) {
            config->scoreFeatures.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                config->scoreFeatures.emplace_back(features[i] ? features[i] : std::string {});
            }
        }
        config->scoreFeaturesVersion = version;
    }
}

void sb_config_set_encrypted_key(sb_config_t config, const char *encrypted_key)
{
    if (config) {
        config->encryptedKey = encrypted_key ? encrypted_key : std::string {};
        // Clear signer if encrypted key is set (mutually exclusive)
        if (!config->encryptedKey.empty()) {
            config->signerCallback = nullptr;
            config->signerUserData = nullptr;
        }
    }
}

void sb_config_set_signer(sb_config_t config, sb_signer_callback_t signer, void *user_data)
{
    if (config) {
        config->signerCallback = signer;
        config->signerUserData = user_data;
        // Clear encrypted key if signer is set (mutually exclusive)
        if (signer != nullptr) {
            config->encryptedKey.clear();
        }
    }
}

void sb_config_set_scorbitd_version(sb_config_t config, const char *version)
{
    if (config) {
        config->scorbitdVersion = version ? version : std::string {};
    }
}

void sb_config_set_scorbitd_platform_id(sb_config_t config, const char *platform_id)
{
    if (config) {
        config->scorbitdPlatformId = platform_id ? platform_id : std::string {};
    }
}

void sb_config_set_event_callback(sb_config_t config, sb_event_callback_t callback, void *user_data)
{
    using scorbit::detail::EventBase;

    if (config) {
        config->m_eventCallback = [callback, user_data](const EventBase &event) {
            if (callback) {
                callback(static_cast<const sb_event_t *>(&event), user_data);
            }
        };
    }
}

void sb_config_set_event_callback_cpp(sb_config_t config, sb_event_callback_t callback,
                                      void *cpp_callback)
{
    using scorbit::detail::EventBase;
    using scorbit::Event;
    using CppCallback = std::function<void(const Event &)>;

    if (config && cpp_callback) {
        // Take ownership of the heap-allocated callback
        config->m_cppCallbackStorage.reset(static_cast<CppCallback *>(cpp_callback));

        // Set up the event callback that uses the stored C++ callback
        auto *cbPtr = config->m_cppCallbackStorage.get();
        config->m_eventCallback = [callback, cbPtr](const EventBase &event) {
            if (callback && cbPtr && *cbPtr) {
                callback(static_cast<const sb_event_t *>(&event), cbPtr);
            }
        };
    }
}
