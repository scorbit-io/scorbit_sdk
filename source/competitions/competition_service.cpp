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

/** A refusal for a report naming another device, among others. */
constexpr int HTTP_FORBIDDEN = 403;

/** The positions a game slot may have, as the API bounds them. */
constexpr sb_player_t MAX_POSITION = 6;

/** A 409 that doesn't refuse: the server takes the start once the release is answered. */
constexpr auto RELEASE_PENDING = "release_pending";

/** A 409 to a report that doesn't refuse: the server takes the report once it has the start. */
constexpr auto START_PENDING = "start_pending";

/** The `code` of a refusal's `{code, detail}`; empty when it carries none. */
std::string refusalCode(const ApiReply &reply)
{
    try {
        const auto document = json::parse(reply.body);
        if (document.is_object()) {
            return document.value("code", std::string {});
        }
    } catch (const std::exception &) {
    }
    return {};
}

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
    switch (kind) {
    case MessageKind::GoalInstalled:
        return goalInstalledType();
    case MessageKind::AttemptStarted:
        return attemptStartedType();
    case MessageKind::Qualification:
        return qualificationType();
    case MessageKind::AttemptReleased:
        break;
    }
    return attemptReleasedType();
}

const char *describe(ReleaseState state)
{
    switch (state) {
    case ReleaseState::Released:
        return "dropped it";
    case ReleaseState::Started:
        return "kept it: its game had started";
    case ReleaseState::Unknown:
        break;
    }
    return "doesn't hold it";
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
    if (std::ranges::any_of(m_releases, [&](const ReleaseRecord &release) {
            // Dropped by the release, or by its refused `started` answer
            return release.release.identity.attempt == attempt && release.decided
                && (release.applied.state != ReleaseState::Started
                    || release.delivery == Delivery::Refused);
        })) {
        refuse(goal, "the attempt was released");
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

// ---- Releases ----------------------------------------------------------------------------------

void CompetitionService::onRelease(AttemptRelease release)
{
    if (auto *kept = releaseOf(release.control)) {
        // A resend: the same answer, sent again once decided (the server answers `duplicate`)
        INF("Competitions: release {} of attempt {} was applied already", release.control,
            release.identity.attempt);
        if (kept->decided) {
            queueReleased(*kept);
            pump();
        }
        return;
    }

    // Applied after the last row: a qualification on it or before came first
    auto &kept = m_releases.emplace_back(
            ReleaseRecord {std::move(release), {ReleaseState::Unknown, m_bootId, m_lastSequence}});
    const auto attempt = kept.release.identity.attempt;
    const auto it = m_attempts.find(attempt);
    if (it == m_attempts.end()) {
        settle(kept, ReleaseState::Unknown);
    } else if (it->second.start && it->second.started == Delivery::Pending) {
        // An unclaim releases a started attempt; leaving the lobby or a lapsed reservation one
        // the server hasn't seen start. Which, only the start's answer tells: until then the
        // attempt plays on.
        INF("Competitions: release {} of attempt {} at row {} waits for its start's answer",
            kept.release.control, attempt, kept.applied.sequence);
    } else {
        settle(kept, ReleaseState::Released);
    }
    dropAnsweredReleases();
    pump();
}

ReleaseRecord *CompetitionService::releaseOf(const std::string &control)
{
    const auto it = std::ranges::find(m_releases, control,
                                      [](const ReleaseRecord &r) { return r.release.control; });
    return it == m_releases.end() ? nullptr : &*it;
}

void CompetitionService::settle(ReleaseRecord &release, ReleaseState state, bool first)
{
    release.decided = true;
    release.applied.state = state;
    INF("Competitions: release {} of attempt {} applied at row {}: {}", release.release.control,
        release.release.identity.attempt, release.applied.sequence, describe(state));
    // A qualification on the release's row or before came first: the answer names its report, so
    // the server keeps the game used for the result on its way. One after the release counts for
    // nothing (`after_release`), so it is neither named nor sent.
    release.qualification = reportBefore(release.release.identity.attempt, release.applied.sequence);
    if (state == ReleaseState::Released) {
        // It stops timing, and never qualifies from here on
        forget(release.release.identity.attempt, !release.qualification.empty());
    }
    queueReleased(release, first);
}

void CompetitionService::settlePending(const std::string &attempt, ReleaseState state, bool first)
{
    // Newest first when each goes to the front, so they are sent oldest first. Going first, an
    // answer already decided but still owed is moved there too: behind the start, it would wait
    // for the start, which waits for it.
    auto apply = [&](ReleaseRecord &release) {
        if (release.release.identity.attempt != attempt) {
            return;
        }
        if (!release.decided) {
            settle(release, state, first);
        } else if (first && release.delivery == Delivery::Pending) {
            queueReleased(release, true);
        }
    };
    if (first) {
        std::for_each(m_releases.rbegin(), m_releases.rend(), apply);
    } else {
        std::ranges::for_each(m_releases, apply);
    }
}

void CompetitionService::forget(const std::string &attempt, bool keepReport)
{
    m_outbox.drop(attempt);
    if (!keepReport) {
        dropReportOf(attempt);
    }
    m_attempts.erase(attempt);
}

void CompetitionService::dropAnsweredReleases()
{
    // Only answered ones, and not while a resend of the answer is queued or in flight: one still
    // owed to the server is always kept. An older one forgotten no longer refuses a late goal for
    // its attempt; the server refuses that attempt's start and results.
    for (auto it = m_releases.begin();
         m_releases.size() > MAX_KEPT_RELEASES && it != m_releases.end();) {
        const bool answered = it->decided && it->delivery != Delivery::Pending
                           && !m_outbox.holdsRelease(it->release.control);
        it = answered ? m_releases.erase(it) : std::next(it);
    }
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
    sawSequence(firstRow.sequence);
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
    // Ending the game it replaced may have qualified an attempt
    pump();
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
    sawSequence(row.sequence);
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
    pump();
}

void CompetitionService::onGameFinished(int gameSessionId, uint64_t endSequence)
{
    sawSequence(endSequence);
    if (auto *game = gameOf(gameSessionId); game && !game->finished) {
        finishGame(*game, endSequence);
    }
    dropSettledGames();
    pump();
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
            report(record);
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

// ---- Qualification reports ---------------------------------------------------------------------

void CompetitionService::report(AttemptRecord &record)
{
    // Rendered once from the saved start and the timer's evidence, never the machine's current
    // state: the server compares both exactly with the start it saved
    record.reportId = generateUuid();
    auto &kept = m_reports.emplace_back(ReportRecord {
            QualificationReport {record.goal.identity, record.reportId, record.goal.player,
                                 *record.start, *record.qualification}});
    INF("Competitions: reporting attempt {}'s qualification as {}", record.goal.identity.attempt,
        record.reportId);
    queueReport(kept);
}

std::string CompetitionService::reportBefore(const std::string &attempt, uint64_t sequence) const
{
    // The attempt's own record, while held: an answered report may be gone from the list. Once a
    // release dropped the attempt, the report it kept: another release settled with it (two
    // controls waiting on the same start) must name it too, not drop it.
    if (const auto it = m_attempts.find(attempt); it != m_attempts.end()) {
        const auto &record = it->second;
        return record.qualification && record.qualification->completionSequence <= sequence
                     ? record.reportId
                     : std::string {};
    }
    const auto kept = std::ranges::find_if(m_reports, [&](const ReportRecord &r) {
        return r.report.identity.attempt == attempt
            && r.report.result.completionSequence <= sequence;
    });
    return kept == m_reports.end() ? std::string {} : kept->report.reportId;
}

ReportRecord *CompetitionService::reportOf(const std::string &reportId)
{
    const auto it = std::ranges::find(m_reports, reportId,
                                      [](const ReportRecord &r) { return r.report.reportId; });
    return it == m_reports.end() ? nullptr : &*it;
}

void CompetitionService::dropReportOf(const std::string &attempt)
{
    std::erase_if(m_reports, [&](const ReportRecord &kept) {
        if (kept.report.identity.attempt != attempt) {
            return false;
        }
        if (kept.delivery == Delivery::Pending) {
            INF("Competitions: dropping report {}: attempt {} can't be credited",
                kept.report.reportId, attempt);
        }
        m_outbox.dropReport(kept.report.reportId);
        return true;
    });
}

void CompetitionService::dropAnsweredReports()
{
    // One still owed to the server is always kept
    for (auto it = m_reports.begin();
         m_reports.size() > MAX_KEPT_REPORTS && it != m_reports.end();) {
        it = it->delivery != Delivery::Pending ? m_reports.erase(it) : std::next(it);
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

void CompetitionService::queueReleased(const ReleaseRecord &release, bool first)
{
    OutgoingMessage message {
            release.release.identity.attempt, MessageKind::AttemptReleased,
            attemptReleasedMessage(release.release, release.applied, release.qualification).dump(),
            release.release.control};
    if (first) {
        m_outbox.addFirst(std::move(message));
    } else {
        m_outbox.add(std::move(message));
    }
}

void CompetitionService::queueReport(const ReportRecord &report)
{
    if (report.delivery == Delivery::Pending) {
        OutgoingMessage message {report.report.identity.attempt, MessageKind::Qualification,
                                 qualificationMessage(report.report).dump()};
        message.report = report.report.reportId;
        m_outbox.add(std::move(message));
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
    if (message.kind == MessageKind::AttemptReleased) {
        onReleasedReply(message, reply);
        return;
    }
    if (message.kind == MessageKind::Qualification) {
        onReportReply(message, reply);
        return;
    }
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
        // The server had the start: a release waiting on it was an unclaim, and drops the attempt
        if (message.kind == MessageKind::AttemptStarted) {
            settlePending(message.attempt, ReleaseState::Released);
        }
    } else if (message.kind == MessageKind::AttemptStarted && reply.httpStatus == HTTP_CONFLICT
               && refusalCode(reply) == RELEASE_PENDING) {
        // Not a refusal (SB-6368 D3): the server takes the start once the release is answered.
        // A release waiting on it is `started`, and every answer still owed goes first.
        const auto delay = retryLater();
        WRN("Competitions: {} for attempt {} waits for a release's answer, resending in {} ms",
            typeOf(message.kind), message.attempt, delay.count());
        settlePending(message.attempt, ReleaseState::Started, true);
        return;
    } else if (reply.httpStatus == HTTP_CONFLICT) {
        // The only final answer besides a receipt (ADR-0052): the attempt is over for the
        // server, so nothing of it is sent again, and it is forgotten here. A release waiting on
        // it is answered `released`: the attempt is dropped.
        WRN("Competitions: {} for attempt {} refused, forgetting the attempt: {}",
            typeOf(message.kind), message.attempt, refusalOf(reply));
        // Forgotten first: its qualification, if any, can't be credited, so a release answered
        // here names none
        forget(message.attempt);
        settlePending(message.attempt, ReleaseState::Released);
        m_outbox.answered();
        pump();
        return;
    } else {
        // Anything else is resent: a lost or failed request, but also a 4xx such as a failed
        // re-authentication, or an API not deployed yet
        const auto delay = retryLater();
        if (reply.error == Error::Success) {
            ERR("Competitions: {} for attempt {} answered without a receipt, resending in {} "
                "ms: {}",
                typeOf(message.kind), message.attempt, delay.count(), reply.body);
        } else {
            WRN("Competitions: {} for attempt {} got no final answer (status {}), resending in "
                "{} ms",
                typeOf(message.kind), message.attempt, reply.httpStatus, delay.count());
        }
        return;
    }

    m_outbox.answered();
    pump();
}

void CompetitionService::onReleasedReply(const OutgoingMessage &message, const ApiReply &reply)
{
    auto *release = releaseOf(message.control);
    if (!release) {
        m_outbox.answered();
        pump();
        return;
    }

    std::optional<ReceiptStatus> receipt;
    if (reply.error == Error::Success) {
        try {
            receipt = parseReceipt(json::parse(reply.body), attemptReleasedType(),
                                   release->release.identity, release->release.control);
        } catch (const std::exception &) {
        }
    }

    if (receipt) {
        release->delivery =
                *receipt == ReceiptStatus::Saved ? Delivery::Saved : Delivery::Duplicate;
        INF("Competitions: {} {} for attempt {}: {}", attemptReleasedType(), message.control,
            message.attempt, *receipt == ReceiptStatus::Saved ? "saved" : "duplicate");
    } else if (reply.httpStatus == HTTP_CONFLICT) {
        // Final: the server answered the release otherwise, or knows no such release
        WRN("Competitions: {} {} for attempt {} refused: {}", attemptReleasedType(),
            message.control, message.attempt, refusalOf(reply));
        // Only the first answer counts: once saved, a refused resend (a newer release has
        // replaced this one) changes nothing
        if (release->delivery == Delivery::Pending) {
            // A `started` refused (`wrong_state`: the server had the start, so it was an unclaim)
            // leaves the attempt released for the server: it never plays on here
            if (release->applied.state == ReleaseState::Started) {
                // A qualification the answer named came before the release: still reported
                forget(message.attempt, !release->qualification.empty());
            }
            // Kept, not resent: a late goal for the attempt is still refused, and a repeated
            // release gets the same answer
            release->delivery = Delivery::Refused;
        }
    } else {
        const auto delay = retryLater();
        WRN("Competitions: {} {} for attempt {} got no final answer (status {}), resending in {} "
            "ms",
            attemptReleasedType(), message.control, message.attempt, reply.httpStatus,
            delay.count());
        return;
    }

    m_outbox.answered();
    dropAnsweredReleases();
    pump();
}

void CompetitionService::onReportReply(const OutgoingMessage &message, const ApiReply &reply)
{
    auto *kept = reportOf(message.report);
    if (!kept) {
        // Dropped while in flight: the server can't credit it
        m_outbox.answered();
        pump();
        return;
    }

    std::optional<QualificationReceipt> receipt;
    if (reply.error == Error::Success) {
        try {
            receipt = parseQualificationReceipt(json::parse(reply.body), kept->report.identity,
                                                kept->report.reportId);
        } catch (const std::exception &) {
        }
    }

    if (receipt) {
        // Every receipt is final: a conflict is kept for support, and resending changes nothing
        kept->receipt = receipt;
        switch (receipt->status) {
        case QualificationStatus::Saved:
        case QualificationStatus::Duplicate:
            kept->delivery = receipt->status == QualificationStatus::Saved ? Delivery::Saved
                                                                           : Delivery::Duplicate;
            INF("Competitions: {} {} for attempt {}: {}, case {}", qualificationType(),
                message.report, message.attempt,
                receipt->status == QualificationStatus::Saved ? "saved" : "duplicate",
                receipt->caseId);
            break;
        case QualificationStatus::Conflict:
            kept->delivery = Delivery::Refused;
            WRN("Competitions: {} {} for attempt {} kept as a conflict, not credited: {}, case {}",
                qualificationType(), message.report, message.attempt, receipt->reason,
                receipt->caseId);
            break;
        }
    } else if (reply.httpStatus == HTTP_CONFLICT && refusalCode(reply) == START_PENDING) {
        // Nothing saved: the server takes the report once it has the attempt's start. It is the
        // only 409 the API's QualificationView sends today, though its comment calls every 409
        // there retryable: a new retryable code must be added here, or reports get it as final.
        const auto delay = retryLater();
        WRN("Competitions: {} {} for attempt {} waits for the start, resending in {} ms",
            qualificationType(), message.report, message.attempt, delay.count());
        return;
    } else if (reply.httpStatus == HTTP_CONFLICT
               || (reply.httpStatus == HTTP_FORBIDDEN
                   && toLowerAscii(kept->report.result.evidence.deviceId)
                              != toLowerAscii(m_net.getMachineUuid()))) {
        // A 403 is final only when the report names another device than this one now is (its
        // game was timed before the machine was provisioned again): it can never be accepted.
        // Any other 403, such as a failed authentication, is resent below.
        WRN("Competitions: {} {} for attempt {} refused: {}", qualificationType(), message.report,
            message.attempt, refusalOf(reply));
        kept->delivery = Delivery::Refused;
    } else {
        const auto delay = retryLater();
        if (reply.error == Error::Success) {
            ERR("Competitions: {} {} for attempt {} answered without a receipt, resending in {} "
                "ms: {}",
                qualificationType(), message.report, message.attempt, delay.count(), reply.body);
        } else {
            WRN("Competitions: {} {} for attempt {} got no final answer (status {}), resending "
                "in {} ms",
                qualificationType(), message.report, message.attempt, reply.httpStatus,
                delay.count());
        }
        return;
    }

    m_outbox.answered();
    dropAnsweredReports();
    pump();
}

std::chrono::milliseconds CompetitionService::retryLater()
{
    const auto delay = m_outbox.failed();
    m_net.scheduleCompetitionRetry(delay, [this, alive = std::weak_ptr(m_alive)] {
        m_poster([this, alive] {
            if (!alive.expired()) {
                onRetryTimer();
            }
        });
    });
    return delay;
}

void CompetitionService::onRetryTimer()
{
    m_outbox.retryReady();
    pump();
}

} // namespace competitions
} // namespace detail
} // namespace scorbit
