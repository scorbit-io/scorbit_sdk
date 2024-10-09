/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "player_state.h"

namespace scorbit {
namespace detail {

PlayerState::PlayerState(ChangeTracker &tracker, sb_player_t player, sb_score_t score)
    : m_player(player)
    , m_tracker(tracker)
{
    setScore(score); // To mark as changed if needed
}

sb_player_t PlayerState::player() const
{
    return m_player;
}

sb_score_t PlayerState::score() const
{
    return m_score;
}

void PlayerState::setScore(sb_score_t score)
{
    if (m_score == score)
        return;

    m_score = score;
    m_tracker.setChanged();
}

} // namespace detail
} // namespace scorbit
