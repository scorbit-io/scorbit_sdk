/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "achievements_c.h"
#include "common_types_c.h"

#include <cstdint>
#include <string>
#include <vector>

namespace scorbit {

/** @brief The artwork handed over with an unlock; see @ref sb_achievement_artwork_t. */
enum class AchievementArtwork {
    None = SB_ACHIEVEMENT_ARTWORK_NONE,
    Frame = SB_ACHIEVEMENT_ARTWORK_FRAME,
    Image = SB_ACHIEVEMENT_ARTWORK_IMAGE,
};

/** @brief The content of an @ref EventType::AchievementUnlocked event. */
struct AchievementUnlock {
    std::string key;
    sb_player_t player {0};
    std::string name;
    std::string description;
    std::vector<uint8_t> artwork; ///< Empty when there is none; see @ref AchievementArtwork
};

} // namespace scorbit
