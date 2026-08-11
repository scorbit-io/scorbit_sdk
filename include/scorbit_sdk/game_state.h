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

#include <scorbit_sdk/export.h>
#include "common_types_c.h"

#include "achievements.h"
#include "achievements_c.h"
#include "leaderboard.h"
#include "net_types.h"
#include "game_state_c.h"
#include "event.h"
#include "config.h"

#include <cstdint>
#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace scorbit {

/**
 * @brief Game state class.
 *
 * This class provides an interface to modify the game state. The game state is a collection of
 * information about the current state of the game, such as the active player, score, and active
 * modes. The game state can be modified by setting the active player, updating the player's score,
 * and adding or removing modes.
 *
 * The game must be marked as started by calling @ref setGameStarted before any modifications to the
 * state can be made.
 *
 * @note To apply changes to the game state, the @ref commit function must be called. This function
 * finalizes all modifications made to the game state. Until @ref commit is invoked, the game state
 * remains unchanged. Ideally, @ref commit should be called at the end of each frame cycle.
 *
 * @warning If the game is not active, all calls to modify the game state, as well as @ref commit,
 * will be ignored.
 */
class GameState
{
public:
    GameState(sb_game_handle_t handle, Config &config)
        : m_handle(handle, sb_destroy_game_state)
    {
        // Move callback ownership from Config to GameState.
        // unique_ptr move preserves the underlying pointer address, so the raw pointers
        // captured in the C bridge lambdas remain valid for the lifetime of the game state.
        m_eventCallbackStorage = std::move(config.m_eventCallbackStorage);
        m_saveKeyCallbackStorage = std::move(config.m_saveKeyCallbackStorage);
        m_loadKeyCallbackStorage = std::move(config.m_loadKeyCallbackStorage);
    }

    GameState(const GameState &) = delete;
    GameState &operator=(const GameState &) = delete;
    GameState(GameState &&) = default;
    GameState &operator=(GameState &&) = default;

    /**
     * @brief Mark the game as started.
     *
     * This function sets the game session active, resetting the game state. It initializes the
     * active player to Player 1 with a score of 0, and sets the current ball to 1.
     *
     * If the game is already in progress, this function has no effect.
     *
     * @note After starting the game, @ref commit must be called to notify the cloud. Optionally,
     * before calling @ref commit, the active player, scores, modes, or current ball can be
     * modified.
     *
     * @param origin The origin of the game start. This indicates how the game was started, such as
     * by pressing the start button or via a request from the lobby (mobile app). See
     * @ref scorbit::GameStartOrigin for details and @ref scobit::EventType::GameStartRequested
     * event.
     */
    void setGameStarted(GameStartOrigin origin)
    {
        sb_set_game_started(m_handle.get(), static_cast<sb_game_start_origin_t>(origin));
    }

    /**
     * @brief Mark the game as finished.
     *
     * Marks the game as completed. Call this function when the game ends.
     *
     * @note This function automatically commits changes using @ref commit.
     *
     * @warning After the game is finished, you can't add any modes or change players' scores.
     */
    void setGameFinished() { sb_set_game_finished(m_handle.get()); }

    /**
     * @brief Set the current ball number.
     *
     * Updates the current ball number in the game. When game starts, the ball number is
     * automatically set to 1.
     *
     * @param ball The ball number [1-9]. If the ball number is out of range, the function does
     * nothing.
     */
    void setCurrentBall(sb_ball_t ball) { sb_set_current_ball(m_handle.get(), ball); }

    /**
     * @brief Set the active player.
     *
     * Updates the current active player in the game. By default, player 1 is the active player.
     *
     * @note If active player was set while player is not yet exists, new player will be added with
     * score 0 and set active.
     *
     * @param player The player's number [1-9]. Typically, up to 6 players are supported in
     * pinball. If the player number is out of range, the function does nothing.
     */
    void setActivePlayer(sb_player_t player) { sb_set_active_player(m_handle.get(), player); }

    /**
     * @brief Set the player's score.
     *
     * Updates the specified player's score. If the new score is the same as the current score,
     * no update is made.
     * If the player does not exist, a new player is added with the specified score.
     *
     * @param player The player's number [1-9]. If the player number is out of range, the function
     * does nothing.
     * @param score The player's new score.
     * @param feature The score feature (i.e., what game feature caused the score bump, like
     * spinner, etc.). If the feature is not set, it is 0.
     */
    void setScore(sb_player_t player, sb_score_t score, sb_score_feature_t feature = 0)
    {
        sb_set_score(m_handle.get(), player, score, feature);
    }

