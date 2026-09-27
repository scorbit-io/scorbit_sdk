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

#include <scorbit_sdk/achievements_c.h>
#include "game_state_c_internal.h"
#include "game_state_impl.h"
#include <memory>
#include <string>
#include <vector>

using namespace scorbit::detail;
using namespace scorbit::detail::achievements;

namespace {

/**
 * What the pointers handed out by this API point into, per calling thread: they stay valid until
 * the next call of this API from the same thread.
 */
struct Pinned {
    std::shared_ptr<const AchievementsView> view;
    std::vector<uint8_t> frame;
};

thread_local Pinned t_pinned;

const AchievementsView &pinView(sb_game_handle_t handle)
{
    t_pinned.view = gameStateOf(handle).achievements().view();
    return *t_pinned.view;
}

const Definition *findDefinition(const AchievementsView &view, const char *key)
{
    return key && view.definitions ? view.definitions->find(key) : nullptr;
}

const PlayerTracker::Record *findRecord(const AchievementsView &view, sb_player_t player,
                                        const char *key)
{
    if (!key) {
        return nullptr;
    }
    const auto playerIt = view.players.find(player);
    if (playerIt == view.players.end()) {
        return nullptr;
    }
    const auto recordIt = playerIt->second.records.find(key);
    return recordIt == playerIt->second.records.end() ? nullptr : &recordIt->second;
}

void fill(const Definition &definition, sb_achievement_t *out)
{
    out->key = definition.key.c_str();
    out->name = definition.name.c_str();
    out->description = definition.description.c_str();
    out->scope = static_cast<sb_achievement_scope_t>(definition.scope);
    out->evaluation = static_cast<sb_achievement_evaluation_t>(definition.evaluation);
    out->is_trophy = definition.isTrophy;
    out->visible = definition.visible;
    out->obscure = definition.obscure;
    out->notify_when_achieved = definition.notifyWhenAchieved;
    out->icon_url = definition.iconUrl.c_str();
    out->obscure_image_url = definition.obscureImageUrl.c_str();
    out->has_group = definition.groupId.has_value();
    out->group_id = definition.groupId.value_or(0);
    out->display_position = definition.displayPosition.value_or(0);
    out->rules_count = definition.rules.size();
}

// The public enums mirror the domain ones value for value
static_assert(static_cast<int>(RuleType::Achievement) == SB_ACHIEVEMENT_RULE_ACHIEVEMENT);
static_assert(static_cast<int>(RuleType::TimerBetween) == SB_ACHIEVEMENT_RULE_TIMER_BETWEEN);
static_assert(static_cast<int>(Comparison::Ne) == SB_ACHIEVEMENT_NE);
static_assert(static_cast<int>(EvaluationClass::Unlimited) == SB_ACHIEVEMENT_UNLIMITED);
static_assert(static_cast<int>(Scope::Global) == SB_ACHIEVEMENT_SCOPE_GLOBAL);

} // namespace

size_t sb_achievements_count(sb_game_handle_t handle)
{
    const auto &view = pinView(handle);
    return view.definitions ? view.definitions->size() : 0;
}

bool sb_achievement_at(sb_game_handle_t handle, size_t index, sb_achievement_t *achievement)
{
    const auto &view = pinView(handle);
    if (!achievement || !view.definitions || index >= view.definitions->size()) {
        return false;
    }
    fill(view.definitions->all()[index], achievement);
    return true;
}

bool sb_achievement_find(sb_game_handle_t handle, const char *key, sb_achievement_t *achievement)
{
    const auto *definition = findDefinition(pinView(handle), key);
    if (!definition || !achievement) {
        return false;
    }
    fill(*definition, achievement);
    return true;
}

bool sb_achievement_rule_at(sb_game_handle_t handle, const char *key, size_t index,
                            sb_achievement_rule_t *rule)
{
    const auto *definition = findDefinition(pinView(handle), key);
    if (!definition || !rule || index >= definition->rules.size()) {
        return false;
    }
    const auto &r = definition->rules[index];
    rule->type = static_cast<sb_achievement_rule_type_t>(r.type);
    rule->comparison = static_cast<sb_achievement_comparison_t>(r.comparison);
    rule->target = r.target;
    rule->reference = r.reference.c_str();
    return true;
}

bool sb_achievement_player_progress(sb_game_handle_t handle, sb_player_t player, const char *key,
                                    sb_achievement_progress_t *progress)
{
    const auto *record = findRecord(pinView(handle), player, key);
    if (!record || !progress) {
        return false;
    }
    progress->held = record->held;
    progress->all_satisfied = record->allSatisfied;
    progress->confirmed = record->confirmed;
    return true;
}

bool sb_achievement_player_rule_progress(sb_game_handle_t handle, sb_player_t player,
                                         const char *key, size_t index,
                                         sb_achievement_rule_progress_t *progress)
{
    const auto &view = pinView(handle);
    const auto *definition = findDefinition(view, key);
    const auto *record = findRecord(view, player, key);
    if (!definition || !record || !progress || index >= definition->rules.size()) {
        return false;
    }

    const auto it = record->rules.find(index);
    progress->judged = it != record->rules.end();
    progress->value = progress->judged ? it->second.value : 0;
    progress->satisfied = progress->judged && it->second.satisfied;
    progress->target = definition->rules[index].target;
    return true;
}

void sb_refresh_achievements(sb_game_handle_t handle)
{
    postToGameState(handle, [handle] { gameStateOf(handle).achievements().refreshDefinitions(); });
}

void sb_fetch_player_achievements(sb_game_handle_t handle, const char *user_id,
                                  sb_string_callback_t callback, void *user_data)
{
    gameStateOf(handle).fetchPlayerAchievements(
            user_id ? user_id : "",
            [callback, user_data](scorbit::Error error, const std::string &reply) {
                if (callback) {
                    callback(static_cast<sb_error_t>(error), reply.c_str(), user_data);
                }
            });
}

void sb_flush_achievement_reports(sb_game_handle_t handle)
{
    postToGameState(handle, [handle] { gameStateOf(handle).achievements().flushReports(); });
}

void sb_download_achievement_frames(sb_game_handle_t handle)
{
    postToGameState(handle, [handle] { gameStateOf(handle).achievements().refreshFrames(); });
}

bool sb_achievement_frame(sb_game_handle_t handle, const char *key, const uint8_t **data,
                          size_t *size)
{
    if (!key || !data || !size) {
        return false;
    }
    auto frame = gameStateOf(handle).achievements().frame(key);
    if (!frame) {
        return false;
    }
    t_pinned.frame = std::move(*frame);
    *data = t_pinned.frame.data();
    *size = t_pinned.frame.size();
    return true;
}
