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
#include "../achievements/rule_evaluator.h"
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

/** The API's answer that refuses a message for good. */
constexpr int HTTP_CONFLICT = 409;

/** The positions a game slot may have, as the API bounds them. */
constexpr sb_player_t MAX_POSITION = 6;

/** The `code: detail` of a refusal's `{code, detail}`, or its body when it carries none. */
std::string refusalOf(const ApiReply &reply)
{
    try {
        const auto document = json::parse(reply.body);
        if (document.is_object() && document.contains("code")) {
            return fmt::format("{}: {}", document.value("code", std::string {}),
                               document.value("detail", std::string {}));
        }
    } catch (const std::exception &) {
    }
    return reply.body;
}

const char *typeOf(MessageKind kind)
{
    return kind == MessageKind::GoalInstalled ? goalInstalledType() : attemptStartedType();
}

/**
 * Whether every rule of @p goal holds on the latest row of @p facts, for @p position: judged as an
 * `in_session` achievement no one has made progress on and that depends on nothing held, so only
 * this game counts.
 */
bool goalMet(const CompetitionGoal &goal, const achievements::SessionFacts &facts,
             achievements::PlayerNumber position)
{
    static const achievements::HeldSet nothingHeld;
    static const achievements::DefinitionSet noDefinitions;
    const achievements::EvaluationContext context {
            achievements::PlayerSession {facts, position, &goal.calendar}, nullptr, nothingHeld,
            noDefinitions};
    return achievements::evaluate(goal.goal, context).allSatisfied;
}

} // namespace

const std::string &bootId()
{
    static const std::string id = boost::uuids::to_string(boost::uuids::random_generator {}());
    return id;
}

CompetitionService::CompetitionService(NetBase &net, Poster poster, WallClock wallClock,
                                       std::string bootId)
    : m_net {net}
    , m_poster {std::move(poster)}
    , m_wallClock {std::move(wallClock)}
    , m_bootId {std::move(bootId)}
{
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
        // A repeated delivery: already held, and its install is answered or on its way
        INF("Competitions: attempt {} is already installed", attempt);
        return;
    }

    if (const auto here = toLowerAscii(m_net.venueMachine());
        !here.empty() && here != goal.venueMachine) {
        refuse(goal, fmt::format("it is for venue machine {}, this is {}", goal.venueMachine, here));
        return;
    }

    AttemptRecord record;
    record.goal = std::move(goal);
    record.installedAtMs = m_wallClock();

    INF("Competitions: installed attempt {} for player {}", attempt, record.goal.player);
    const auto &[it, inserted] = m_attempts.emplace(attempt, std::move(record));
    queueInstalled(it->second);
    pump();
}

void CompetitionService::refuse(const CompetitionGoal &goal, const std::string &reason)
{
    // Only logged: the host hears of a goal once Scorbit has confirmed it is installed
    WRN("Competitions: refusing attempt {}: {}", goal.identity.attempt, reason);
}

void CompetitionService::publishGoal(const CompetitionGoal &goal)
{
    m_net.publishEvent(std::make_shared<CompetitionGoalReceivedEvent>(
            goal.identity.attempt, goal.player, goal.position.value_or(0), goal.name));
}

// ---- Games -------------------------------------------------------------------------------------

