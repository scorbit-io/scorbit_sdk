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

void CompetitionService::onGameStarted(int gameSessionId, const achievements::TimelineRow &firstRow)
{
    // A game that never saw its end (a new one replaced it) can't start anything any more. It
    // ends on its last row, just before the new one's first, and is judged there once more, as at
    // an end. (0, no row's place, if the new row has none: then it ended out of sight.)
    const auto endSequence = firstRow.sequence > 0 ? firstRow.sequence - 1 : 0;
    for (auto &game : m_games) {
        if (!game.finished) {
            finishGame(game, endSequence);
        }
    }
    dropSettledGames();

    Game game;
    game.id = gameSessionId;
    game.startedAtMs = firstRow.timeMs;
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
    if (!game.candidates.empty() && !firstRow.clockSynchronised) {
        WRN("Competitions: game {} starts no attempt: the clock isn't synchronised",
            gameSessionId);
        game.candidates.clear();
    }
    // Nor one that can't be timed, or placed in the order of this boot's rows
    if (!game.candidates.empty() && (firstRow.sequence == 0 || !firstRow.elapsedMs)) {
        WRN("Competitions: game {} starts no attempt: its first row has no {}", gameSessionId,
            firstRow.sequence == 0 ? "place in the row order" : "elapsed time");
        game.candidates.clear();
    }

    if (!game.candidates.empty()) {
        // The start row's place in the order of rows, which the qualification's rows share
        game.startSequence = firstRow.sequence;
        INF("Competitions: game {} may start {} attempts, sequence {}", gameSessionId,
            game.candidates.size(), game.startSequence);

        // The game's facts start from its first row, as achievements' do. Each candidate is timed
        // at every position until a reply seats its player, possibly after the goal was met.
        game.startElapsedMs = *firstRow.elapsedMs;
        apply(game, firstRow);
        const TimingEvidence evidence {.deviceId = m_net.getMachineUuid(), .bootId = m_bootId};
        for (const auto &attempt : game.candidates) {
            const auto &goal = m_attempts.at(attempt).goal;
            auto &timers = game.timers[attempt];
            for (achievements::PlayerNumber position = 1; position <= MAX_POSITION; ++position) {
                auto &timer =
                        timers.emplace(position, TurnTimer {position, evidence}).first->second;
                timer.begin(firstRow, metAt(game, goal, position));
            }
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
    if (game.finished || game.timers.empty()) {
        return;
    }
    if (!judging(game)) {
        // Nothing left to time here: a late seat needs only the timers, not the facts
        game.facts = {};
        game.lastSequence = row.sequence;
        return;
    }

    apply(game, row);
    advance(game, &row);
    for (const auto &attempt : game.candidates) {
        qualifyFrom(game, attempt);
    }
}

void CompetitionService::onGameFinished(int gameSessionId, uint64_t endSequence)
{
    if (auto *game = gameOf(gameSessionId); game && !game->finished) {
        finishGame(*game, endSequence);
    }
    dropSettledGames();
}

void CompetitionService::apply(Game &game, const achievements::TimelineRow &row)
{
    // The goal's timers are judged on the elapsed clock the turn timer runs on, placed at the
    // start's UTC: a UTC correction during the game would add to them or take from them. A row
    // without an elapsed time stops every timer, so it moves nothing.
    auto timed = row;
    timed.timeMs = row.elapsedMs ? game.startedAtMs + (*row.elapsedMs - game.startElapsedMs)
                                 : game.lastTimeMs;
    game.facts.apply(timed);
    game.lastSequence = row.sequence;
    game.lastTimeMs = timed.timeMs;
    // Only the positions the API accepts can ever be an attempt's
    auto appear = [&](achievements::PlayerNumber player) {
        if (player >= 1 && player <= MAX_POSITION) {
            game.players.insert(player);
        }
    };
    appear(row.player);
    for (const auto &[player, score] : row.scores) {
        appear(player);
    }
}

bool CompetitionService::metAt(const Game &game, const CompetitionGoal &goal,
                               achievements::PlayerNumber position) const
{
    return game.players.contains(position) && goalMet(goal, game.facts, position);
}

bool CompetitionService::judging(const Game &game) const
{
    return std::ranges::any_of(game.timers, [&](const auto &byAttempt) {
        const auto it = m_attempts.find(byAttempt.first);
        return it != m_attempts.end() && mayQualifyIn(game, it->second)
            && std::ranges::any_of(byAttempt.second, [](const auto &timer) {
                   return timer.second.state() == TurnTimer::State::Running;
               });
    });
}

bool CompetitionService::mayQualifyIn(const Game &game, const AttemptRecord &record)
{
    // A start carries its game's start row, unique within the boot: one with another was seated
    // by a reply for another game
    return !record.qualification && (!record.start || record.start->sequence == game.startSequence);
}

void CompetitionService::advance(Game &game, const achievements::TimelineRow *row)
{
    for (auto &[attempt, timers] : game.timers) {
        const auto it = m_attempts.find(attempt);
        if (it == m_attempts.end() || !mayQualifyIn(game, it->second)) {
            continue; // refused since, qualified, or started in another game
        }
        for (auto &[position, timer] : timers) {
            if (timer.state() != TurnTimer::State::Running) {
                continue;
            }
            // Every position takes every row, met or not: a missing one stops its timer
            const bool met = metAt(game, it->second.goal, position);
            if (row) {
                timer.onRow(*row, met);
            } else {
                timer.endAtLastRow(met);
            }
        }
    }
}

void CompetitionService::finishGame(Game &game, uint64_t endSequence)
{
    if (!game.timers.empty()) {
        if (game.lastSequence == endSequence) {
            // The end closes the running turn and modes: judged once more, on the game's last row
            game.facts.finish(game.lastTimeMs);
            advance(game, nullptr);
        } else {
            // The game's end row, or rows before it, never arrived: what came after the last one
            // seen is unknown, so nothing is timed to it
            WRN("Competitions: game {} ended at row {}, its last row seen is {}: its attempts "
                "can't qualify in it",
                game.id, endSequence, game.lastSequence);
            for (auto &[attempt, timers] : game.timers) {
                for (auto &[position, timer] : timers) {
                    timer.interrupt("the game's end row is missing");
                }
            }
        }
        for (const auto &attempt : game.candidates) {
            qualifyFrom(game, attempt);
        }
    }
    game.finished = true;
    // A late seat needs only the timers, not the facts they were judged from
    game.facts = {};
}

void CompetitionService::qualifyFrom(Game &game, const std::string &attempt)
{
    const auto byAttempt = game.timers.find(attempt);
    if (byAttempt == game.timers.end()) {
        return;
    }
    const auto it = m_attempts.find(attempt);
    if (it == m_attempts.end() || !mayQualifyIn(game, it->second)) {
        game.timers.erase(byAttempt); // refused since, or started in another game: never counts
        return;
    }
    // Only once its player is seated: until then every position is timed
    auto &record = it->second;
    if (!record.start) {
        return;
    }
    const auto timer =
            byAttempt->second.find(static_cast<achievements::PlayerNumber>(record.start->position));
    if (timer == byAttempt->second.end()) {
        return;
    }

    switch (timer->second.state()) {
    case TurnTimer::State::Idle:
    case TurnTimer::State::Running:
        return;
    case TurnTimer::State::Completed:
        if (!record.qualification) {
            record.qualification = *timer->second.result();
            INF("Competitions: attempt {} met its goal in game {} at position {} in {} ms, row {}",
                attempt, game.id, record.start->position, record.qualification->durationMs,
                record.qualification->completionSequence);
        }
        break;
    case TurnTimer::State::Ended:
        INF("Competitions: attempt {} didn't meet its goal in game {}", attempt, game.id);
        break;
    case TurnTimer::State::Interrupted:
        WRN("Competitions: attempt {} can't qualify in game {}: {}", attempt, game.id,
            timer->second.interruption().value_or(""));
        break;
    }
    // What came of the attempt's game is final
    game.timers.erase(byAttempt);
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

        record.start = AttemptStart {game.sessionUuid, position, game.startedAtMs, m_bootId,
                                     game.startSequence};
        INF("Competitions: attempt {} started in game {} at position {}", attempt, game.id,
            position);
        queueStarted(record);

        // Only its own position's time counts from now on. Its goal may have been met, or the
        // game ended, before a reply seated its player.
        if (const auto timers = game.timers.find(attempt); timers != game.timers.end()) {
            std::erase_if(timers->second, [position](const auto &timer) {
                return timer.first != static_cast<achievements::PlayerNumber>(position);
            });
        }
        qualifyFrom(game, attempt);
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
