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

#include "messages.h"
#include "../achievements/definition.h"
#include "../achievements/definition_check.h"
#include "../achievements/json_codec.h"
#include "../identifiers.h"
#include "../net_util.h"
#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace scorbit {
namespace detail {
namespace competitions {

using json = nlohmann::json;
using achievements::RuleType;

namespace {

constexpr int MESSAGE_VERSION = 1;
constexpr int DEFINITION_FORMAT_VERSION = 1;
constexpr int EVALUATOR_VERSION = 1;
constexpr auto TIMER_POLICY = "current_player_turn";
constexpr int TIMER_POLICY_VERSION = 1;

constexpr auto TYPE_GOAL = "competition.goal";
constexpr auto TYPE_GOAL_INSTALLED = "competition.goal_installed";
constexpr auto TYPE_ATTEMPT_STARTED = "competition.attempt_started";
constexpr auto TYPE_ATTEMPT_CONTROL = "competition.attempt_control";
constexpr auto TYPE_ATTEMPT_RELEASED = "competition.attempt_released";
constexpr auto TYPE_ATTEMPT_INTERRUPTED = "competition.attempt_interrupted";
constexpr auto TYPE_QUALIFICATION = "competition.qualification";
constexpr auto RECEIPT_SUFFIX = "_saved";

constexpr auto KEY_VERSION = "version";
constexpr auto KEY_TYPE = "type";
constexpr auto KEY_TOURNAMENT = "tournament";
constexpr auto KEY_RULES = "rules";
constexpr auto KEY_CHECKSUM = "checksum";
constexpr auto KEY_ATTEMPT = "attempt";
constexpr auto KEY_PLAYER = "player";
constexpr auto KEY_VENUEMACHINE = "venuemachine";
constexpr auto KEY_POSITION = "position";
constexpr auto KEY_DEFINITION = "definition";
constexpr auto KEY_FORMAT_VERSION = "format_version";
constexpr auto KEY_EVALUATOR_VERSION = "evaluator_version";
constexpr auto KEY_TIMER_POLICY = "timer_policy";
constexpr auto KEY_NAME = "name";
constexpr auto KEY_ACHIEVEMENT = "achievement";
/** The goal's calendar envelope, and the parts the feed's names. */
constexpr auto KEY_CALENDAR = "calendar";
constexpr auto KEY_TIMEZONE = "timezone";
constexpr auto KEY_UTC_OFFSETS = "utc_offsets";
constexpr auto KEY_RANGES = "ranges";
constexpr auto KEY_INSTALLED_AT = "installed_at";
constexpr auto KEY_SESSION = "session";
constexpr auto KEY_STARTED_AT = "started_at";
constexpr auto KEY_BOOT_ID = "boot_id";
constexpr auto KEY_SEQUENCE = "sequence";
constexpr auto KEY_LAST_SEQUENCE = "last_sequence";
constexpr auto KEY_STATUS = "status";
constexpr auto KEY_ACTION = "action";
constexpr auto KEY_CONTROL = "control";
constexpr auto KEY_STATE = "state";
constexpr auto KEY_QUALIFICATION = "qualification";
constexpr auto KEY_REPORT_ID = "report_id";
constexpr auto KEY_QUALIFIED_AT = "qualified_at";
constexpr auto KEY_DURATION = "duration";
constexpr auto KEY_VALUE = "value";
constexpr auto KEY_UNIT = "unit";
constexpr auto KEY_DEVICE = "device";
constexpr auto KEY_SCORBITRON = "scorbitron";
constexpr auto KEY_REASON = "reason";
constexpr auto KEY_CASE = "case";

constexpr auto ACTION_RELEASE = "release";

/** Positions a game slot may have, as the API bounds them. */
constexpr int MIN_POSITION = 1;
constexpr int MAX_POSITION = 6;

/**
 * The rule types a goal may use: those judged within one game, as the API's
 * COMPETITION_RULE_TYPES lists them. ACHIEVEMENT and SESSION are left out.
 */
bool isCompetitionRuleType(RuleType type)
{
    switch (type) {
    case RuleType::Mode:
    case RuleType::ModeCompleted:
    case RuleType::ModeStack:
    case RuleType::ModeScore:
    case RuleType::Score:
    case RuleType::Event:
    case RuleType::TimerSession:
    case RuleType::TimerBall:
    case RuleType::TimerMode:
    case RuleType::TimerBetween:
    case RuleType::Ball:
    case RuleType::Calendar:
        return true;
    case RuleType::Session:
    case RuleType::Achievement:
        break;
    }
    return false;
}

const json &member(const json &object, const char *key)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        throw std::invalid_argument(fmt::format("missing '{}'", key));
    }
    return *it;
}

int64_t integer(const json &object, const char *key)
{
    const auto &value = member(object, key);
    if (!value.is_number_integer()) {
        throw std::invalid_argument(fmt::format("'{}' is not an integer", key));
    }
    return value.get<int64_t>();
}

std::string uuid(const json &object, const char *key)
{
    const auto &value = member(object, key);
    if (!value.is_string() || !isUuid(value.get<std::string>())) {
        throw std::invalid_argument(fmt::format("'{}' is not a uuid", key));
    }
    return toLowerAscii(value.get<std::string>());
}

std::string checksum(const json &object)
{
    const auto &value = member(object, KEY_CHECKSUM);
    if (!value.is_string()) {
        throw std::invalid_argument("'checksum' is not a string");
    }
    const auto text = value.get<std::string>();
    constexpr size_t SHA256_HEX = 64;
    if (text.size() != SHA256_HEX || !std::ranges::all_of(text, [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) {
        throw std::invalid_argument("'checksum' is not a lowercase SHA-256");
    }
    return text;
}

/**
 * Throws unless every part of @p definition is one this SDK judges, and every rule parses; puts
 * the parsed rules and calendar in @p goal.
 */
void checkDefinition(const json &definition, CompetitionGoal &goal)
{
    if (!definition.is_object()) {
        throw std::invalid_argument("'definition' is not an object");
    }
    if (integer(definition, KEY_FORMAT_VERSION) != DEFINITION_FORMAT_VERSION) {
        throw std::invalid_argument(fmt::format("unsupported definition format {}",
                                                member(definition, KEY_FORMAT_VERSION).dump()));
    }
    // Bumped by the API whenever how a saved rule is judged changes (ADR-0051, D3)
    if (integer(definition, KEY_EVALUATOR_VERSION) != EVALUATOR_VERSION) {
        throw std::invalid_argument(fmt::format("unsupported evaluator version {}",
                                                member(definition, KEY_EVALUATOR_VERSION).dump()));
    }

    const auto &timer = member(definition, KEY_TIMER_POLICY);
    if (!timer.is_object() || timer.value(KEY_NAME, json {}) != TIMER_POLICY
        || timer.value(KEY_VERSION, json {}) != TIMER_POLICY_VERSION) {
        throw std::invalid_argument(fmt::format("unsupported timer policy {}", timer.dump()));
    }

    const auto &rules = member(definition, KEY_RULES);
    if (!rules.is_array() || rules.empty()) {
        throw std::invalid_argument("'rules' is not a non-empty array");
    }
    for (const auto &rule : rules) {
        const auto type = rule.is_object() && rule.value(KEY_TYPE, json {}).is_string()
                                ? achievements::ruleTypeFromString(rule[KEY_TYPE].get<std::string>())
                                : std::nullopt;
        if (!type || !isCompetitionRuleType(*type)) {
            throw std::invalid_argument(
                    fmt::format("unsupported rule type {}",
                                rule.is_object() ? rule.value(KEY_TYPE, json {}).dump() : "?"));
        }
    }

    // The rules parsed as an achievement's are: one that doesn't is refused, never left out
    const auto &achievement = member(definition, KEY_ACHIEVEMENT);
    if (!achievement.is_object()) {
        throw std::invalid_argument("'achievement' is not an object");
    }
    auto asDefinition = achievement;
    asDefinition[KEY_RULES] = rules;
    std::string error;
    const auto parsed = achievements::parseDefinition(asDefinition, error);
    if (!parsed) {
        throw std::invalid_argument(fmt::format("goal doesn't parse: {}", error));
    }

    // And judged as the feed's are: a goal the evaluator can't judge (a rule without the
    // reference it needs, a CALENDAR condition without the venue's offsets) is refused, not
    // acknowledged and then never met. The goal carries its own calendar envelope.
    achievements::CalendarData calendar;
    if (const auto it = definition.find(KEY_CALENDAR); it != definition.end() && it->is_object()) {
        json envelope = json::object();
        for (const auto *key : {KEY_TIMEZONE, KEY_UTC_OFFSETS}) {
            if (const auto part = it->find(key); part != it->end()) {
                envelope[key] = *part;
            }
        }
        if (const auto ranges = it->find(KEY_RANGES); ranges != it->end()) {
            envelope[KEY_CALENDAR] = *ranges;
        }
        calendar = achievements::parseCalendarEnvelope(envelope);
    }
    if (const auto problem = achievements::findEvaluationProblem(*parsed, calendar)) {
        throw std::invalid_argument(fmt::format("goal can't be evaluated: {}", *problem));
    }

    // Judged within one game only: a lifetime goal would carry progress from earlier games
    if (parsed->evaluation != achievements::EvaluationClass::InSession) {
        throw std::invalid_argument("goal isn't single session");
    }
    if (const auto problem = findInstantProblem(*parsed)) {
        throw std::invalid_argument(
                fmt::format("goal can't be judged at one instant: {}", *problem));
    }

    goal.goal = std::move(*parsed);
    goal.calendar = std::move(calendar);
}

json identityJson(const AttemptIdentity &identity, const char *type)
{
    return {{KEY_VERSION, MESSAGE_VERSION},  {KEY_TOURNAMENT, identity.tournament},
            {KEY_RULES, identity.rules},     {KEY_CHECKSUM, identity.checksum},
            {KEY_ATTEMPT, identity.attempt}, {KEY_TYPE, type}};
}

} // namespace

/** The identity of @p payload, an API message of @p type in this version; throws if it isn't. */
AttemptIdentity identityOf(const json &payload, const char *type)
{
    if (!payload.is_object()) {
        throw std::invalid_argument("not an object");
    }
    if (integer(payload, KEY_VERSION) != MESSAGE_VERSION) {
        throw std::invalid_argument(
                fmt::format("unsupported version {}", member(payload, KEY_VERSION).dump()));
    }
    if (member(payload, KEY_TYPE) != type) {
        throw std::invalid_argument(
                fmt::format("not a {}: {}", type, member(payload, KEY_TYPE).dump()));
    }
    return {uuid(payload, KEY_TOURNAMENT), uuid(payload, KEY_RULES), checksum(payload),
            uuid(payload, KEY_ATTEMPT)};
}

const char *toString(ReleaseState state)
{
    switch (state) {
    case ReleaseState::Released:
        return "released";
    case ReleaseState::Started:
        return "started";
    case ReleaseState::Unknown:
        break;
    }
    return "unknown";
}

std::optional<CompetitionGoal> parseGoal(const json &payload, std::string &error)
{
    try {
        CompetitionGoal goal;
        goal.identity = identityOf(payload, TYPE_GOAL);
        goal.player = uuid(payload, KEY_PLAYER);
        goal.venueMachine = uuid(payload, KEY_VENUEMACHINE);

        if (const auto &position = member(payload, KEY_POSITION); !position.is_null()) {
            const auto value = integer(payload, KEY_POSITION);
            if (value < MIN_POSITION || value > MAX_POSITION) {
                throw std::invalid_argument(fmt::format("position {} out of range", value));
            }
            goal.position = static_cast<int>(value);
        }

        goal.definition = member(payload, KEY_DEFINITION);
        checkDefinition(goal.definition, goal);
        // Only shown to the host: a goal without one is still judged
        const auto &achievement = member(goal.definition, KEY_ACHIEVEMENT);
        if (const auto it = achievement.find(KEY_NAME);
            it != achievement.end() && it->is_string()) {
            goal.name = it->get<std::string>();
        }
        goal.message = payload;
        return goal;
    } catch (const std::exception &e) {
        error = e.what();
        return std::nullopt;
    }
}

std::optional<AttemptRelease> parseAttemptControl(const json &payload, std::string &error)
{
    try {
        AttemptRelease release;
        release.identity = identityOf(payload, TYPE_ATTEMPT_CONTROL);
        if (const auto &action = member(payload, KEY_ACTION); action != ACTION_RELEASE) {
            throw std::invalid_argument(fmt::format("unsupported action {}", action.dump()));
        }
        release.control = uuid(payload, KEY_CONTROL);
        return release;
    } catch (const std::exception &e) {
        error = e.what();
        return std::nullopt;
    }
}

std::optional<std::string> findInstantProblem(const achievements::Definition &goal)
{
    const auto siblings = std::ranges::count_if(goal.rules, [](const achievements::Rule &rule) {
        return achievements::isInstantValued(rule.type);
    });
    const bool calendar = std::ranges::any_of(goal.rules, [](const achievements::Rule &rule) {
        return rule.type == RuleType::Calendar;
    });

    for (size_t i = 0; i < goal.rules.size(); ++i) {
        const auto &rule = goal.rules[i];
        // TIMER_BETWEEN over several siblings is the shortest span covering them on a ball, which
        // a later occurrence on the same ball shortens; so is a single sibling's gap that an
        // occurrence entering a CALENDAR window later splits. Only `<=` survives that.
        if (rule.type == RuleType::TimerBetween && rule.comparison != achievements::Comparison::Le
            && (siblings > 1 || calendar)) {
            return fmt::format("rule {} (TIMER_BETWEEN {}): a later occurrence would shorten the "
                               "span that met it",
                               i, achievements::toString(rule.comparison));
        }
        if (rule.comparison == achievements::Comparison::Ge) {
            continue;
        }
        switch (rule.type) {
        case RuleType::Score:
            return fmt::format("rule {} (SCORE {}): an earlier score would meet it", i,
                               achievements::toString(rule.comparison));
        case RuleType::TimerBall:
        case RuleType::TimerMode:
            return fmt::format("rule {} ({} {}): one still running would meet it", i,
                               achievements::toString(rule.type),
                               achievements::toString(rule.comparison));
        default:
            break;
        }
    }
    return std::nullopt;
}

json goalInstalledMessage(const AttemptIdentity &identity, int64_t installedAtMs)
{
    auto message = identityJson(identity, TYPE_GOAL_INSTALLED);
    message[KEY_INSTALLED_AT] = formatUtc(installedAtMs);
    return message;
}

json attemptStartedMessage(const AttemptIdentity &identity, const AttemptStart &start)
{
    auto message = identityJson(identity, TYPE_ATTEMPT_STARTED);
    message[KEY_SESSION] = start.session;
    message[KEY_POSITION] = start.position;
    message[KEY_STARTED_AT] = formatUtc(start.startedAtMs);
    message[KEY_BOOT_ID] = start.bootId;
    message[KEY_SEQUENCE] = start.sequence;
    return message;
}

json attemptReleasedMessage(const AttemptRelease &release, const ReleaseApplied &applied,
                            const std::string &qualification)
{
    auto message = identityJson(release.identity, TYPE_ATTEMPT_RELEASED);
    message[KEY_CONTROL] = release.control;
    message[KEY_STATE] = toString(applied.state);
    message[KEY_BOOT_ID] = applied.bootId;
    message[KEY_SEQUENCE] = applied.sequence;
    message[KEY_QUALIFICATION] = qualification.empty() ? json(nullptr) : json(qualification);
    return message;
}

json attemptInterruptedMessage(const AttemptIdentity &identity, const AttemptStart &start,
                               uint64_t lastSequence, const std::string &qualification)
{
    auto message = identityJson(identity, TYPE_ATTEMPT_INTERRUPTED);
    message[KEY_SESSION] = start.session;
    message[KEY_POSITION] = start.position;
    message[KEY_BOOT_ID] = start.bootId;
    message[KEY_LAST_SEQUENCE] = lastSequence;
    message[KEY_QUALIFICATION] = qualification.empty() ? json(nullptr) : json(qualification);
    return message;
}

json qualificationMessage(const QualificationReport &report)
{
    const auto &result = report.result;
    auto message = identityJson(report.identity, TYPE_QUALIFICATION);
    message[KEY_REPORT_ID] = report.reportId;
    message[KEY_PLAYER] = report.player;
    message[KEY_SESSION] = report.start.session;
    message[KEY_POSITION] = report.start.position;
    message[KEY_STARTED_AT] = formatUtc(report.start.startedAtMs);
    message[KEY_QUALIFIED_AT] = formatUtc(result.completionUtcMs);
    message[KEY_SEQUENCE] = result.completionSequence;
    message[KEY_DURATION] = {{KEY_VALUE, result.durationMs}, {KEY_UNIT, result.evidence.unit}};
    message[KEY_TIMER_POLICY] = {{KEY_NAME, TIMER_POLICY},
                                 {KEY_VERSION, result.evidence.policyVersion}};
    message[KEY_DEVICE] = {{KEY_SCORBITRON, result.evidence.deviceId},
                           {KEY_BOOT_ID, result.evidence.bootId}};
    return message;
}

std::optional<ReceiptStatus> parseReceipt(const json &reply, const std::string &sentType,
                                          const AttemptIdentity &identity,
                                          const std::string &control)
{
    if (!reply.is_object() || reply.value(KEY_TYPE, json {}) != sentType + RECEIPT_SUFFIX
        || reply.value(KEY_VERSION, json {}) != MESSAGE_VERSION
        || reply.value(KEY_ATTEMPT, json {}) != identity.attempt
        || reply.value(KEY_CHECKSUM, json {}) != identity.checksum
        || (!control.empty() && reply.value(KEY_CONTROL, json {}) != control)) {
        return std::nullopt;
    }
    const auto status = reply.value(KEY_STATUS, json {});
    if (status == "saved") {
        return ReceiptStatus::Saved;
    }
    if (status == "duplicate") {
        return ReceiptStatus::Duplicate;
    }
    return std::nullopt;
}

std::optional<QualificationReceipt> parseQualificationReceipt(const json &reply,
                                                              const AttemptIdentity &identity,
                                                              const std::string &reportId)
{
    if (!reply.is_object()
        || reply.value(KEY_TYPE, json {}) != std::string {TYPE_QUALIFICATION} + RECEIPT_SUFFIX
        || reply.value(KEY_VERSION, json {}) != MESSAGE_VERSION
        || reply.value(KEY_ATTEMPT, json {}) != identity.attempt
        || reply.value(KEY_CHECKSUM, json {}) != identity.checksum
        || reply.value(KEY_REPORT_ID, json {}) != reportId) {
        return std::nullopt;
    }
    QualificationReceipt receipt;
    const auto status = reply.value(KEY_STATUS, json {});
    if (status == "saved") {
        receipt.status = QualificationStatus::Saved;
    } else if (status == "duplicate") {
        receipt.status = QualificationStatus::Duplicate;
    } else if (status == "conflict") {
        receipt.status = QualificationStatus::Conflict;
    } else {
        return std::nullopt;
    }
    if (const auto reason = reply.value(KEY_REASON, json {}); reason.is_string()) {
        receipt.reason = reason.get<std::string>();
    }
    if (const auto caseId = reply.value(KEY_CASE, json {}); caseId.is_string()) {
        receipt.caseId = caseId.get<std::string>();
    }
    return receipt;
}

std::string formatUtc(int64_t ms)
{
    using namespace std::chrono;
    const sys_time<milliseconds> time {milliseconds {ms}};
    const auto day = floor<days>(time);
    const year_month_day date {day};
    const hh_mm_ss clock {time - day};
    return fmt::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                       clock.hours().count(), clock.minutes().count(), clock.seconds().count(),
                       clock.subseconds().count());
}

const MessageKindInfo &kindInfo(MessageKind kind)
{
    // The server takes a start once the release waiting on it is answered (SB-6368 D3), and a
    // report once it has the attempt's start
    static const MessageKindInfo goalInstalled {TYPE_GOAL_INSTALLED,
                                                URL_COMPETITIONS_GOAL_INSTALLED, nullptr};
    static const MessageKindInfo attemptStarted {
            TYPE_ATTEMPT_STARTED, URL_COMPETITIONS_ATTEMPT_STARTED, "release_pending"};
    static const MessageKindInfo attemptReleased {TYPE_ATTEMPT_RELEASED,
                                                  URL_COMPETITIONS_ATTEMPT_RELEASED, nullptr};
    static const MessageKindInfo attemptInterrupted {
            TYPE_ATTEMPT_INTERRUPTED, URL_COMPETITIONS_ATTEMPT_INTERRUPTED, "start_pending"};
    static const MessageKindInfo qualification {TYPE_QUALIFICATION, URL_COMPETITIONS_QUALIFICATION,
                                                "start_pending"};
    switch (kind) {
    case MessageKind::GoalInstalled:
        return goalInstalled;
    case MessageKind::AttemptStarted:
        return attemptStarted;
    case MessageKind::AttemptInterrupted:
        return attemptInterrupted;
    case MessageKind::Qualification:
        return qualification;
    case MessageKind::AttemptReleased:
        break;
    }
    return attemptReleased;
}

const char *goalInstalledType()
{
    return TYPE_GOAL_INSTALLED;
}

const char *attemptStartedType()
{
    return TYPE_ATTEMPT_STARTED;
}

const char *attemptReleasedType()
{
    return TYPE_ATTEMPT_RELEASED;
}

const char *qualificationType()
{
    return TYPE_QUALIFICATION;
}

} // namespace competitions
} // namespace detail
} // namespace scorbit
