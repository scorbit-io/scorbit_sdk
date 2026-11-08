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

#include "competition_service.h"
#include "../event_classes.h"
#include "../net_util.h"
#include <logger/logger.h>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <utility>

namespace scorbit {
namespace detail {
namespace competitions {

using json = nlohmann::json;

namespace {

/** Slots a profile may hold, as game state numbers them. */
constexpr sb_player_t MAX_PLAYERS = 9;

/** A transport failure or a server-side one is worth retrying; a refused request is not. */
bool isRetryable(const ApiReply &reply)
{
    return reply.httpStatus == 0 || reply.httpStatus == 408 || reply.httpStatus == 429
        || reply.httpStatus >= 500;
}

/** The `code` of a refusal's `{code, detail}`, or its status when it carries none. */
std::string refusalCode(const ApiReply &reply)
{
    try {
        const auto document = json::parse(reply.body);
        if (document.is_object() && document.contains("code") && document["code"].is_string()) {
            return document["code"].get<std::string>();
        }
    } catch (const std::exception &) {
    }
    return fmt::format("http_{}", reply.httpStatus);
}

const char *typeOf(MessageKind kind)
{
    return kind == MessageKind::GoalInstalled ? goalInstalledType() : attemptStartedType();
}

} // namespace

const std::string &bootId()
{
    static const std::string id = boost::uuids::to_string(boost::uuids::random_generator {}());
    return id;
}

CompetitionService::CompetitionService(NetBase &net, AttemptStore store, Poster poster,
                                       WallClock wallClock, std::string bootId)
    : m_net {net}
    , m_store {std::move(store)}
    , m_poster {std::move(poster)}
    , m_wallClock {std::move(wallClock)}
    , m_bootId {std::move(bootId)}
{
    // Whatever the API hasn't answered yet is sent again, as it was saved
    for (auto &record : m_store.load()) {
        auto attempt = record.goal.identity.attempt;
        const auto &[it, inserted] = m_attempts.emplace(std::move(attempt), std::move(record));
        queueInstalled(it->second);
        queueStarted(it->second);
    }
    if (!m_attempts.empty()) {
        INF("Competitions: {} attempts kept on this machine", m_attempts.size());
    }
    pump();
}

CompetitionService::~CompetitionService()
{
    *m_alive = false;
}

// ---- Goals -------------------------------------------------------------------------------------

void CompetitionService::onGoal(CompetitionGoal goal)
{
    // A copy: the goal is moved into its record below
    const auto attempt = goal.identity.attempt;
    if (const auto it = m_attempts.find(attempt); it != m_attempts.end()) {
        const auto &kept = it->second.goal;
        if (kept.identity != goal.identity || kept.player != goal.player) {
            refuse(goal, kept.player != goal.player
                                 ? "the attempt is installed for another player"
                                 : "the attempt is installed with another revision");
            return;
        }
        // A repeated delivery: already saved, and its install is answered or on its way
        INF("Competitions: attempt {} is already installed", attempt);
        return;
    }

    AttemptRecord record;
    record.goal = std::move(goal);
    record.installedAtMs = m_wallClock();
    if (!m_store.save(record)) {
        refuse(record.goal, "it could not be saved");
        return;
    }

    INF("Competitions: installed attempt {} for player {}", attempt, record.goal.player);
    const auto &[it, inserted] = m_attempts.emplace(attempt, std::move(record));
    publishGoal(it->second.goal, true, {});
    queueInstalled(it->second);
    pump();
}

void CompetitionService::refuse(const CompetitionGoal &goal, const std::string &reason)
{
    WRN("Competitions: refusing attempt {}: {}", goal.identity.attempt, reason);
    publishGoal(goal, false, reason);
}

void CompetitionService::publishGoal(const CompetitionGoal &goal, bool installed,
                                     const std::string &reason)
{
    m_net.publishEvent(std::make_shared<CompetitionGoalReceivedEvent>(
            installed, goal.identity.attempt, goal.player, goal.position.value_or(0), reason));
}

// ---- Games -------------------------------------------------------------------------------------

void CompetitionService::onGameStarted(int gameSessionId, int64_t startedAtMs)
{
    // A game that never saw its end (a new one replaced it) can't start anything any more
    for (auto &game : m_games) {
        game.finished = true;
    }

    Game game;
    game.id = gameSessionId;
    game.startedAtMs = startedAtMs;
    for (const auto &[attempt, record] : m_attempts) {
        if (!record.closed()) {
            game.candidates.push_back(attempt);
        }
    }

    // Only a game that may start an attempt takes an ordinal, so others cost no write
    if (!game.candidates.empty()) {
        const auto sequence = m_store.nextSequence();
        if (!sequence) {
            ERR("Competitions: game {} starts no attempt: no device event ordinal",
                gameSessionId);
            game.candidates.clear();
        } else {
            game.sequence = *sequence;
            INF("Competitions: game {} may start {} attempts, sequence {}", gameSessionId,
                game.candidates.size(), game.sequence);
        }
    }

    m_games.push_back(std::move(game));
    dropSettledGames();
}

void CompetitionService::onSessionCreated(int gameSessionId, const std::string &sessionUuid)
{
    auto *game = gameOf(gameSessionId);
    if (!game) {
        return;
    }
    game->sessionUuid = sessionUuid;

    // The reply carried the game's players, even when the game has ended since. A start is only
    // recorded from here on, so it always carries the uuid it is sent with.
    matchPlayers(*game);
    dropSettledGames();
    pump();
}

void CompetitionService::onPlayersChanged()
{
    // Before the server created the session the profiles may still be the previous game's
    if (!m_games.empty() && !m_games.back().finished && !m_games.back().sessionUuid.empty()) {
        matchPlayers(m_games.back());
        pump();
    }
}

void CompetitionService::onGameFinished(int gameSessionId)
{
    if (auto *game = gameOf(gameSessionId)) {
        game->finished = true;
    }
    dropSettledGames();
}

void CompetitionService::matchPlayers(Game &game)
{
    for (const auto &attempt : game.candidates) {
        auto &record = m_attempts.at(attempt);
        if (record.closed()) {
            continue;
        }

        sb_player_t position = 0;
        for (sb_player_t slot = 1; slot <= MAX_PLAYERS && position == 0; ++slot) {
            const auto profile = m_net.playersManager().profile(slot);
            if (profile && profile->hasInfo() && toLowerAscii(profile->id) == record.goal.player) {
                position = slot;
            }
        }
        if (position == 0) {
            continue;
        }

        record.start = AttemptStart {game.sessionUuid, static_cast<int>(position),
                                     game.startedAtMs, m_bootId, game.sequence};
        if (!m_store.save(record)) {
            // Not started, so that it isn't sent unsaved; a later match may save it
            record.start.reset();
            continue;
        }
        INF("Competitions: attempt {} started in game {} at position {}", attempt, game.id,
            position);
        queueStarted(record);
    }
}

CompetitionService::Game *CompetitionService::gameOf(int gameSessionId)
{
    const auto it = std::ranges::find(m_games, gameSessionId, &Game::id);
    return it == m_games.end() ? nullptr : &*it;
}

void CompetitionService::dropSettledGames()
{
    // An ended game is kept until its session uuid arrives: its starts wait for it
    std::erase_if(m_games, [](const Game &game) {
        return game.finished && (!game.sessionUuid.empty() || game.candidates.empty());
    });
}

// ---- Delivery ----------------------------------------------------------------------------------

void CompetitionService::queueInstalled(const AttemptRecord &record)
{
    if (record.installed == Delivery::Pending) {
        m_outbox.add({record.goal.identity.attempt, MessageKind::GoalInstalled,
                      goalInstalledMessage(record.goal.identity, record.installedAtMs).dump()});
    }
}

void CompetitionService::queueStarted(const AttemptRecord &record)
{
    if (record.start && !record.start->session.empty() && record.started == Delivery::Pending
        && record.installed != Delivery::Refused) {
        m_outbox.add({record.goal.identity.attempt, MessageKind::AttemptStarted,
                      attemptStartedMessage(record.goal.identity, *record.start).dump()});
    }
}

void CompetitionService::pump()
{
    const auto message = m_outbox.next();
    if (!message) {
        return;
    }
    auto replied = [this, alive = std::weak_ptr(m_alive), message = *message](ApiReply reply) {
        m_poster([this, alive, message, reply = std::move(reply)]() mutable {
            if (!alive.expired()) {
                onReply(message, std::move(reply));
            }
        });
    };
    m_net.postCompetitionMessage(message->kind, message->body, std::move(replied));
}

void CompetitionService::onReply(const OutgoingMessage &message, ApiReply reply)
{
    const auto it = m_attempts.find(message.attempt);
    if (it == m_attempts.end()) {
        m_outbox.answered();
        pump();
        return;
    }
    auto &record = it->second;
    auto &delivery =
            message.kind == MessageKind::GoalInstalled ? record.installed : record.started;

    std::optional<ReceiptStatus> receipt;
    if (reply.error == Error::Success) {
        try {
            receipt = parseReceipt(json::parse(reply.body), typeOf(message.kind),
                                   record.goal.identity);
        } catch (const std::exception &) {
        }
    }

    if (receipt) {
        delivery = *receipt == ReceiptStatus::Saved ? Delivery::Saved : Delivery::Duplicate;
        INF("Competitions: {} for attempt {}: {}", typeOf(message.kind), message.attempt,
            *receipt == ReceiptStatus::Saved ? "saved" : "duplicate");
    } else if (reply.error != Error::Success && !isRetryable(reply)) {
        // A named refusal is final: never resent
        delivery = Delivery::Refused;
        record.refusal = refusalCode(reply);
        WRN("Competitions: {} for attempt {} refused: {}", typeOf(message.kind),
            message.attempt, reply.body);
    } else {
        const auto delay = m_outbox.failed();
        WRN("Competitions: {} for attempt {} got no answer (status {}), resending in {} ms",
            typeOf(message.kind), message.attempt, reply.httpStatus, delay.count());
        m_net.scheduleCompetitionRetry(delay, [this, alive = std::weak_ptr(m_alive)] {
            m_poster([this, alive] {
                if (!alive.expired()) {
                    onRetryTimer();
                }
            });
        });
        return;
    }

    // Saved after the answer: if it can't be, the message is sent again after a restart and
    // answered `duplicate`, which is harmless
    m_store.save(record);
    m_outbox.answered();
    pump();
}

void CompetitionService::onRetryTimer()
{
    m_outbox.retryReady();
    pump();
}

} // namespace competitions
} // namespace detail
} // namespace scorbit
