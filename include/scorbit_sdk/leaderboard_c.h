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

#include <scorbit_sdk/common_types_c.h>
#include <scorbit_sdk/export.h>
#include <scorbit_sdk/net_types_c.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sb_leaderboard_t sb_leaderboard_t;

/**
 * @brief Leaderboard callback function type.
 *
 * On success, @p leaderboard is non-NULL and valid only for the duration of the callback; do not
 * store the pointer or retain it after the callback returns. On failure, @p leaderboard is NULL.
 */
typedef void (*sb_leaderboard_callback_t)(sb_error_t error, sb_leaderboard_t *leaderboard,
                                          void *user_data);

/**
 * @brief Return the number of entries in the leaderboard.
 */
SCORBIT_SDK_EXPORT
size_t sb_leaderboard_entries_count(const sb_leaderboard_t *leaderboard);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_id(const sb_leaderboard_t *leaderboard, size_t index, uint64_t *id);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_rank(const sb_leaderboard_t *leaderboard, size_t index, int *rank);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_high_score(const sb_leaderboard_t *leaderboard, size_t index,
                                     sb_score_t *high_score);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_image(const sb_leaderboard_t *leaderboard, size_t index,
                                const char **image);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_reaction_count(const sb_leaderboard_t *leaderboard, size_t index,
                                         int *reaction_count);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_score_count(const sb_leaderboard_t *leaderboard, size_t index,
                                      int *score_count);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_is_nfc_verified(const sb_leaderboard_t *leaderboard, size_t index,
                                          bool *is_nfc_verified);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_is_verified(const sb_leaderboard_t *leaderboard, size_t index,
                                      bool *is_verified);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_is_vpin(const sb_leaderboard_t *leaderboard, size_t index, bool *is_vpin);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_created(const sb_leaderboard_t *leaderboard, size_t index,
                                  const char **created);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_player_id(const sb_leaderboard_t *leaderboard, size_t index,
                                    const char **id);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_player_username(const sb_leaderboard_t *leaderboard, size_t index,
                                          const char **username);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_player_display_name(const sb_leaderboard_t *leaderboard, size_t index,
                                              const char **display_name);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_player_initials(const sb_leaderboard_t *leaderboard, size_t index,
                                          const char **initials);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_player_avatar(const sb_leaderboard_t *leaderboard, size_t index,
                                        const char **avatar);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_player_follower_count(const sb_leaderboard_t *leaderboard, size_t index,
                                                int *follower_count);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_player_following_count(const sb_leaderboard_t *leaderboard, size_t index,
                                                 int *following_count);

SCORBIT_SDK_EXPORT
bool sb_leaderboard_entry_player_last_login(const sb_leaderboard_t *leaderboard, size_t index,
                                            const char **last_login);

#ifdef __cplusplus
}
#endif
