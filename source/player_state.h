/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
