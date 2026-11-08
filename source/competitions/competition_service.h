/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "attempt_store.h"
#include "message_outbox.h"
#include "messages.h"
#include "../net_base.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace competitions {

/** This run's boot id: a random uuid, generated once per process. */
const std::string &bootId();

/**
 * The machine side of a competition attempt (SB-6437, ADR-0052): installs the goals the API
 * sends, records the game start of each attempt once, and resends both to the API until it
 * answers.
 *
 * - A goal is saved durably before `goal_installed` is sent. A repeated delivery of an attempt
 *   changes nothing; the same attempt with another player or revision is refused.
 * - An attempt starts in the first game that began after it was installed, with a synchronised
 *   clock, in which its player holds a slot of the positions the API accepts (1 to 6). Who holds
 *   which slot is known from the game's own session replies: the create reply, then any update
 *   that seats a player later. The machine-wide profiles are never used, as a late reply for
 *   another session rewrites them.
 * - The start (the game's original UTC, this boot and a device event ordinal taken at the game
 *   start) is saved before it is sent, and sent once the session uuid is known.
 * - Only a receipt or a 409 answers a message. Anything else is resent. A 409 ends the attempt
 *   for the server, so it is forgotten here.
 * - Without a configured data dir goals are refused: saved to a temporary directory, an
 *   acknowledged install could be lost.
 *
 * Threading: every method runs on the thread game state lives on. Network replies and timers are
 * brought back to it through the poster.
 */
class CompetitionService
{
public:
    using Poster = std::function<void(std::function<void()>)>;
    using WallClock = std::function<int64_t()>;

    /** @p persistent: the store's data dir survives a restart (one was configured). */
    CompetitionService(NetBase &net, AttemptStore store, Poster poster, WallClock wallClock,
                       bool persistent = true, std::string bootId = competitions::bootId());
    ~CompetitionService();

    CompetitionService(const CompetitionService &) = delete;
    CompetitionService &operator=(const CompetitionService &) = delete;

    /** A `competition.goal` arrived and parsed. Raises CompetitionGoalReceived unless repeated. */
    void onGoal(CompetitionGoal goal);

    /** Game @p gameSessionId (game state's own id) started at @p startedAtMs, device UTC. */
    void onGameStarted(int gameSessionId, int64_t startedAtMs);

    /** The server created game @p gameSessionId's session and assigned it @p sessionUuid. */
    void onSessionCreated(int gameSessionId, const std::string &sessionUuid);

    /** A reply for session @p sessionUuid named who holds which of its slots. */
    void onSessionPlayers(const std::string &sessionUuid, SessionSeats seats);

    /** Game @p gameSessionId ended. */
    void onGameFinished(int gameSessionId);

    /** The attempts installed here, as kept on disk; for tests and diagnostics. */
    const std::map<std::string, AttemptRecord> &attempts() const { return m_attempts; }

private:
    /** A game in which installed attempts may start. */
    struct Game {
        int id {0};
        int64_t startedAtMs {0};
        /** The device event ordinal of the game start, shared by every attempt it starts. */
        int64_t sequence {0};
        std::string sessionUuid;
        bool finished {false};
        /** The attempts installed before the game started, and not started yet. */
        std::vector<std::string> candidates;
    };

    /** Games, and seats waiting for their game, kept at most. */
    static constexpr size_t MAX_KEPT_GAMES = 8;

    Game *gameOf(int gameSessionId);
    /** Starts every candidate of @p game whose player holds one of @p seats. */
    void matchPlayers(Game &game, const SessionSeats &seats);
    void dropSettledGames();

    void refuse(const CompetitionGoal &goal, const std::string &reason);
    void publishGoal(const CompetitionGoal &goal, bool installed, const std::string &reason);

    void queueInstalled(const AttemptRecord &record);
    void queueStarted(const AttemptRecord &record);
    void pump();
    void onReply(const OutgoingMessage &message, ApiReply reply);
    void onRetryTimer();

    NetBase &m_net;
    AttemptStore m_store;
    Poster m_poster;
    WallClock m_wallClock;
    bool m_persistent;
    std::string m_bootId;

    /** attempt → record. */
    std::map<std::string, AttemptRecord> m_attempts;
    std::vector<Game> m_games;
    /** Seats of a session whose game doesn't know its uuid yet: session uuid → seats. */
    std::map<std::string, SessionSeats> m_unboundSeats;
    MessageOutbox m_outbox;

    /** Guards the service against replies arriving after it was destroyed. */
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace competitions
} // namespace detail
} // namespace scorbit
