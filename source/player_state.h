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
    explicit PlayerState(sb_player_t player, sb_score_t score = 0);

    sb_player_t player() const;

    sb_score_t score() const;
    sb_score_feature_t scoreFeature() const;

    void setScore(sb_score_t score, sb_score_feature_t feature = 0);

private:
    sb_score_t m_score {0};
    sb_player_t m_player {0};
    sb_score_feature_t m_scoreFeature {0};
};

inline bool operator<(const scorbit::detail::PlayerState &lhs,
                      const scorbit::detail::PlayerState &rhs)
{
    return lhs.player() < rhs.player();
}

inline bool operator==(const scorbit::detail::PlayerState &lhs,
                       const scorbit::detail::PlayerState &rhs)
{
    return lhs.player() == rhs.player() && lhs.score() == rhs.score()
        && lhs.scoreFeature() == rhs.scoreFeature();
}

} // namespace detail
} // namespace scorbit
