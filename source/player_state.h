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

class PlayerState : public ChangeTracker
{
public:
    explicit PlayerState(sb_player_t player);

    sb_player_t player() const;

    sb_score_t score() const;
    void setScore(sb_score_t score);

private:
    const sb_player_t m_player;
    sb_score_t m_score {-1};
};

} // namespace detail
} // namespace scorbit
