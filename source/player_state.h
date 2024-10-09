/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/common_types_c.h>
#include "change_tracker.h"

namespace scorbit {
namespace detail {

class PlayerState
{
public:
    explicit PlayerState(ChangeTracker &tracker, sb_player_t player, sb_score_t score = -1);

    sb_player_t player() const;

    sb_score_t score() const;
    void setScore(sb_score_t score);

private:
    sb_player_t m_player;
    sb_score_t m_score {-1};
    ChangeTracker &m_tracker;
};

inline bool operator<(const scorbit::detail::PlayerState &lhs,
                      const scorbit::detail::PlayerState &rhs)
{
    return lhs.player() < rhs.player();
}

inline bool operator==(const scorbit::detail::PlayerState &lhs,
                       const scorbit::detail::PlayerState &rhs)
{
    return lhs.player() == rhs.player() && lhs.score() == rhs.score();
}

} // namespace detail
} // namespace scorbit
