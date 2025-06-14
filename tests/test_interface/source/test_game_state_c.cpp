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

// clazy:excludeall=non-pod-global-static

int signer(uint8_t signature[SB_SIGNATURE_MAX_LENGTH], size_t *signature_len,
           const uint8_t digest[SB_DIGEST_LENGTH], void *user_data)
{
    // Sign digest using given key and store the result in signature, the length of signature store
    // in signature_len
    (void)signature;
    (void)signature_len;
    (void)digest;
    (void)user_data;
    return 0;
}

TEST_CASE("Create and destroy game state")
{
    sb_device_info_t device_info = {"vscorbitron", 4419, "0.1.0", nullptr, nullptr, 0,
                                    false,         NULL, 0,       0};
    sb_game_handle_t h = sb_create_game_state(&signer, nullptr, &device_info);
    REQUIRE(h != nullptr);

    sb_destroy_game_state(h);
}
