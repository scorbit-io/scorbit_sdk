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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file achievements_c.h
 * @brief Achievements: what game code does, and the artwork it can be handed.
 *
 * The SDK evaluates achievements by itself. It downloads the definitions for this machine, and on
 * every @ref sb_commit re-evaluates each one for every player who claimed a slot, from the game
 * state reported with @ref sb_set_score, @ref sb_add_mode, @ref sb_set_mode_completed,
 * @ref sb_add_game_event and the ball and player setters. It reports to Scorbit, retries, and
 * revalidates the definitions on its own. Game code does nothing else for achievements to work.
 *
 * When Scorbit grants a claimed player an achievement, the SDK raises
 * @ref SB_EVT_ACHIEVEMENT_UNLOCKED, which game code may present: see
 * @ref sb_event_achievement_unlocked. To present it with its artwork, choose the kind with
 * @ref sb_config_set_achievement_artwork.
 */

/**
 * @brief The artwork the SDK keeps ready to hand over with @ref SB_EVT_ACHIEVEMENT_UNLOCKED.
 *
 * - DMD frames are small: the SDK downloads every achievement's frame when it starts, and again
 *   only what changed when the definitions do, and keeps them in memory.
 * - Icons are large: the SDK downloads an icon only when its unlock is granted, and keeps the last
 *   few in memory.
 *
 * An unlock whose artwork is still being downloaded is raised once the download ends. One whose
 * artwork can't be downloaded, e.g. while offline, is raised without it.
 */
typedef enum {
    SB_ACHIEVEMENT_ARTWORK_NONE = 0,  ///< No artwork (the default): nothing is downloaded
    SB_ACHIEVEMENT_ARTWORK_FRAME = 1, ///< The DMD frame, for the machine's display (a PNG today)
    SB_ACHIEVEMENT_ARTWORK_IMAGE = 2, ///< The icon, a high-resolution image for LCD machines
} sb_achievement_artwork_t;

#ifdef __cplusplus
}
#endif
