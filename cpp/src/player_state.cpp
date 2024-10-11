/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "player_state.h"

namespace scorbit {
namespace detail {

PlayerState::PlayerState(sb_player_t player, sb_score_t score)
    : m_player(player)
    , m_score {score}
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
    m_score = score;
}

} // namespace detail
} // namespace scorbit
