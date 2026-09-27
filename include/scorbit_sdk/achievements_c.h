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

#include <scorbit_sdk/common_types_c.h>
#include <scorbit_sdk/export.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What happened to an achievement, as reported by @ref SB_EVT_ACHIEVEMENT_UPDATED.
 *
 * The SDK evaluates achievements automatically on every @ref sb_commit. An achievement the machine
 * decides itself is reported as @ref SB_ACHIEVEMENT_UNLOCKED_LOCALLY at once and may be presented
 * immediately; the server's verdict follows as @ref SB_ACHIEVEMENT_CONFIRMED, @ref
 * SB_ACHIEVEMENT_ALREADY_HELD or @ref SB_ACHIEVEMENT_RETRACTED.
 */
typedef enum {
    /** Progress changed; reported at ball boundaries. */
    SB_ACHIEVEMENT_PROGRESS = 0,
    /** The machine decided the player earned it. It may be presented now. */
    SB_ACHIEVEMENT_UNLOCKED_LOCALLY = 1,
    /** The server granted it. */
    SB_ACHIEVEMENT_CONFIRMED = 2,
    /** The server says the player already held it: do not celebrate again. */
    SB_ACHIEVEMENT_ALREADY_HELD = 3,
    /** The server refused an unlock the machine decided: a presented unlock must be retracted. */
    SB_ACHIEVEMENT_RETRACTED = 4,
} sb_achievement_status_t;

#ifdef __cplusplus
}
#endif