    /**
     * @brief Add a mode to the game.
     *
     * Adds a mode to the game's active mode list. If the mode already exists, it is skipped.
     * To remove a mode, use @ref removeMode. All modes can be cleared at once using
     * @ref clearModes.
     *
     * @param mode The mode to add (e.g., "MB:Multiball").
     */
    void addMode(const std::string &mode) { sb_add_mode(m_handle.get(), mode.c_str()); }

    /**
     * @brief Add a mode that expires automatically after a duration (seconds).
     *
     * Duration rules: **0** becomes **3** seconds (recommended default); values **> 10** clamp to
     * **10**; **1-10** unchanged. **3** seconds is recommended. No need to call @ref removeMode
     * when the timer elapses; calling again with the same mode before expiry promotes it to the
     * front and resets the timer.
     *
     * @code
     * game.addModeExpiring("MB:Multiball", 3); // recommended duration
     * @endcode
     */
    void addModeExpiring(const std::string &mode, uint32_t durationSeconds)
    {
        sb_add_mode_expiring(m_handle.get(), mode.c_str(), durationSeconds);
    }

    /**
     * @brief Remove a mode from the game.
     *
     * Removes a mode from the game's active mode list. If the mode does not exist, it is skipped.
     * To remove all modes at once, use @ref clearModes.
     *
     * @param mode The mode to remove (e.g., "MB:Multiball"). If the mode doesn't exist, the
     * function does nothing.
     */
    void removeMode(const std::string &mode) { sb_remove_mode(m_handle.get(), mode.c_str()); }

    /**
     * @brief Clear all modes.
     *
     * Removes all modes from the game's active mode list.
     */
    void clearModes() { sb_clear_modes(m_handle.get()); }

    /**
     * @brief Commit changes to the game state.
     *
     * Applies all changes made to the game state. This function should be called after
     * any modifications to the game state, such as @ref setActivePlayer, @ref setScore, @ref
     * addMode, @ref addModeExpiring, @ref removeMode, or @ref clearModes.
     *
     * If nothing was changed, this function does nothing.
     */
    void commit() { sb_commit(m_handle.get()); }

    // ----------------------------------------------------------------

    /**
     * @brief Retrieves the current authentication status.
     *
     * Key statuses to consider:
     * - @ref AuthStatus::AuthenticatedUnpaired: Authentication succeeded, but pairing is not
     * established.
     * - @ref AuthStatus::AuthenticatedPaired: Authentication succeeded, and pairing is established.
     * - @ref AuthStatus::AuthenticationFailed: The authentication process failed, indicating a
     * signing error.
     *
     * @return The current authentication status as an @ref AuthStatus value.
     */
    AuthStatus getStatus() const { return static_cast<AuthStatus>(sb_get_status(m_handle.get())); }

    /**
     * @brief Retrieve the machine's UUID.
     *
     * If machine UUID was not provided and it was derived from MAC address
     *
     * @return The machine UUID.
     */
    std::string getMachineUuid() const { return std::string {sb_get_machine_uuid(m_handle.get())}; }

    /**
     * @brief Retrieve the machine serial number.
     *
     * @return The serial number (see @ref sb_get_machine_serial).
     */
    std::uint64_t getMachineSerial() const { return sb_get_machine_serial(m_handle.get()); }

    /**
     * @brief Retrieve the pairing deeplink.
     *
     * This link has to be encoded and displayed as QR code, so that the user can scan it with
     * mobile app to do pairing.
     *
     * @return The pairing deeplink. If the machine is not paired or the SDK is not yet
     * authenticated, an empty string is returned.
     */
    std::string getPairDeeplink() const
    {
        return std::string {sb_get_pair_deeplink(m_handle.get())};
    }

