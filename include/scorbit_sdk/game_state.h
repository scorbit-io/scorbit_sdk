/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/export.h>
#include "common_types_c.h"

#include "net_types.h"
#include "game_state_c.h"
#include "player_info.h"

#include <string>

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
class SCORBIT_SDK_EXPORT GameState
{
public:
    GameState(sb_game_handle_t handle)
        : m_handle(handle)
    {
    }

    ~GameState() { sb_destroy_game_state(m_handle); }

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
     */
    void setGameStarted() { sb_set_game_started(m_handle); }

    /**
     * @brief Mark the game as finished.
     *
     * Marks the game as completed. Call this function when the game ends.
     *
     * @note This function automatically commits changes using @ref commit.
     *
     * @warning After the game is finished, you can't add any modes or change players' scores.
     */
    void setGameFinished() { sb_set_game_finished(m_handle); }

    /**
     * @brief Set the current ball number.
     *
     * Updates the current ball number in the game. When game starts, the ball number is
     * automatically set to 1.
     *
     * @param ball The ball number [1-9]. If the ball number is out of range, the function does
     * nothing.
     */
    void setCurrentBall(sb_ball_t ball) { sb_set_current_ball(m_handle, ball); }

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
    void setActivePlayer(sb_player_t player) { sb_set_active_player(m_handle, player); }

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
        sb_set_score(m_handle, player, score, feature);
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
    void addMode(const std::string &mode) { sb_add_mode(m_handle, mode.c_str()); }

    /**
     * @brief Remove a mode from the game.
     *
     * Removes a mode from the game's active mode list. If the mode does not exist, it is skipped.
     * To remove all modes at once, use @ref clearModes.
     *
     * @param mode The mode to remove (e.g., "MB:Multiball"). If the mode doesn't exist, the
     * function does nothing.
     */
    void removeMode(const std::string &mode) { sb_remove_mode(m_handle, mode.c_str()); }

    /**
     * @brief Clear all modes.
     *
     * Removes all modes from the game's active mode list.
     */
    void clearModes() { sb_clear_modes(m_handle); }

    /**
     * @brief Commit changes to the game state.
     *
     * Applies all changes made to the game state. This function should be called after
     * any modifications to the game state, such as @ref setActivePlayer, @ref setScore, @ref
     * addMode, @ref removeMode, or @ref clearModes.
     *
     * If nothing was changed, this function does nothing.
     */
    void commit() { sb_commit(m_handle); }

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
    AuthStatus getStatus() const { return static_cast<AuthStatus>(sb_get_status(m_handle)); }

    /**
     * @brief Retrieve the machine's UUID.
     *
     * If machine UUID was not provided and it was derived from MAC address
     *
     * @return The machine UUID.
     */
    std::string getMachineUuid() const { return std::string {sb_get_machine_uuid(m_handle)}; }

    /**
     * @brief Retrieve the pairing deeplink.
     *
     * This link has to be encoded and displayed as QR code, so that the user can scan it with
     * mobile app to do pairing.
     *
     * @return The pairing deeplink. If the machine is not paired or the SDK is not yet
     * authenticated, an empty string is returned.
     */
    std::string getPairDeeplink() const { return std::string {sb_get_pair_deeplink(m_handle)}; }

    /**
     * @brief Retrieve the claim and navigation deeplink.
     *
     * This link has to be encoded and displayed as QR code, so that the user can scan it with
     * mobile app to claim the player's slot.
     *
     * @param player The player number (starting from 1).
     * @return The claim deeplink string. If the machine is not paired or the SDK is not yet
     * authenticated, an empty string is returned.
     */
    std::string getClaimDeeplink(int player) const
    {
        return std::string {sb_get_claim_deeplink(m_handle, player)};
    }

    /**
     * @brief Retrieves the top scores from the leaderboard.
     *
     * @note The callback function is invoked asynchronously when the operation completes, running
     * in a separate thread from the main calling thread. It is recommended to use appropriate locks
     * (e.g., a mutex) when accessing shared data.
     *
     * @param scoreFilter A score value used to filter the leaderboard results. If a score is
     * provided, the function retrieves the ten scores above and ten scores below the specified
     * value, allowing the user to view their score in the leaderboard context. Set to 0 to disable
     * the score filter.
     * @param callback A callback function of type @ref StringCallback that receives the top scores
     * in JSON format as a string. Returns @ref Error::Success if the request was successful.
     * Otherwise, it returns an error codes: @ref Error::NotPaired if machine is not paired, or @ref
     * Error::ApiError if the API call failed.
     */
    void requestTopScores(sb_score_t scoreFilter, StringCallback callback)
    {
        auto cbPair = prepareCallback(std::move(callback));
        sb_request_top_scores(m_handle, scoreFilter, cbPair.first, cbPair.second);
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
        auto cbPair = prepareCallback(std::move(callback));
        sb_request_pair_code(m_handle, cbPair.first, cbPair.second);
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
        auto cbPair = prepareCallback(std::move(callback));
        sb_request_unpair(m_handle, cbPair.first, cbPair.second);
    }

    // -------------------------- PLAYER PROFILE INFO --------------------------------------

    /**
     * @brief Checks whether any player profiles have been updated.
     *
     * Calling this function clears the internal update status and prepares updated player data
     * for retrieval. If no changes are made to any player's information before the next call,
     * this function will return false.
     *
     * If the function returns true, use @getPlayerInfo function to retrieve the updated data.
     *
     * @return true if there are updates to any player profiles; false otherwise.
     */
    bool isPlayersInfoUpdated() { return sb_is_players_info_updated(m_handle); }

    /**
     * @brief Checks if player information is available.
     *
     * @param player The player number (starting from 1).
     * @return true if player information is available; false otherwise.
     */
    bool hasPlayerInfo(sb_player_t player) { return sb_has_player_info(m_handle, player); }

    /**
     * @brief Retrieves the player's profile info (@ref PlayerInfo)
     *
     * Returns the player's profile info. If the player does not exist, or player not yet claimed
     * his/her slot it will return @ref PlayerInfo::isValid == false.
     *
     * @param player The player number (starting from 1).
     * @return @ref PlayerInfo object with the player's profile info. If @ref PlayerInfo::isValid is
     * false, all fields will be empty.
     */
    PlayerInfo getPlayerInfo(sb_player_t player)
    {
        PlayerInfo info;
        if (hasPlayerInfo(player)) {
            info.preferredName = sb_get_player_preferred_name(m_handle, player);
            info.name = sb_get_player_name(m_handle, player);
            info.initials = sb_get_player_initials(m_handle, player);

            size_t pictureSize;
            const auto picture = sb_get_player_picture(m_handle, player, &pictureSize);
            if (picture && pictureSize > 0) {
                info.picture = Picture(picture, picture + pictureSize);
            }
        }
        return info;
    }

private:
    static std::pair<sb_string_callback_t, void *> prepareCallback(StringCallback callback)
    {
        auto *userData = new StringCallback(std::move(callback));

        // Define callback as static to get function pointer
        static sb_string_callback_t cb_c = [](sb_error_t error, const char *reply,
                                              void *user_data) {
            auto *cb = static_cast<StringCallback *>(user_data);
            (*cb)(static_cast<Error>(error), reply ? std::string(reply) : std::string {});
            delete cb;
        };

        return std::make_pair(cb_c, userData);
    }

private:
    sb_game_handle_t m_handle {nullptr};
};

} // namespace scorbit
