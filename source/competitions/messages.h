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

/*
 * The competition attempt messages, version 1 (SB-6437, ADR-0052). The API fixes their shape in
 * core/competition_messages.py, with one JSON example each in core/contracts/competition/v1/.
 *
 * | type                              | direction    | carried by                          |
 * |-----------------------------------|--------------|-------------------------------------|
 * | competition.goal                  | API → device | control_machine, `{type, payload}`  |
 * | competition.goal_installed        | device → API | POST competitions/goal-installed/   |
 * | competition.goal_installed_saved  | API → device | its 200 reply                       |
 * | competition.attempt_started       | device → API | POST competitions/attempt-started/  |
 * | competition.attempt_started_saved | API → device | its 200 reply                       |
 * | competition.attempt_control       | API → device | control_machine, `{type, payload}`  |
 * | competition.attempt_released      | device → API | POST competitions/attempt-released/ |
 * | competition.attempt_released_saved| API → device | its 200 reply                       |
 * | competition.qualification         | device → API | POST competitions/qualification/    |
 * | competition.qualification_saved   | API → device | its 200 reply                       |
 */

#include "turn_timer.h"
#include "../achievements/definition.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace scorbit {
namespace detail {
namespace competitions {

/** Every message names the competition, its saved copy, the copy's revision and the attempt. */
struct AttemptIdentity {
    std::string tournament;
    /** The competition's saved copy (`CompetitionRules`). */
    std::string rules;
    /** The copy's `definition_checksum`. */
    std::string checksum;
    /** The attempt token: the uuid the API issued for one designated game. */
    std::string attempt;

    bool operator==(const AttemptIdentity &) const = default;
};

/** `competition.goal`: the saved goal, delivered for one attempt. */
struct CompetitionGoal {
    AttemptIdentity identity;
    /** The admitted player's user uuid, as the profiles of a game's players carry it. */
    std::string player;
    std::string venueMachine;
    /** Where the API expects the player to play; a hint, the game decides. */
    std::optional<int> position;
    /** The goal achievement's name, for the host to show; empty when the definition has none. */
    std::string name;
    /** The complete saved definition, kept as delivered. */
    nlohmann::json definition;
    /**
     * The saved definition's rules, in their saved order, as the evaluator judges them: parsed once
     * at install, never from the profile's current definition (SB-6366).
     */
    achievements::Definition goal;
    /** The saved definition's own calendar envelope; empty when it has no `CALENDAR` rule. */
    achievements::CalendarData calendar;
    /** The message as delivered. */
    nlohmann::json message;
};

/**
 * Parses a `competition.goal` payload. A version, definition format, timer policy or rule type
 * this SDK doesn't support refuses the whole message, as does any rule that doesn't parse, or one
 * the evaluator can't judge at a single instant (see @ref findInstantProblem): a goal is never
 * installed with a rule skipped or approximated. On refusal returns nullopt and says why in
 * @p error.
 */
std::optional<CompetitionGoal> parseGoal(const nlohmann::json &payload, std::string &error);

/**
 * Why @p goal can't be judged by checking, row by row, whether every rule holds together; nullopt
 * when it can (SB-6366).
 *
 * A competition completes on the first row where all of its rules hold at once. The evaluator
 * judges most rules on what is true at that row: a count, a register's net value, the time played
 * so far, or a fact that stays true once it happened (a mode ran, a span closed, a score was
 * reached). Two kinds would let an earlier moment stand in for the row, and so invent a
 * completion that never occurred:
 *
 * - `SCORE` other than `>=`: any earlier score is kept, so the `0` at the start meets `<= 1000`
 *   for the whole game;
 * - `TIMER_BALL` and `TIMER_MODE` other than `>=`: a ball or activation still running is measured
 *   so far, so one at 2 s meets `<= 10 s` though it goes on to last a minute;
 * - `TIMER_BETWEEN` other than `<=` over several siblings, or beside a `CALENDAR` rule: a later
 *   occurrence on the same ball shortens the span, so 10 s meets `>= 8 s` though the ball ends
 *   with the facts 1 s apart.
 *
 * They are refused, never approximated.
 */
std::optional<std::string> findInstantProblem(const achievements::Definition &goal);

/** The device's record of when an attempt's game started, sent as `attempt_started`. */
struct AttemptStart {
    /** The Session uuid the server assigned the game; empty until its reply arrives. */
    std::string session;
    int position {0};
    /** The game's start, device UTC. */
    int64_t startedAtMs {0};
    std::string bootId;
    /** The device's ordinal for the start event: its start row's place in the order of rows. */
    uint64_t sequence {0};

