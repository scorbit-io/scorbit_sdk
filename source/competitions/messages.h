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
 */

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
    /** The device's ordinal for the start event. */
    int64_t sequence {0};

    bool operator==(const AttemptStart &) const = default;
};

/** `competition.goal_installed`. */
const char *goalInstalledType();

/** `competition.attempt_started`. */
const char *attemptStartedType();

/** `competition.goal_installed`, the goal having been saved at @p installedAtMs. */
nlohmann::json goalInstalledMessage(const AttemptIdentity &identity, int64_t installedAtMs);

/** `competition.attempt_started`. */
nlohmann::json attemptStartedMessage(const AttemptIdentity &identity, const AttemptStart &start);

/** What the server did with a message it accepted. */
enum class ReceiptStatus { Saved, Duplicate };

/**
 * Parses the 200 reply to a message of @p sentType about @p identity: its receipt, of type
 * `<sentType>_saved`. nullopt for anything else, including a receipt for another attempt.
 */
std::optional<ReceiptStatus> parseReceipt(const nlohmann::json &reply, const std::string &sentType,
                                          const AttemptIdentity &identity);

/** @p ms since the epoch as ISO 8601 UTC with milliseconds, e.g. `2026-11-06T20:00:00.250Z`. */
std::string formatUtc(int64_t ms);

} // namespace competitions
} // namespace detail
} // namespace scorbit