    /**
     * @brief Retrieves the top scores from the leaderboard.
     *
     * @note The callback function is invoked asynchronously when the operation completes, running
     * in a separate thread from the main calling thread. It is recommended to use appropriate locks
     * (e.g., a mutex) when accessing shared data.
     *
     * @param scope Selects whether the request targets the paired venue machine leaderboard, the
     * variant-wide leaderboard for the paired title, or the shared game leaderboard.
     * @param period Selects the backend time bucket. Use @ref LeaderboardPeriod::AllTime for the
     * unfiltered all-time leaderboard.
     * @param since Optional UTC ISO-8601 lower-bound time filter. When non-empty, the backend uses
     * this value instead of @p period.
     * @param vpinFilter Controls whether virtual pinball scores are included. Use
     * @ref LeaderboardVpinFilter::Any to include both virtual and physical scores.
     * @param callback Receives a @ref LeaderboardResult built from the API response. The SDK
     * copies leaderboard data before your callback runs; you do not receive a raw handle to free.
     *
     * If pairing or machine context (machine UUID, variant UUID, or game slug, depending on
     * @p scope) is not available yet, the SDK defers the HTTP request and retries automatically
     * until the context is ready or a terminal error occurs. The callback is invoked only when
     * the request completes or fails terminally.
     */
    void requestTopScores(LeaderboardScope scope, LeaderboardPeriod period,
                          const std::string &since, LeaderboardVpinFilter vpinFilter,
                          LeaderboardCallback callback)
    {
        auto cbPair = prepareLeaderboardCallback(std::move(callback));
        sb_request_top_scores(m_handle.get(), static_cast<sb_leaderboard_scope_t>(scope),
                              static_cast<sb_leaderboard_period_t>(period),
                              since.empty() ? nullptr : since.c_str(),
                              static_cast<sb_leaderboard_vpin_filter_t>(vpinFilter), cbPair.first,
                              cbPair.second);
    }

    /**
     * @brief Request a pairing short code (6 alphanumeric characters).
     *
     * Requests a pairing short code from the server. The short code is used to pair the device with
     * the Scorbit service where on machines which can display only aplhanumric characters. This is
     * alternative to @ref getPairDeeplink.
     *
     * @note The callback function is invoked asynchronously when the operation completes, running
     * in a separate thread from the main calling thread. It is recommended to use appropriate locks
     * (e.g., a mutex) when accessing shared data.
     *
     * @param callback A callback function of @ref StringCallbak that receives the short code.
     * Returns @ref Error::Success if the request was successful. Otherwise, it returns an error
     * code: @ref Error::ApiError if the API call failed.
     */
    void requestPairCode(StringCallback callback) const
    {
        auto cbPair = prepareStringCallback(std::move(callback));
        sb_request_pair_code(m_handle.get(), cbPair.first, cbPair.second);
    }

    /**
     * @brief Request to unpair a device.
     *
     * Sends a request to unpair the device from the Scorbit service. This function should be called
     * when the device is being reset by a (new) owner.
     *
     * The returned data string is the raw reply from the API and can be safely ignored. On a
     * successful unpairing, it will return @ref SB_EC_SUCCESS.
     *
     * @note The callback function is invoked asynchronously when the operation completes, running
     * in a separate thread from the main calling thread. It is recommended to use appropriate locks
     * (e.g., a mutex) when accessing shared data.
     *
     * @param callback A callback function of @ref StringCallbak that receives the error code.
     * Returns @ref Error::Success if the request was successful. Otherwise, it returns an error
     * code: @ref Error::ApiError if the API call failed.
     */
    void requestUnpair(StringCallback callback) const
    {
        auto cbPair = prepareStringCallback(std::move(callback));
        sb_request_unpair(m_handle.get(), cbPair.first, cbPair.second);
    }

    /**
     * @brief Sets the device capabilities.
     *
     * Configures the device with the features it supports. The @p capabilities
     * argument should contain a bitwise OR of one or more values from @ref scorbit::Capability.
     *
     * @note If this function is not called, all capabilities are assumed to be disabled by default.
     *
     * @param capabilities Bitwise OR of capability flags supported by the device.
     */
    void setCapabilities(Capabilities capabilities)
    {
        sb_set_capabilities(m_handle.get(), capabilities);
    }

    // -------------------------- CREDITS / STATUS ----------------------------------

    /**
     * @brief Sets the number of credits dropped into the machine.
     *
     * This function should be called when @ref scorbit::EventType::CreditsAddRequested event
     * received and credits added to machine. It notifies the Scorbit cloud service and mobile app
     * dropped credits count and if it was successful.
     *
     * @note it should not be called if physical coins dropped in to machine.
     *
     * @param credits The number of credits dropped into the machine.
     * @param transaction The transaction identifier associated with the credit drop (passed in the
     * event).
     * @param success true if the credit drop was successful; false otherwise.
     */
    void setCreditsDropped(int credits, const std::string &transaction, bool success)
    {
        sb_set_credits_dropped(m_handle.get(), credits, transaction.c_str(), success);
    }

