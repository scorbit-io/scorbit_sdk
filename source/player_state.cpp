/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "player_state.h"

namespace scorbit {
namespace detail {

PlayerState::PlayerState(sb_player_t player)
    : m_player(player)
{
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
    setChanged();
}

} // namespace detail
} // namespace scorbit
