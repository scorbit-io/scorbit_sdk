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
#include <nfc/probes_manager.h>
#include "net_base.h"
#include "game_data.h"
#include "event_classes.h"
#include <memory>
#include <string>

namespace scorbit {
namespace detail {

class GameStateImpl
{
public:
    GameStateImpl(std::unique_ptr<NetBase> net);

    void setGameStarted(GameStartOrigin origin);
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

    bool isPlayersInfoUpdated();
    std::optional<PlayerProfile> getPlayerProfile(sb_player_t player) const;
    const Picture &getPlayerPicture(sb_player_t player) const;

    void setCapabilities(Capabilities capabilities);

    void setCreditsDropped(int credits, const std::string &transaction, bool success);
    void setCreditsStatus(bool freePlay, int credits, int maxCredits, const char *pricing);

    void requestTopScores(sb_score_t scoreFilter, StringCallback callback);

    void requestPairCode(StringCallback callback) const;
    void requestUnpair(StringCallback callback) const;

    void requestPairMachine(const std::string &machineUuid, const std::string &ownerUuid,
                            StringCallback callback);

    void download(StringCallback callback, const std::string &url, const std::string &filename,
                  const std::string &contentType = {});
    void downloadBuffer(VectorCallback callback, const std::string &url, size_t reserveBufferSize,
                        const std::string &contentType = {});

private:
    void addNewPlayer(sb_player_t player);
    void submitGameData(bool forceSending);
    bool isChanged() const;
    bool isPlayerValid(sb_player_t player) const;
    bool isBallValid(sb_ball_t ball) const;
    bool startGame(int playersCount, GameStartOrigin origin);

private:
    // Members destroy in reverse declaration order. m_net must be destroyed FIRST so ~Net()
    // stops timers/worker and sets m_stop before GameData is destroyed; otherwise late
    // session-create replies can call submitGameData() on torn-down m_data.
    GameData m_data;
    GameData m_prevData;
    int m_sessionId {0};

    std::shared_ptr<nfc::ProbesManager> m_probesManager;
    std::unique_ptr<NetBase> m_net;
};

} // namespace detail
} // namespace scorbit