void CompetitionService::onGameStarted(int gameSessionId, int64_t startedAtMs)
{
    // A game that never saw its end (a new one replaced it) can't start anything any more. It
    // ends where the new one starts, and is judged there once more, as at an end.
    // (Ids first: finishing a game may drop games.)
    std::vector<int> unfinished;
    for (const auto &game : m_games) {
        if (!game.finished) {
            unfinished.push_back(game.id);
        }
    }
    for (const auto id : unfinished) {
        onGameFinished(id, startedAtMs);
    }

    Game game;
    game.id = gameSessionId;
    game.startedAtMs = startedAtMs;
    // Only this machine's attempts: after a switch, or a restart paired to another machine, one
    // admitted on the previous machine must not start in this one's game
    const auto here = toLowerAscii(m_net.venueMachine());
    for (const auto &[attempt, record] : m_attempts) {
        if (!record.start && (here.empty() || record.goal.venueMachine == here)) {
            game.candidates.push_back(attempt);
        }
    }

    // A start outside the competition's window ends the attempt, so a start time from a clock
    // that may be wrong is never sent: the attempt waits for a game with a trusted clock
    if (!game.candidates.empty() && !m_net.isClockSynchronised()) {
        WRN("Competitions: game {} starts no attempt: the clock isn't synchronised",
            gameSessionId);
        game.candidates.clear();
    }

    // Only a game that may start an attempt takes an ordinal. Counted from 1 each boot: the boot
    // id tells one boot's from another's.
    if (!game.candidates.empty()) {
        game.sequence = ++m_lastSequence;
        INF("Competitions: game {} may start {} attempts, sequence {}", gameSessionId,
            game.candidates.size(), game.sequence);
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

    // The reply's own seats arrived just before, under its uuid. Matched even when the game has
    // ended since: they are that game's players. A start is only recorded from here on, so it
    // always carries the uuid it is sent with.
    if (const auto seats = std::ranges::find(m_unboundSeats, sessionUuid,
                                             &decltype(m_unboundSeats)::value_type::first);
        seats != m_unboundSeats.end()) {
        auto seated = std::move(seats->second);
        m_unboundSeats.erase(seats);
        matchPlayers(*game, seated);
    }
    dropSettledGames();
    pump();
}

void CompetitionService::onSessionPlayers(const std::string &sessionUuid, SessionSeats seats)
{
    // Only the seats of a game's own session start its attempts: the machine's profiles are
    // shared, and a late reply for another session rewrites them
    const auto it = std::ranges::find(m_games, sessionUuid, &Game::sessionUuid);
    if (it == m_games.end()) {
        // Most likely the create reply, whose uuid a game learns next: kept until then, but only
        // while some game still waits for its uuid. A late reply for a game already dropped is
        // let go. The oldest goes first once too many wait.
        const bool awaited = std::ranges::any_of(
                m_games, [](const Game &game) { return game.sessionUuid.empty(); });
        if (!awaited) {
            return;
        }
        std::erase_if(m_unboundSeats, [&](const auto &kept) { return kept.first == sessionUuid; });
        m_unboundSeats.emplace_back(sessionUuid, std::move(seats));
        while (m_unboundSeats.size() > MAX_KEPT_GAMES) {
            m_unboundSeats.pop_front();
        }
        return;
    }
    matchPlayers(*it, seats);
    pump();
}

void CompetitionService::onRow(const achievements::TimelineRow &row)
{
    // Only the game in progress takes rows, and only while it may start an attempt
    if (m_games.empty()) {
        return;
    }
    auto &game = m_games.back();
    if (game.finished || game.candidates.empty()) {
        return;
    }

    game.facts.apply(row);
    if (row.player > 0) {
        game.players.insert(row.player);
    }
    for (const auto &[player, score] : row.scores) {
        game.players.insert(player);
    }
    judgeGoals(game, row.timeMs);
}

void CompetitionService::onGameFinished(int gameSessionId, std::optional<int64_t> finishedAtMs)
{
    if (auto *game = gameOf(gameSessionId); game && !game->finished) {
        // The end closes the running turn and modes: judged once more, at the end
        if (finishedAtMs && !game->candidates.empty()) {
            game->facts.finish(*finishedAtMs);
            judgeGoals(*game, *finishedAtMs);
        }
        game->finished = true;
        // A late seat needs only what was met, not the facts it was judged from
        game->facts = {};
    }
    dropSettledGames();
}

void CompetitionService::judgeGoals(Game &game, int64_t timeMs)
{
    // One device event ordinal for the row, shared by every goal met on it
    std::optional<int64_t> sequence;
    for (const auto &attempt : game.candidates) {
        const auto it = m_attempts.find(attempt);
        if (it == m_attempts.end() || it->second.qualification) {
            continue; // refused since, or already qualified
        }
        auto &record = it->second;
        if (record.start && record.start->sequence != game.sequence) {
            continue; // started in another game, as when that game's reply came late
        }
        auto &metAt = game.metAt[attempt];

        // Once its player is seated only their position matters; until then, every position
        auto judge = [&](achievements::PlayerNumber position) {
            if (metAt.contains(position) || !goalMet(record.goal, game.facts, position)) {
                return;
            }
            if (!sequence) {
                sequence = ++m_lastSequence;
            }
            metAt.emplace(position, AttemptQualification {timeMs, *sequence});
        };
        if (record.start) {
            judge(static_cast<achievements::PlayerNumber>(record.start->position));
        } else {
            std::ranges::for_each(game.players, judge);
        }
        qualify(game, attempt, record);
    }
}

void CompetitionService::qualify(const Game &game, const std::string &attempt,
                                 AttemptRecord &record)
{
    // Only from the game it started in: game sequences are unique, and its start carries its own
    if (!record.start || record.qualification || record.start->sequence != game.sequence) {
        return;
    }
    const auto byAttempt = game.metAt.find(attempt);
    if (byAttempt == game.metAt.end()) {
        return;
    }
    const auto met =
            byAttempt->second.find(static_cast<achievements::PlayerNumber>(record.start->position));
    if (met == byAttempt->second.end()) {
        return;
    }
    record.qualification = met->second;
    INF("Competitions: attempt {} met its goal in game {} at position {}, sequence {}", attempt,
        game.id, record.start->position, met->second.sequence);
}

void CompetitionService::matchPlayers(Game &game, const SessionSeats &seats)
{
    for (const auto &attempt : game.candidates) {
        const auto it = m_attempts.find(attempt);
        if (it == m_attempts.end() || it->second.start) {
            continue; // refused since, or started already
        }
        auto &record = it->second;

        // The slot's player and the competition its Entry is for. A player may hold open attempts
        // for several competitions, e.g. after re-tapping to choose another before playing: only
        // the chosen one starts, or the first sent would take the slot from it (slot_taken).
        int position = 0;
        for (const auto &[slot, seat] : seats) {
            if (toLowerAscii(seat.userId) != record.goal.player) {
                continue;
            }
            if (toLowerAscii(seat.tournament) != record.goal.identity.tournament) {
                INF("Competitions: attempt {} not started: its player's slot {} is for {}", attempt,
                    slot, seat.tournament.empty() ? std::string {"no competition"} : seat.tournament);
                continue;
            }
            if (slot < 1 || slot > MAX_POSITION) {
                WRN("Competitions: attempt {} not started: its player is at position {}, beyond "
                    "{}",
                    attempt, slot, MAX_POSITION);
                continue;
            }
            position = static_cast<int>(slot);
            break;
        }
        if (position == 0) {
            continue;
        }

        record.start =
                AttemptStart {game.sessionUuid, position, game.startedAtMs, m_bootId, game.sequence};
        INF("Competitions: attempt {} started in game {} at position {}", attempt, game.id,
            position);
        queueStarted(record);
        // Its goal may have been met before a reply seated its player
        qualify(game, attempt, record);
    }
}

CompetitionService::Game *CompetitionService::gameOf(int gameSessionId)
{
    const auto it = std::ranges::find(m_games, gameSessionId, &Game::id);
    return it == m_games.end() ? nullptr : &*it;
}

void CompetitionService::dropSettledGames()
{
    // An ended game is kept while its session can still name its players: a late update for it
    // may seat a candidate. Only the latest few are kept, so a session that never got a uuid
    // (created offline) doesn't stay forever.
    std::erase_if(m_games, [](const Game &game) { return game.finished && game.candidates.empty(); });
    while (m_games.size() > MAX_KEPT_GAMES) {
        m_games.erase(m_games.begin());
    }
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
    if (record.start && !record.start->session.empty() && record.started == Delivery::Pending) {
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
        // The host hears of the goal once, when Scorbit first confirms it: a resend answered
        // `duplicate` after a lost reply counts, a later receipt doesn't
        const bool confirmed =
                message.kind == MessageKind::GoalInstalled && delivery == Delivery::Pending;
        delivery = *receipt == ReceiptStatus::Saved ? Delivery::Saved : Delivery::Duplicate;
        INF("Competitions: {} for attempt {}: {}", typeOf(message.kind), message.attempt,
            *receipt == ReceiptStatus::Saved ? "saved" : "duplicate");
        if (confirmed) {
            publishGoal(record.goal);
        }
    } else if (reply.httpStatus == HTTP_CONFLICT) {
        // The only final answer besides a receipt (ADR-0052): the attempt is over for the
        // server, so nothing of it is sent again, and it is forgotten here
        WRN("Competitions: {} for attempt {} refused, forgetting the attempt: {}",
            typeOf(message.kind), message.attempt, refusalOf(reply));
        m_outbox.drop(message.attempt);
        m_attempts.erase(it);
        m_outbox.answered();
        pump();
        return;
    } else {
        // Anything else is resent: a lost or failed request, but also a 4xx such as a failed
        // re-authentication, or an API not deployed yet
        const auto delay = m_outbox.failed();
        if (reply.error == Error::Success) {
            ERR("Competitions: {} for attempt {} answered without a receipt, resending in {} "
                "ms: {}",
                typeOf(message.kind), message.attempt, delay.count(), reply.body);
        } else {
            WRN("Competitions: {} for attempt {} got no final answer (status {}), resending in "
                "{} ms",
                typeOf(message.kind), message.attempt, reply.httpStatus, delay.count());
        }
        m_net.scheduleCompetitionRetry(delay, [this, alive = std::weak_ptr(m_alive)] {
            m_poster([this, alive] {
                if (!alive.expired()) {
                    onRetryTimer();
                }
            });
        });
        return;
    }

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
