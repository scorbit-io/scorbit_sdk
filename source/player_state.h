/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/common_types_c.h>

namespace scorbit {
namespace detail {

class PlayerState
{
public:
    explicit PlayerState(sb_player_t player);

    bool isChanged() const { return m_isChanged; }
    void clearChanged() { m_isChanged = false; }

    sb_player_t player() const;

    sb_score_t score() const;
    void setScore(sb_score_t score);

private:
    void setChanged() { m_isChanged = true; }

private:
    const sb_player_t m_player;
    sb_score_t m_score {-1};

    bool m_isChanged {false};
};

} // namespace detail
} // namespace scorbit
