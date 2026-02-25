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

#include <scorbit_sdk/game_state_c.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

// clazy:excludeall=non-pod-global-static

using json = nlohmann::json;

namespace {

int dummySigner(uint8_t signature[SB_SIGNATURE_MAX_LENGTH], size_t *signature_len,
                const uint8_t digest[SB_DIGEST_LENGTH], void *user_data)
{
    (void)signature;
    (void)signature_len;
    (void)digest;
    (void)user_data;
    return 0;
}

// Valid P-256 test private key (32 bytes = 64 hex chars)
constexpr auto TEST_PRIVATE_KEY_HEX =
        "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2";
constexpr auto TEST_UUID = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab";
constexpr uint64_t TEST_SERIAL = 12345;

std::string g_savedKey;

void testSaveKeyCallback(const char *key, void * /*user_data*/)
{
    g_savedKey = key;
}

int testLoadKeyCallback(char *buffer, size_t buffer_size, void * /*user_data*/)
{
    json j;
    j["private_key"] = TEST_PRIVATE_KEY_HEX;
    j["uuid"] = TEST_UUID;
    j["serial_number"] = TEST_SERIAL;
    std::string data = j.dump();

    if (data.size() >= buffer_size) {
        return -1;
    }
    std::copy(data.begin(), data.end(), buffer);
    buffer[data.size()] = '\0';
    return static_cast<int>(data.size());
}

int testEmptyLoadKeyCallback(char * /*buffer*/, size_t /*buffer_size*/, void * /*user_data*/)
{
    return 0;
}

} // namespace

TEST_CASE("Create and destroy game state with signer callback", "[GameState]")
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "vscorbitron");
    sb_config_set_machine_id(cfg, 4419);
    sb_config_set_game_code_version(cfg, "0.1.0");
    sb_config_set_signer(cfg, dummySigner, nullptr);

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("Create game state with software key from callback", "[GameState][SoftKey]")
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "testprovider");
    sb_config_set_machine_id(cfg, 1234);
    sb_config_set_game_code_version(cfg, "1.0.0");
    sb_config_set_hostname(cfg, "staging");
    sb_config_set_encrypted_key(cfg, "dummy_encrypted_key");
    sb_config_set_save_key_callback(cfg, testSaveKeyCallback, nullptr);
    sb_config_set_load_key_callback(cfg, testLoadKeyCallback, nullptr);

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("Create game state returns nullptr with no auth", "[GameState]")
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "testprovider");
    sb_config_set_machine_id(cfg, 1234);
    sb_config_set_game_code_version(cfg, "1.0.0");

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h == nullptr);

    sb_config_destroy(cfg);
}

TEST_CASE("Create game state returns nullptr with null config", "[GameState]")
{
    sb_game_handle_t h = sb_create_game_state(nullptr);
    REQUIRE(h == nullptr);
}

TEST_CASE("Create game state fails when load callback returns empty", "[GameState][SoftKey]")
{
    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "testprovider");
    sb_config_set_machine_id(cfg, 1234);
    sb_config_set_game_code_version(cfg, "1.0.0");
    sb_config_set_hostname(cfg, "staging");
    sb_config_set_encrypted_key(cfg, "dummy_encrypted_key");
    sb_config_set_save_key_callback(cfg, testSaveKeyCallback, nullptr);
    sb_config_set_load_key_callback(cfg, testEmptyLoadKeyCallback, nullptr);

    // No saved key and provisionNewKey will fail (no real server)
    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h == nullptr);

    sb_config_destroy(cfg);
}
