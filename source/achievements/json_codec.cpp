/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "json_codec.h"
#include "definition_check.h"
#include <fmt/format.h>
#include <logger/logger.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string_view>
#include <utility>

namespace scorbit {
namespace detail {
namespace achievements {

using json = nlohmann::json;

namespace {

// Achievement (API ScorbitronAchievementSerializer)
constexpr auto KEY_KEY = "key";
constexpr auto KEY_NAME = "name";
constexpr auto KEY_DESCRIPTION = "description";
constexpr auto KEY_SCOPE = "scope";
constexpr auto KEY_IS_SINGLE_SESSION = "is_single_session";
constexpr auto KEY_IS_TROPHY = "is_trophy";
constexpr auto KEY_IS_BADGE = "is_badge";
constexpr auto KEY_VISIBLE = "visible";
constexpr auto KEY_OBSCURE = "obscure";
constexpr auto KEY_ICON = "icon";
constexpr auto KEY_OBSCURE_IMAGE = "obscure_image";
constexpr auto KEY_GROUP_ID = "group_id";
constexpr auto KEY_LEVEL = "level";
constexpr auto KEY_DISPLAY_POSITION = "display_position";
constexpr auto KEY_FRAME = "frame";
constexpr auto KEY_FRAME_VERSION = "frame_version";
constexpr auto KEY_NOTIFY_WHEN_ACHIEVED = "notify_when_achieved";
constexpr auto KEY_RULES = "rules";

// Rule
constexpr auto KEY_TYPE = "type";
constexpr auto KEY_COMPARISON = "comparison";
constexpr auto KEY_TARGET = "target";
constexpr auto KEY_REFERENCE = "reference";

// Progress entry (API UserAchievementV2Serializer)
constexpr auto KEY_ACHIEVEMENT = "achievement";
constexpr auto KEY_ACHIEVED = "achieved";

// Report request / response (§10.5)
constexpr auto KEY_RESULTS = "results";
constexpr auto KEY_RULE_PROGRESS = "rule_progress";
constexpr auto KEY_INDEX = "index";
constexpr auto KEY_VALUE = "value";
constexpr auto KEY_SATISFIED = "satisfied";
constexpr auto KEY_USER_ID = "user_id";
constexpr auto KEY_SESSION_UUID = "session_uuid";
constexpr auto KEY_SEQUENCE = "sequence";
constexpr auto KEY_ACHIEVEMENTS = "achievements";
constexpr auto KEY_STATUS = "status";

// Definitions response (§10.3)
constexpr auto KEY_TIMEZONE = "timezone";
constexpr auto KEY_UTC_OFFSETS = "utc_offsets";
constexpr auto KEY_CALENDAR = "calendar";
constexpr auto KEY_CODE = "code";
constexpr auto KEY_DETAIL = "detail";

std::optional<ReportStatus> reportStatusFromString(std::string_view str)
{
    if (str == "unlocked") {
        return ReportStatus::Unlocked;
    }
    if (str == "in_progress") {
        return ReportStatus::InProgress;
    }
    if (str == "already_held") {
        return ReportStatus::AlreadyHeld;
    }
    if (str == "rejected") {
        return ReportStatus::Rejected;
    }
    return std::nullopt;
}

/** The field's value, or std::nullopt when absent or null. Throws json::type_error on mismatch. */
template<typename T>
std::optional<T> field(const json &object, const char *key)
{
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    return it->get<T>();
}

template<typename T>
T fieldOr(const json &object, const char *key, T fallback)
{
    return field<T>(object, key).value_or(std::move(fallback));
}

/** The field's value; throws std::invalid_argument when absent or null. */
template<typename T>
T requiredField(const json &object, const char *key)
{
    auto value = field<T>(object, key);
    if (!value) {
        throw std::invalid_argument(fmt::format("missing '{}'", key));
    }
    return std::move(*value);
}

std::string trimmed(std::string_view str)
{
    constexpr std::string_view whitespace {" \t\r\n\f\v"};
    const auto first = str.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = str.find_last_not_of(whitespace);
    return std::string {str.substr(first, last - first + 1)};
}

/**
 * Splits a MODE_STACK reference on commas. Each name is trimmed, as the API does when it validates
 * the list; otherwise names are compared byte-for-byte.
 */
std::vector<std::string> splitStack(const std::string &reference)
{
    std::vector<std::string> modes;
    size_t start = 0;
    while (start <= reference.size()) {
        const auto comma = reference.find(',', start);
        const auto end = comma == std::string::npos ? reference.size() : comma;
        auto mode = trimmed(std::string_view {reference}.substr(start, end - start));
        if (std::ranges::find(modes, mode) == modes.end()) {
            modes.push_back(std::move(mode));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return modes;
}

/** `[a, b]`: two integers. */
std::optional<std::pair<int64_t, int64_t>> integerPair(const json &value)
{
    if (!value.is_array() || value.size() != 2 || !value[0].is_number_integer()
        || !value[1].is_number_integer()) {
        return std::nullopt;
    }
    return std::pair {value[0].get<int64_t>(), value[1].get<int64_t>()};
}

/** `utc_offsets`: `[from_ms, offset_seconds]` changes, ascending. */
std::optional<UtcOffsetTable> parseUtcOffsets(const json &value)
{
    if (!value.is_array()) {
        return std::nullopt;
    }
    std::vector<UtcOffsetTable::Change> changes;
    for (const auto &entry : value) {
        const auto pair = integerPair(entry);
        if (!pair) {
            return std::nullopt;
        }
        changes.push_back({pair->first, pair->second});
    }
    return UtcOffsetTable::create(std::move(changes));
}

/** `[[start_ms, end_ms], …]`: UTC ranges. */
std::optional<std::vector<TimeInterval>> parseRanges(const json &value)
{
    if (!value.is_array()) {
        return std::nullopt;
    }
    std::vector<TimeInterval> rv;
    for (const auto &entry : value) {
        const auto pair = integerPair(entry);
        if (!pair) {
            return std::nullopt;
        }
        rv.push_back({pair->first, pair->second});
    }
    return rv;
}

/** `calendar`: each `window=` / `event=` condition's ranges. */
std::map<std::string, std::vector<TimeInterval>> parseCalendarRanges(const json &value)
{
    std::map<std::string, std::vector<TimeInterval>> rv;
    if (!value.is_object()) {
        return rv;
    }
    for (const auto &[condition, ranges] : value.items()) {
        auto parsed = parseRanges(ranges);
        if (!parsed) {
            // Left out, it matches nothing, as a condition the API did not resolve
            WRN("Achievements: ignoring malformed calendar ranges of '{}': {}", condition,
                ranges.dump());
            continue;
        }
        rv.emplace(condition, std::move(*parsed));
    }
    return rv;
}

/**
 * What `CALENDAR` rules are judged with. Anything malformed is left out with a warning: without
 * offsets, achievements that need local time are skipped rather than judged in the wrong time.
 */
CalendarData parseCalendarData(const json &document)
{
    CalendarData data;
    if (const auto timezone = document.find(KEY_TIMEZONE);
        timezone != document.end() && timezone->is_string()) {
        data.timezone = timezone->get<std::string>();
    }

    if (const auto offsets = document.find(KEY_UTC_OFFSETS);
        offsets != document.end() && !offsets->is_null()) {
        data.offsets = parseUtcOffsets(*offsets);
        if (!data.offsets) {
            WRN("Achievements: ignoring malformed utc_offsets: {}", offsets->dump());
        }
    }

    if (const auto ranges = document.find(KEY_CALENDAR); ranges != document.end()) {
        data.ranges = parseCalendarRanges(*ranges);
    }
    return data;
}

Rule parseRule(const json &object)
{
    const auto typeStr = requiredField<std::string>(object, KEY_TYPE);
    const auto type = ruleTypeFromString(typeStr);
    if (!type) {
        throw std::invalid_argument(fmt::format("unknown rule type '{}'", typeStr));
    }

    const auto comparisonStr = requiredField<std::string>(object, KEY_COMPARISON);
    const auto comparison = comparisonFromString(comparisonStr);
    if (!comparison) {
        throw std::invalid_argument(fmt::format("unknown comparison '{}'", comparisonStr));
    }

    Rule rule;
    rule.type = *type;
    rule.comparison = *comparison;
    rule.target = requiredField<int64_t>(object, KEY_TARGET);
    rule.reference = fieldOr<std::string>(object, KEY_REFERENCE, {});
    if (rule.type == RuleType::ModeStack) {
        rule.stackModes = splitStack(rule.reference);
    }
    if (rule.type == RuleType::Calendar) {
        rule.calendar = parseCalendarCondition(rule.reference);
    }
    return rule;
}

} // namespace

std::optional<Definition> parseDefinition(const json &object, std::string &error)
{
    try {
        if (!object.is_object()) {
            throw std::invalid_argument("not an object");
        }

        Definition definition;
        definition.key = requiredField<std::string>(object, KEY_KEY);
        definition.name = fieldOr<std::string>(object, KEY_NAME, {});
        definition.description = fieldOr<std::string>(object, KEY_DESCRIPTION, {});

        const auto scopeStr = requiredField<std::string>(object, KEY_SCOPE);
        const auto scope = scopeFromString(scopeStr);
        if (!scope) {
            throw std::invalid_argument(fmt::format("unknown scope '{}'", scopeStr));
        }
        definition.scope = *scope;

        definition.evaluation = requiredField<bool>(object, KEY_IS_SINGLE_SESSION)
                                      ? EvaluationClass::InSession
                                      : EvaluationClass::Unlimited;

        definition.isTrophy = fieldOr<bool>(object, KEY_IS_TROPHY, false);
        definition.isBadge = fieldOr<bool>(object, KEY_IS_BADGE, false);
        definition.visible = fieldOr<bool>(object, KEY_VISIBLE, true);
        definition.obscure = fieldOr<bool>(object, KEY_OBSCURE, false);
        definition.notifyWhenAchieved = fieldOr<bool>(object, KEY_NOTIFY_WHEN_ACHIEVED, false);
        definition.iconUrl = fieldOr<std::string>(object, KEY_ICON, {});
        definition.obscureImageUrl = fieldOr<std::string>(object, KEY_OBSCURE_IMAGE, {});
        definition.groupId = field<int64_t>(object, KEY_GROUP_ID);
        definition.level = field<int64_t>(object, KEY_LEVEL);
        definition.displayPosition = field<int64_t>(object, KEY_DISPLAY_POSITION);
        definition.frameUrl = fieldOr<std::string>(object, KEY_FRAME, {});
        definition.frameVersion = fieldOr<int64_t>(object, KEY_FRAME_VERSION, 0);

        const auto rules = object.find(KEY_RULES);
        if (rules == object.end() || !rules->is_array()) {
            throw std::invalid_argument("missing 'rules' array");
        }
        for (const auto &rule : *rules) {
            definition.rules.push_back(parseRule(rule));
        }

        return definition;
    } catch (const std::exception &e) {
        error = e.what();
        return std::nullopt;
    }
}

std::optional<DefinitionFeed> parseDefinitionFeed(const json &document)
{
    const auto results = document.is_object() ? document.find(KEY_RESULTS) : document.end();
    if (!document.is_object() || results == document.end() || !results->is_array()) {
        ERR("Achievements: definitions response is not an object with 'results': {}",
            document.dump());
        return std::nullopt;
    }

    auto calendar = parseCalendarData(document);

    std::vector<Definition> definitions;
    definitions.reserve(results->size());
    for (size_t i = 0; i < results->size(); ++i) {
        std::string error;
        auto definition = parseDefinition((*results)[i], error);
        if (!definition) {
            WRN("Achievements: skipping malformed definition #{}: {}", i, error);
            continue;
        }
        if (auto problem = findEvaluationProblem(*definition, calendar)) {
            WRN("Achievements: skipping '{}', it can't be evaluated: {}", definition->key,
                *problem);
            continue;
        }
        definitions.push_back(std::move(*definition));
    }

    for (const auto &key : removeCyclicChains(definitions)) {
        WRN("Achievements: skipping '{}', its ACHIEVEMENT rules form a cycle", key);
    }

    return DefinitionFeed {DefinitionSet {std::move(definitions)}, std::move(calendar)};
}

/**
 * A stored rule value as `int64` (§10.2), or std::nullopt for `null`, which means never reported.
 * An integral float is accepted only where a double holds it exactly; anything else throws.
 */
std::optional<int64_t> storedRuleValue(const json &value)
{
    if (value.is_null()) {
        return std::nullopt;
    }
    if (value.is_number_unsigned()) {
        if (value.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            throw std::invalid_argument("rule value out of int64 range");
        }
        return static_cast<int64_t>(value.get<uint64_t>());
    }
    if (value.is_number_integer()) {
        return value.get<int64_t>();
    }
    if (value.is_number_float()) {
        constexpr double exact = 9007199254740992.0; // 2^53
        const auto number = value.get<double>();
        if (!std::isfinite(number) || std::trunc(number) != number || std::fabs(number) > exact) {
            throw std::invalid_argument(
                    fmt::format("rule value {} is not an exact integer", number));
        }
        return static_cast<int64_t>(number);
    }
    throw std::invalid_argument(fmt::format("rule value is a {}", value.type_name()));
}

/** The stored rules of one entry, by index. A rule never reported, or malformed, has no entry. */
RuleProgressMap parseStoredRules(const json &entry, const std::string &key)
{
    RuleProgressMap rules;
    const auto progress = entry.find(KEY_RULE_PROGRESS);
    if (progress == entry.end() || progress->is_null()) {
        return rules;
    }
    if (!progress->is_array()) {
        WRN("Achievements: progress for '{}' has a non-array 'rule_progress'", key);
        return rules;
    }
    for (const auto &rule : *progress) {
        try {
            const auto index = requiredField<int64_t>(rule, KEY_INDEX);
            if (index < 0) {
                throw std::invalid_argument(fmt::format("negative index {}", index));
            }
            const auto value = storedRuleValue(rule.value(KEY_VALUE, json {}));
            if (!value) {
                continue; // never reported: a later report starts from zero
            }
            const RuleProgress stored {*value, fieldOr<bool>(rule, KEY_SATISFIED, false)};
            if (!rules.emplace(static_cast<size_t>(index), stored).second) {
                WRN("Achievements: progress for '{}' repeats rule {}, keeping the first", key,
                    index);
            }
        } catch (const std::exception &e) {
            WRN("Achievements: skipping a malformed rule in the progress for '{}': {}", key,
                e.what());
        }
    }
    return rules;
}

std::optional<Baselines> parseProgressResponse(const json &document)
{
    // §10.4 wraps the entries as {"user_id", "results"}; an older API sends the bare array
    const json *entries = &document;
    if (document.is_object()) {
        const auto results = document.find(KEY_RESULTS);
        if (results == document.end() || !results->is_array()) {
            ERR("Achievements: progress response has no 'results' array: {}", document.dump());
            return std::nullopt;
        }
        entries = &*results;
    } else if (!document.is_array()) {
        ERR("Achievements: progress response is neither an object nor an array but {}: {}",
            document.type_name(), document.dump());
        return std::nullopt;
    }

    Baselines baselines;
    for (const auto &entry : *entries) {
        try {
            if (!entry.is_object()) {
                throw std::invalid_argument("entry is not an object");
            }
            // A §10.2 UserAchievement carries its own `key`; the older shape nests it
            auto key = field<std::string>(entry, KEY_KEY);
            if (!key) {
                const auto achievement = entry.find(KEY_ACHIEVEMENT);
                if (achievement == entry.end() || !achievement->is_object()) {
                    throw std::invalid_argument("missing 'key' and 'achievement' object");
                }
                key = requiredField<std::string>(*achievement, KEY_KEY);
            }

            // `current_value` (the older shape) is not read: it holds only what the legacy
            // /unlock/ `count` once wrote, not a per-rule measurement
            Baseline baseline;
            baseline.achieved = fieldOr<bool>(entry, KEY_ACHIEVED, false);
            baseline.rules = parseStoredRules(entry, *key);
            baselines.insert_or_assign(std::move(*key), std::move(baseline));
        } catch (const std::exception &e) {
            WRN("Achievements: skipping malformed progress entry: {}", e.what());
        }
    }
    return baselines;
}

std::string encodeReportRequest(const ReportRequest &request)
{
    json achievements = json::array();
    for (const auto &item : request.items) {
        json progress = json::array();
        for (const auto &[index, rule] : item.rules) {
            progress.push_back(
                    {{KEY_INDEX, index}, {KEY_VALUE, rule.value}, {KEY_SATISFIED, rule.satisfied}});
        }

        json entry {{KEY_KEY, item.key}, {KEY_RULE_PROGRESS, std::move(progress)}};
        if (item.achieved) {
            entry[KEY_ACHIEVED] = *item.achieved;
        }
        achievements.push_back(std::move(entry));
    }

    return json {{KEY_USER_ID, request.userId},
                 {KEY_SESSION_UUID, request.sessionUuid},
                 {KEY_SEQUENCE, request.sequence},
                 {KEY_ACHIEVEMENTS, std::move(achievements)}}
            .dump();
}

std::optional<std::vector<ReportOutcome>> parseReportResponse(const json &document)
{
    if (!document.is_object()) {
        ERR("Achievements: report response is not an object but {}: {}", document.type_name(),
            document.dump());
        return std::nullopt;
    }

    const auto results = document.find(KEY_RESULTS);
    if (results == document.end() || !results->is_array()) {
        ERR("Achievements: report response has no 'results' array: {}", document.dump());
        return std::nullopt;
    }

    std::vector<ReportOutcome> outcomes;
    for (const auto &entry : *results) {
        try {
            const auto statusStr = requiredField<std::string>(entry, KEY_STATUS);
            const auto status = reportStatusFromString(statusStr);
            if (!status) {
                throw std::invalid_argument(fmt::format("unknown status '{}'", statusStr));
            }
            outcomes.push_back(ReportOutcome {requiredField<std::string>(entry, KEY_KEY), *status,
                                              fieldOr<std::string>(entry, KEY_CODE, {}),
                                              fieldOr<std::string>(entry, KEY_DETAIL, {})});
        } catch (const std::exception &e) {
            WRN("Achievements: skipping malformed report result: {}", e.what());
        }
    }
    return outcomes;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