    /**
     * @brief Sets the current credits status.
     *
     * This function should be called:
     * 1. when @ref scorbit::EventType::CreditsStatusRequested event received
     * 2. when credits number changed in machine (added or subtracted)
     *
     * @param freePlay true if the machine is in free play mode; false otherwise.
     * @param credits The current number of credits available in the machine.
     * @param maxCredits The maximum number of credits allowed in the machine.
     * @param pricing For future use. Currently should be set to nullptr or an empty string.
     */
    void setCreditsStatus(bool freePlay, int credits, int maxCredits, const char *pricing = nullptr)
    {
        sb_set_credits_status(m_handle.get(), freePlay, credits, maxCredits, pricing);
    }

    /**
     * @brief Upload diagnostics (logs, recordings, and arbitrary text) to the Scorbit API.
     *
     * Creates a tar.gz archive from the provided files and text, then uploads it.
     * The SDK enforces limits: max 5 log files (each <= 10 MB), max 2 recordings
     * (each <= 20 MB), and log string truncated to 10 MB. Files exceeding limits are skipped.
     *
     * When the upload completes, a @ref EventType::DiagnosticsUploaded event is fired.
     *
     * @param logPaths List of file paths to log files.
     * @param recordingPaths List of file paths to recording files.
     * @param logString Arbitrary log text to include.
     */
    void uploadDiagnostics(const std::vector<std::string> &logPaths,
                           const std::vector<std::string> &recordingPaths = {},
                           const std::string &logString = {})
    {
        std::vector<const char *> logArr;
        logArr.reserve(logPaths.size());
        for (const auto &p : logPaths) {
            logArr.push_back(p.c_str());
        }

        std::vector<const char *> recArr;
        recArr.reserve(recordingPaths.size());
        for (const auto &p : recordingPaths) {
            recArr.push_back(p.c_str());
        }

        sb_upload_diagnostics(
                m_handle.get(), logArr.empty() ? nullptr : const_cast<const char **>(logArr.data()),
                logArr.size(), recArr.empty() ? nullptr : const_cast<const char **>(recArr.data()),
                recArr.size(), logString.c_str());
    }

    // -------------------------- ACHIEVEMENTS ----------------------------------

    /**
     * @brief Fetch all published achievement definitions for the current machine.
     *
     * On success the definitions are also cached for local matching, so
     * @ref checkModeAchievements and friends start working as soon as the callback fires.
     *
     * @note The callback function is invoked asynchronously when the operation completes, running
     * in a separate thread from the main calling thread.
     *
     * @param callback Receives the definitions, or an empty vector on failure.
     */
    void fetchAchievements(AchievementsCallback callback)
    {
        auto cbPair = prepareAchievementsCallback(std::move(callback));
        sb_fetch_achievements(m_handle.get(), cbPair.first, cbPair.second);
    }

    // TEMPORARY - LOCAL TESTING ONLY - DELETE BEFORE COMMIT.
    bool debugSeedAchievements(const std::string &json)
    {
        return sb_debug_seed_achievements(m_handle.get(), json.c_str());
    }

    /**
     * @brief Fetch one user's progress for the current machine's achievements.
     *
     * On success the progress is also cached for local matching. Call this when a player claims a
     * slot.
     *
     * @param userId The user's UUID (the public "id" the v2 API exposes everywhere, not an
     *               internal integer id) to fetch progress for.
     * @param callback Receives the progress entries, or an empty vector on failure.
     */
    void fetchAchievementProgress(const std::string &userId, AchievementProgressCallback callback)
    {
        auto cbPair = prepareAchievementProgressCallback(std::move(callback));
        sb_fetch_achievement_progress(m_handle.get(), userId.c_str(), cbPair.first, cbPair.second);
    }

    /**
     * @brief Request that the server unlock an achievement for a user.
     *
     * The server validates the request and is the authority; a local match is not an unlock. Wait
     * for the @ref scorbit::EventType::AchievementUnlocked event before telling the player they
     * earned anything.
     *
     * @param userId The user to unlock the achievement for.
     * @param key The achievement key.
     * @param count Count value: 1 for boolean achievements, or the increment for counters.
     * @param callback Receives the result of the request.
     */
    void unlockAchievement(const std::string &userId, const std::string &key, int count,
                           AchievementUnlockCallback callback)
    {
        auto cbPair = prepareAchievementUnlockCallback(std::move(callback));
        sb_unlock_achievement(m_handle.get(), userId.c_str(), key.c_str(), count, cbPair.first,
                              cbPair.second);
    }

