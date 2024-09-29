/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/common_types_c.h>
#include <scorbit_sdk/export.h>

#include "spimpl/spimpl.h"

#include <string>

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
 * The game must be marked as started by calling @ref setGameStarted before modifying the state.
 *
 * To apply changes to the game state, call the @ref commit function. This function applies all
 * changes made to the game state. The game state is not updated until the commit function is
 * called.
 *
 * @warning If the game is not active, all calls to modify the game state, as well as @ref commit,
 * will be ignored.
 */
class SCORBIT_SDK_EXPORT GameState
{
public:
    GameState();

    /**
     * @brief Mark the game as started.
     *
     * Indicates that the game has started. Call this function when the game begins.
     *
     * @note This function automatically commits changes using @ref commit.
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
     * @brief Set the active player.
     *
     * Updates the current active player in the game. By default, player 1 is the active player.
     *
     * @param player The player's number [1-6]. Typically, up to 6 players are supported in pinball.
     * If the player number is out of range, the function does nothing.
     */
    void setActivePlayer(sb_player_t player);

    /**
     * @brief Set the player's score.
     *
     * Updates the specified player's score. If the new score is the same as the current score,
     * no update is made.
     *
     * @param player The player's number [1-6]. If the player number is out of range, the function
     * does nothing.
     * @param score The player's new score.
     */
    void setScore(sb_player_t player, sb_score_t score);

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

private:
    spimpl::unique_impl_ptr<detail::GameStateImpl> p;
};

} // namespace scorbit
