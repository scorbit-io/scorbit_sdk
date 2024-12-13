/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/export.h>
#include "common_types_c.h"
#include "net_base.h"

#include "spimpl/spimpl.h"

#include <string>
#include <memory>

namespace scorbit {

namespace detail {
class GameStateImpl;
}

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
    GameState(std::unique_ptr<detail::NetBase> net);

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
    void setGameStarted();

    /**
     * @brief Mark the game as finished.
     *
     * Marks the game as completed. Call this function when the game ends.
     *
     * @note This function automatically commits changes using @ref commit.
     *
     * @warning After the game is finished, you can't add any modes or change players' scores.
     */
    void setGameFinished();

    /**
     * @brief Set the current ball number.
     *
     * Updates the current ball number in the game. When game starts, the ball number is
     * automatically set to 1.
     *
     * @param ball The ball number [1-9]. If the ball number is out of range, the function does
     * nothing.
     */
    void setCurrentBall(sb_ball_t ball);

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
    void setActivePlayer(sb_player_t player);

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
    void setScore(sb_player_t player, sb_score_t score, sb_score_feature_t feature = 0);

    /**
     * @brief Add a mode to the game.
     *
     * Adds a mode to the game's active mode list. If the mode already exists, it is skipped.
     * To remove a mode, use @ref removeMode. All modes can be cleared at once using
     * @ref clearModes.
     *
     * @param mode The mode to add (e.g., "MB:Multiball").
     */
    void addMode(std::string mode);

    /**
     * @brief Remove a mode from the game.
     *
     * Removes a mode from the game's active mode list. If the mode does not exist, it is skipped.
     * To remove all modes at once, use @ref clearModes.
     *
     * @param mode The mode to remove (e.g., "MB:Multiball"). If the mode doesn't exist, the
     * function does nothing.
     */
    void removeMode(const std::string &mode);

    /**
     * @brief Clear all modes.
     *
     * Removes all modes from the game's active mode list.
     */
    void clearModes();

    /**
     * @brief Commit changes to the game state.
     *
     * Applies all changes made to the game state. This function should be called after
     * any modifications to the game state, such as @ref setActivePlayer, @ref setScore, @ref
     * addMode, @ref removeMode, or @ref clearModes.
     *
     * If nothing was changed, this function does nothing.
     */
    void commit();

    // ----------------------------------------------------------------

    /**
     * @brief Retrieve the machine's UUID.
     *
     * If machine UUID was not provided and it was derived from MAC address
     *
     * @return The machine UUID.
     */
    std::string getMachineUuid() const;

    /**
     * @brief Retrieve the pairing deeplink.
     *
     * This link has to be encoded and displayed as QR code, so that the user can scan it with
     * mobile app to do pairing.
     *
     * @return The pairing deeplink. If the machine is not paired or the SDK is not yet
     * authenticated, an empty string is returned.
     */
    std::string getPairDeeplink() const;

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
    std::string getClaimDeeplink(int player) const;

private:
    spimpl::unique_impl_ptr<detail::GameStateImpl> p;
};

} // namespace scorbit