    /**
     * @brief Request that the server revoke a trophy from its current holder.
     *
     * @param userId The user to revoke the trophy from.
     * @param key The achievement key.
     * @param callback Receives the result of the request.
     */
    void lockAchievement(const std::string &userId, const std::string &key, AchievementUnlockCallback callback)
    {
        auto cbPair = prepareAchievementUnlockCallback(std::move(callback));
        sb_lock_achievement(m_handle.get(), userId.c_str(), key.c_str(), cbPair.first, cbPair.second);
    }

    /**
     * @brief Return true if achievement definitions have been fetched and cached.
     */
    bool hasAchievements() const { return sb_has_achievements(m_handle.get()); }

    /**
     * @brief Every cached achievement definition.
     */
    std::vector<Achievement> getCachedAchievements() const
    {
        const auto count = sb_get_cached_achievements_count(m_handle.get());
        std::vector<Achievement> result;
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            sb_achievement_t c {};
            if (sb_get_cached_achievement_at(m_handle.get(), i, &c)) {
                result.push_back(achievementFromC(c));
            }
        }
        return result;
    }

    /**
     * @brief One cached achievement definition.
     *
     * @param achievement [OUT] The achievement, if found.
     * @return true if the key exists in the cache, false otherwise.
     */
    bool getCachedAchievement(const std::string &key, Achievement &achievement) const
    {
        sb_achievement_t c {};
        if (!sb_get_cached_achievement(m_handle.get(), key.c_str(), &c)) {
            return false;
        }
        achievement = achievementFromC(c);
        return true;
    }

    /**
     * @brief A user's cached progress for one achievement.
     *
     * @param progress [OUT] The progress, if cached.
     * @return true if progress is cached for that user and key, false otherwise.
     */
    bool getCachedProgress(const std::string &userId, const std::string &key,
                           AchievementProgress &progress) const
    {
        sb_achievement_progress_t c {};
        if (!sb_get_cached_progress(m_handle.get(), userId.c_str(), key.c_str(), &c)) {
            return false;
        }

        progress.key = c.key ? c.key : std::string {};
        progress.progress = c.progress;
        progress.unlocked = c.unlocked;
        progress.unlockedAt = c.unlocked_at ? c.unlocked_at : std::string {};
        return true;
    }

    /**
     * @brief Keys of the achievements matched by a mode event, ignoring score.
     *
     * Matching is **conservative** and **predictive only** - see @ref sb_check_mode_achievements.
     * Because no score is supplied, an achievement carrying a `"SCORE"` rule can never match
     * here; use @ref checkModeAchievementsWithScore for those.
     *
     * @param modeName The mode that started, completed, or stacked.
     * @param modeType One of `"start"`, `"complete"`, `"stack"`.
     * @param userId The user whose cached progress gates re-checking.
     */
    std::vector<std::string> checkModeAchievements(const std::string &modeName,
                                                  const std::string &modeType, const std::string &userId) const
    {
        std::vector<const char *> keys(matchBufferSize());
        const auto count = sb_check_mode_achievements(m_handle.get(), modeName.c_str(),
                                                     modeType.c_str(), userId.c_str(), keys.data(),
                                                     keys.size());
        return toStringVector(keys, count);
    }

    /**
     * @brief Keys of the achievements matched by a mode event, also judging `"SCORE"` rules.
     *
     * Use this entry point for any achievement that combines mode and score conditions.
     */
    std::vector<std::string> checkModeAchievementsWithScore(const std::string &modeName,
                                                            const std::string &modeType,
                                                            const std::string &userId, int64_t score) const
    {
        std::vector<const char *> keys(matchBufferSize());
        const auto count = sb_check_mode_achievements_with_score(m_handle.get(), modeName.c_str(),
                                                                modeType.c_str(), userId.c_str(), score,
                                                                keys.data(), keys.size());
        return toStringVector(keys, count);
    }

    /**
     * @brief Keys of the achievements matched by the current score alone.
     *
     * An achievement combining a mode rule with a `"SCORE"` rule is **not** reported here; check
     * those through @ref checkModeAchievementsWithScore when the mode event arrives.
     */
    std::vector<std::string> checkScoreAchievements(int64_t score, const std::string &userId) const
    {
        std::vector<const char *> keys(matchBufferSize());
        const auto count = sb_check_score_achievements(m_handle.get(), score, userId.c_str(), keys.data(),
                                                      keys.size());
        return toStringVector(keys, count);
    }

    /**
     * @brief Add to a counter achievement's locally accumulated progress.
     *
     * The unlock threshold comes from the achievement's `"PROGRESS"` rule `target`; see
     * @ref sb_increment_achievement_progress.
     *
     * @param key Achievement key whose counter is being bumped.
     * @param userId User the progress belongs to.
     * @param increment Amount to add.
     * @param metricKey The `"PROGRESS"` rule `reference`. Leave empty when the achievement has
     * exactly one `"PROGRESS"` rule.
     * @return true only if this call crossed the threshold of a located `"PROGRESS"` rule.
     */
    bool incrementAchievementProgress(const std::string &key, const std::string &userId, int increment = 1,
                                      const std::string &metricKey = {})
    {
        return sb_increment_achievement_progress(m_handle.get(), key.c_str(), userId.c_str(), increment,
                                                metricKey.c_str());
    }

    /**
     * @brief Register the callback invoked when local matching records progress or a local unlock.
     *
     * @param callback The callback, or nullptr to clear it.
     */
    void setAchievementTriggeredCallback(AchievementTriggeredCallback callback)
    {
        if (!callback) {
            sb_set_achievement_triggered_callback(m_handle.get(), nullptr, nullptr);
            m_achievementTriggeredCallbackStorage.reset();
            return;
        }

        m_achievementTriggeredCallbackStorage =
                std::make_unique<AchievementTriggeredCallback>(std::move(callback));
        sb_set_achievement_triggered_callback(m_handle.get(),
                                             &GameState::achievement_triggered_callback_c,
                                             m_achievementTriggeredCallbackStorage.get());
    }

    /**
     * @brief Drop one user's locally accumulated progress.
     *
     * Call this when a player leaves the session, so that session-scoped counters do not carry
     * into the next session and unlock early.
     */
    void clearAchievementUserProgress(const std::string &userId)
    {
        sb_clear_achievement_user_progress(m_handle.get(), userId.c_str());
    }

    /**
     * @brief Drop every user's locally accumulated progress. Call this on game end.
     */
    void clearAchievementProgress() { sb_clear_achievement_progress(m_handle.get()); }

    /**
     * @brief Download and cache achievement DMD frames for later display.
     */
    void downloadAchievementFrames() { sb_download_achievement_frames(m_handle.get()); }

    /**
     * @brief Return true if a DMD frame is cached for @p key.
     */
    bool hasDmdFrame(const std::string &key) const
    {
        return sb_has_dmd_frame(m_handle.get(), key.c_str());
    }

    /**
     * @brief The cached DMD frame for @p key, or an empty vector if it is not cached.
     */
    std::vector<uint8_t> getDmdFrame(const std::string &key) const
    {
        size_t size = 0;
        const auto *data = sb_get_dmd_frame(m_handle.get(), key.c_str(), &size);
        if (!data || size == 0) {
            return {};
        }
        return std::vector<uint8_t>(data, data + size);
    }

    // -------------------------- INTERNAL FOR SCORBIT  --------------------------------------

    void requestPairMachine(const std::string &machineUuid, const std::string &ownerUuid,
                            StringCallback callback)
    {
        auto cbPair = prepareStringCallback(std::move(callback));
        sb_game_request_pair_machine(m_handle.get(), machineUuid.c_str(), ownerUuid.c_str(),
                                     cbPair.first, cbPair.second);
    }

    /**
     * @brief Download a file from a URL and save it to local storage.
     *
     * @note The callback function is invoked asynchronously when the operation completes, running
     * in a separate thread from the main calling thread.
     *
     * @param url The URL to download from.
     * @param filename The local filename to save the downloaded file to.
     * @param headers Optional HTTP headers to include in the request.
     * @param callback A callback function of type @ref StringCallback that receives the result.
     * Returns @ref Error::Success if the download was successful. On success, the reply string
     * contains the path to the downloaded file.
     */
    void download(const std::string &url, const std::string &filename, const HttpHeaders &headers,
                  StringCallback callback)
    {
        auto cHeaders = toCHeaders(headers);
        auto cbPair = prepareStringCallback(std::move(callback));
        sb_download(m_handle.get(), url.c_str(), filename.c_str(), cHeaders.data(), cHeaders.size(),
                    cbPair.first, cbPair.second);
    }

    /**
     * @brief Download data from a URL into a memory buffer.
     *
     * @note The callback function is invoked asynchronously when the operation completes, running
     * in a separate thread from the main calling thread.
     *
     * @param url The URL to download from.
     * @param reserveBufferSize The initial buffer size to reserve for the download.
     * @param headers Optional HTTP headers to include in the request.
     * @param callback A callback function of type @ref VectorCallback that receives the downloaded
     * data. Returns @ref Error::Success if the download was successful.
     */
    void downloadBuffer(const std::string &url, size_t reserveBufferSize,
                        const HttpHeaders &headers, VectorCallback callback)
    {
        auto cHeaders = toCHeaders(headers);
        auto cbPair = prepareBufferCallback(std::move(callback));
        sb_download_buffer(m_handle.get(), url.c_str(), reserveBufferSize, cHeaders.data(),
                           cHeaders.size(), cbPair.first, cbPair.second);
    }

    // -------------------------- END OF PUBLIC INTERFACE  --------------------------------------