    bool operator==(const AttemptStart &) const = default;
};

/**
 * `competition.attempt_control` with action `release` (SB-6368): the player unclaimed, left the
 * lobby, or their reservation lapsed, and the device is to drop the attempt.
 */
struct AttemptRelease {
    AttemptIdentity identity;
    /** The release's stable id: every resend of it repeats it, and the answer names it. */
    std::string control;
};

/**
 * Parses a `competition.attempt_control` payload. Only `release` is applied: any other action, or
 * a release without its `control`, is refused, returning nullopt and saying why in @p error.
 */
std::optional<AttemptRelease> parseAttemptControl(const nlohmann::json &payload,
                                                  std::string &error);

/** What became of a release on the device: the `state` of `attempt_released`. */
enum class ReleaseState {
    Released, ///< The attempt was held, and is dropped
    Started,  ///< Its game had started before the API saved the start: the attempt is kept
    Unknown,  ///< It isn't held here, e.g. after a restart
};

/** Where the device applied a release, sent as `attempt_released`. */
struct ReleaseApplied {
    ReleaseState state {ReleaseState::Unknown};
    std::string bootId;
    /** The device's ordinal for applying the release, from the order of rows the start shares. */
    uint64_t sequence {0};

    bool operator==(const ReleaseApplied &) const = default;
};

/**
 * `competition.qualification` (SB-6781): an attempt's result, reported once under a report id the
 * device chose, and resent unchanged until the server answers.
 */
struct QualificationReport {
    AttemptIdentity identity;
    /** The report's stable id, a uuid: every resend repeats it, and a release's answer names it. */
    std::string reportId;
    /** The admitted player's user uuid. */
    std::string player;
    /** The attempt's start, as sent: the server compares its session, position and time with it. */
    AttemptStart start;
    /** The player's time to the completing row, and what it was measured on. */
    TurnTimeResult result;
};

/** `competition.goal_installed`. */
const char *goalInstalledType();

/** `competition.attempt_started`. */
const char *attemptStartedType();

/** `competition.attempt_released`. */
const char *attemptReleasedType();

/** `competition.qualification`. */
const char *qualificationType();

/** `competition.goal_installed`, the goal having been saved at @p installedAtMs. */
nlohmann::json goalInstalledMessage(const AttemptIdentity &identity, int64_t installedAtMs);

/** `competition.attempt_started`. */
nlohmann::json attemptStartedMessage(const AttemptIdentity &identity, const AttemptStart &start);

/**
 * `competition.attempt_released`, answering @p release. Its `qualification` is @p qualification,
 * the report id of a qualification made on or before the release's row, or null when empty.
 */
nlohmann::json attemptReleasedMessage(const AttemptRelease &release, const ReleaseApplied &applied,
                                      const std::string &qualification = {});

/**
 * `competition.qualification`. Its start and device are the attempt's start and the timer's
 * evidence, never the machine's current state: the server compares them exactly.
 */
nlohmann::json qualificationMessage(const QualificationReport &report);

/** What the server did with a message it accepted. */
enum class ReceiptStatus { Saved, Duplicate };

/**
 * Parses the 200 reply to a message of @p sentType about @p identity: its receipt, of type
 * `<sentType>_saved`. nullopt for anything else, including a receipt for another attempt, or,
 * when @p control is given, for another release.
 */
std::optional<ReceiptStatus> parseReceipt(const nlohmann::json &reply, const std::string &sentType,
                                          const AttemptIdentity &identity,
                                          const std::string &control = {});

/** What the server did with a qualification report: every status is final. */
enum class QualificationStatus {
    Saved,     ///< Saved and credited
    Duplicate, ///< Saved and credited already
    Conflict,  ///< Kept for support, credited nothing; `reason` says why
};

/** `competition.qualification_saved`: the 200 reply to a qualification report. */
struct QualificationReceipt {
    QualificationStatus status {QualificationStatus::Saved};
    /** Why a conflict wasn't credited; empty otherwise. */
    std::string reason;
    /** The saved qualification, or the kept conflict, for support. */
    std::string caseId;

    bool operator==(const QualificationReceipt &) const = default;
};

/**
 * Parses the 200 reply to report @p reportId about @p identity. nullopt for anything else,
 * including a receipt for another attempt or another report.
 */
std::optional<QualificationReceipt> parseQualificationReceipt(const nlohmann::json &reply,
                                                              const AttemptIdentity &identity,
                                                              const std::string &reportId);

/** @p ms since the epoch as ISO 8601 UTC with milliseconds, e.g. `2026-11-06T20:00:00.250Z`. */
std::string formatUtc(int64_t ms);

} // namespace competitions
} // namespace detail
} // namespace scorbit
