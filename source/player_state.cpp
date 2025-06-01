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

#include "player_state.h"

namespace scorbit {
namespace detail {

PlayerState::PlayerState(sb_player_t player, sb_score_t score)
    : m_score {score}
    , m_player(player)
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

sb_score_feature_t PlayerState::scoreFeature() const
{
    return m_scoreFeature;
}

void PlayerState::setScore(sb_score_t score, sb_score_feature_t feature)
{
    m_score = score;
    m_scoreFeature = feature;
}

} // namespace detail
} // namespace scorbit