private:
    static void leaderboard_callback_c(sb_error_t error, sb_leaderboard_t *leaderboard,
                                       void *user_data)
    {
        auto *cb = static_cast<LeaderboardCallback *>(user_data);
        auto result = leaderboard ? LeaderboardResult::fromC(leaderboard) : LeaderboardResult {};
        (*cb)(static_cast<Error>(error), result);
        delete cb;
    }

    static std::pair<sb_leaderboard_callback_t, void *>
    prepareLeaderboardCallback(LeaderboardCallback callback)
    {
        auto *userData = new LeaderboardCallback(std::move(callback));
        return std::make_pair(&GameState::leaderboard_callback_c, userData);
    }

    static void string_callback_c(sb_error_t error, const char *reply, void *user_data)
    {
        auto *cb = static_cast<StringCallback *>(user_data);
        (*cb)(static_cast<Error>(error), reply ? std::string(reply) : std::string {});
        delete cb;
    }

    static std::pair<sb_string_callback_t, void *> prepareStringCallback(StringCallback callback)
    {
        auto *userData = new StringCallback(std::move(callback));
        return std::make_pair(&GameState::string_callback_c, userData);
    }

    static void buffer_callback_c(sb_error_t error, const uint8_t *data, size_t size,
                                  void *user_data)
    {
        auto *cb = static_cast<VectorCallback *>(user_data);

        if (data == nullptr) {
            size = 0;
        }
        (*cb)(static_cast<Error>(error), std::vector<uint8_t>(data, data + size));
        delete cb;
    }

    static std::pair<sb_buffer_callback_t, void *> prepareBufferCallback(VectorCallback callback)
    {
        auto *userData = new VectorCallback(std::move(callback));
        return std::make_pair(&GameState::buffer_callback_c, userData);
    }

    // ---- Achievement callback bridges ----

    static Achievement achievementFromC(const sb_achievement_t &c)
    {
        Achievement ach;
        ach.key = c.key ? c.key : "";
        ach.name = c.name ? c.name : "";
        ach.description = c.description ? c.description : "";
        ach.scope = c.scope ? c.scope : "";
        ach.imageUrl = c.image_url ? c.image_url : "";
        ach.obscureImageUrl = c.obscure_image_url ? c.obscure_image_url : "";
        ach.obscure = c.obscure;
        ach.visible = c.visible;
        ach.isTrophy = c.is_trophy;
        ach.notifyWhenAchieved = c.notify_when_achieved;
        ach.inputTime = static_cast<AchievementInputTime>(c.input_time);
        ach.trigger = static_cast<AchievementTrigger>(c.trigger);
        ach.modeType = static_cast<AchievementModeType>(c.mode_type);
        ach.modeName = c.mode_name ? c.mode_name : "";
        ach.targetScore = c.target_score;
        ach.groupId = c.group_id;
        ach.level = c.level;
        ach.ballCount = c.ball_count;
        // Rules are read back through the handle by the caller when needed; rules_count is kept
        // so callers can tell a rule-less achievement from one whose rules were not fetched.
        ach.rules.resize(c.rules_count);
        return ach;
    }

    static void achievements_callback_c(sb_error_t error, const sb_achievement_t *achievements,
                                       size_t count, void *user_data)
    {
        auto *cb = static_cast<AchievementsCallback *>(user_data);
        std::vector<Achievement> result;
        if (achievements) {
            result.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                result.push_back(achievementFromC(achievements[i]));
            }
        }
        (*cb)(static_cast<Error>(error), std::move(result));
        delete cb;
    }

    static std::pair<sb_achievements_callback_t, void *>
    prepareAchievementsCallback(AchievementsCallback callback)
    {
        auto *userData = new AchievementsCallback(std::move(callback));
        return std::make_pair(&GameState::achievements_callback_c, userData);
    }

    static void achievement_progress_callback_c(sb_error_t error,
                                               const sb_achievement_progress_t *progress,
                                               size_t count, void *user_data)
    {
        auto *cb = static_cast<AchievementProgressCallback *>(user_data);
        std::vector<AchievementProgress> result;
        if (progress) {
            result.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                AchievementProgress entry;
                entry.key = progress[i].key ? progress[i].key : "";
                entry.progress = progress[i].progress;
                entry.unlocked = progress[i].unlocked;
                entry.unlockedAt = progress[i].unlocked_at ? progress[i].unlocked_at : "";
                result.push_back(std::move(entry));
            }
        }
        (*cb)(static_cast<Error>(error), std::move(result));
        delete cb;
    }

    static std::pair<sb_achievement_progress_callback_t, void *>
    prepareAchievementProgressCallback(AchievementProgressCallback callback)
    {
        auto *userData = new AchievementProgressCallback(std::move(callback));
        return std::make_pair(&GameState::achievement_progress_callback_c, userData);
    }

    static void achievement_unlock_callback_c(sb_error_t error,
                                             const sb_achievement_unlock_result_t *result,
                                             void *user_data)
    {
        auto *cb = static_cast<AchievementUnlockCallback *>(user_data);
        AchievementUnlockResult out;
        if (result) {
            out.key = result->key ? result->key : "";
            out.success = result->success;
            out.newlyUnlocked = result->newly_unlocked;
            out.message = result->message ? result->message : "";
        }
        (*cb)(static_cast<Error>(error), std::move(out));
        delete cb;
    }

    static std::pair<sb_achievement_unlock_callback_t, void *>
    prepareAchievementUnlockCallback(AchievementUnlockCallback callback)
    {
        auto *userData = new AchievementUnlockCallback(std::move(callback));
        return std::make_pair(&GameState::achievement_unlock_callback_c, userData);
    }

    static void achievement_triggered_callback_c(const char *key, const char *user_id,
                                                bool is_unlock, int progress, void *user_data)
    {
        auto *cb = static_cast<AchievementTriggeredCallback *>(user_data);
        if (cb && *cb) {
            (*cb)(key ? std::string(key) : std::string {},
                 user_id ? std::string(user_id) : std::string {}, is_unlock, progress);
        }
    }

    /// Upper bound on how many keys a check*() call can return: one per cached definition.
    size_t matchBufferSize() const
    {
        const auto count = sb_get_cached_achievements_count(m_handle.get());
        return count > 0 ? count : 1;
    }

    static std::vector<std::string> toStringVector(const std::vector<const char *> &keys,
                                                   size_t count)
    {
        std::vector<std::string> result;
        result.reserve(count);
        for (size_t i = 0; i < count && i < keys.size(); ++i) {
            result.emplace_back(keys[i] ? keys[i] : "");
        }
        return result;
    }

    static std::vector<sb_http_header_t> toCHeaders(const HttpHeaders &headers)
    {
        std::vector<sb_http_header_t> out;
        out.reserve(headers.size());
        for (const auto &header : headers) {
            out.push_back({header.first.c_str(), header.second.c_str()});
        }
        return out;
    }

private:
    // Callback storage - ownership moved from Config in createGameState().
    // Declared before m_handle so they are destroyed AFTER the handle (C++ destroys
    // members in reverse declaration order). This ensures SDK internals are fully
    // torn down before the callbacks are freed.
    std::unique_ptr<std::function<void(const Event &)>> m_eventCallbackStorage;
    std::unique_ptr<SaveKeyCallback> m_saveKeyCallbackStorage;
    std::unique_ptr<LoadKeyCallback> m_loadKeyCallbackStorage;
    std::unique_ptr<AchievementTriggeredCallback> m_achievementTriggeredCallbackStorage;

    std::unique_ptr<std::remove_pointer<sb_game_handle_t>::type, void (*)(sb_game_handle_t)>
            m_handle;
};

} // namespace scorbit
