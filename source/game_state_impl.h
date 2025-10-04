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

#include "scorbit_sdk/common_types_c.h"
#include "scorbit_sdk/event_types.h"
#include <spb/probes_manager.h>
#include "net_base.h"
#include "game_data.h"
#include "event_classes.h"
#include <string>
#include <memory>
#include <atomic>

namespace scorbit {
namespace detail {

class GameStateImpl
{
public:
    GameStateImpl(std::unique_ptr<NetBase> net);

    void setEventCallback(EventCallback &&callback);

    void setGameStarted();
    void setGameFinished();

    void setCurrentBall(sb_ball_t ball);

    void setActivePlayer(sb_player_t player);
    void setScore(sb_player_t player, sb_score_t score, sb_score_feature_t feature = 0);

    void addMode(std::string mode);
    void removeMode(const std::string &mode);
    void clearModes();

    void commit();

    AuthStatus getStatus() const;

    const std::string &getMachineUuid() const;
    const std::string &getPairDeeplink() const;
    const std::string &getClaimDeeplink(int player) const;

    bool isPlayersInfoUpdated();
    const PlayerProfile *getPlayerProfile(sb_player_t player) const;
    const Picture &getPlayerPicture(sb_player_t player) const;

    bool isGameStartRequested(int *playersCount);

    void requestTopScores(sb_score_t scoreFilter, StringCallback callback);

    void requestPairCode(StringCallback callback) const;
    void requestUnpair(StringCallback callback) const;

private:
    void addNewPlayer(sb_player_t player);
    void sendGameData();
    bool isChanged() const;
    bool isPlayerValid(sb_player_t player) const;
    bool isBallValid(sb_ball_t ball) const;
    bool startGame(int playersCount, GameStartOrigin origin);
    void gameStartRequested(int playersCount);

private:
    std::unique_ptr<NetBase> m_net;
    GameData m_data;
    GameData m_prevData;
    int m_sessionId {0};
    std::atomic_bool m_isGameStartRequested {false};

    std::shared_ptr<spb::ProbesManager> m_probesManager;
};

} // namespace detail
} // namespace scorbit
