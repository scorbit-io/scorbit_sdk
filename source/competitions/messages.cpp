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
#include "../achievements/json_codec.h"
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
constexpr auto KEY_INSTALLED_AT = "installed_at";
constexpr auto KEY_SESSION = "session";
constexpr auto KEY_STARTED_AT = "started_at";
constexpr auto KEY_BOOT_ID = "boot_id";
constexpr auto KEY_SEQUENCE = "sequence";
constexpr auto KEY_STATUS = "status";

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

/** Throws unless every part of @p definition is one this SDK judges, and every rule parses. */
void checkDefinition(const json &definition)
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
    if (!achievements::parseDefinition(asDefinition, error)) {
        throw std::invalid_argument(fmt::format("goal doesn't parse: {}", error));
    }
}

json identityJson(const AttemptIdentity &identity, const char *type)
{
    return {{KEY_VERSION, MESSAGE_VERSION},  {KEY_TOURNAMENT, identity.tournament},
            {KEY_RULES, identity.rules},     {KEY_CHECKSUM, identity.checksum},
            {KEY_ATTEMPT, identity.attempt}, {KEY_TYPE, type}};
}

} // namespace

std::optional<CompetitionGoal> parseGoal(const json &payload, std::string &error)
{
    try {
        if (!payload.is_object()) {
            throw std::invalid_argument("not an object");
        }
        if (integer(payload, KEY_VERSION) != MESSAGE_VERSION) {
            throw std::invalid_argument(
                    fmt::format("unsupported version {}", member(payload, KEY_VERSION).dump()));
        }
        if (member(payload, KEY_TYPE) != TYPE_GOAL) {
            throw std::invalid_argument(
                    fmt::format("not a {}: {}", TYPE_GOAL, member(payload, KEY_TYPE).dump()));
        }

        CompetitionGoal goal;
        goal.identity = {uuid(payload, KEY_TOURNAMENT), uuid(payload, KEY_RULES), checksum(payload),
                         uuid(payload, KEY_ATTEMPT)};
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
        checkDefinition(goal.definition);
        goal.message = payload;
        return goal;
    } catch (const std::exception &e) {
        error = e.what();
        return std::nullopt;
    }
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

std::optional<ReceiptStatus> parseReceipt(const json &reply, const std::string &sentType,
                                          const AttemptIdentity &identity)
{
    if (!reply.is_object() || reply.value(KEY_TYPE, json {}) != sentType + RECEIPT_SUFFIX
        || reply.value(KEY_VERSION, json {}) != MESSAGE_VERSION
        || reply.value(KEY_ATTEMPT, json {}) != identity.attempt
        || reply.value(KEY_CHECKSUM, json {}) != identity.checksum) {
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

const char *goalInstalledType()
{
    return TYPE_GOAL_INSTALLED;
}

const char *attemptStartedType()
{
    return TYPE_ATTEMPT_STARTED;
}

} // namespace competitions
} // namespace detail
} // namespace scorbit
