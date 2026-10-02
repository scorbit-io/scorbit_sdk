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
#include <string_view>

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

std::optional<DefinitionSet> parseDefinitionsResponse(const json &document)
{
    if (!document.is_array()) {
        ERR("Achievements: definitions response is not an array but {}: {}", document.type_name(),
            document.dump());
        return std::nullopt;
    }

    std::vector<Definition> definitions;
    definitions.reserve(document.size());
    for (size_t i = 0; i < document.size(); ++i) {
        std::string error;
        auto definition = parseDefinition(document[i], error);
        if (!definition) {
            WRN("Achievements: skipping malformed definition #{}: {}", i, error);
            continue;
        }
        if (auto problem = findEvaluationProblem(*definition)) {
            WRN("Achievements: skipping '{}', it can't be evaluated: {}", definition->key,
                *problem);
            continue;
        }
        definitions.push_back(std::move(*definition));
    }

    for (const auto &key : removeCyclicChains(definitions)) {
        WRN("Achievements: skipping '{}', its ACHIEVEMENT rules form a cycle", key);
    }

    return DefinitionSet {std::move(definitions)};
}

std::optional<Baselines> parseProgressResponse(const json &document)
{
    if (!document.is_array()) {
        ERR("Achievements: progress response is not an array but {}: {}", document.type_name(),
            document.dump());
        return std::nullopt;
    }

    Baselines baselines;
    for (const auto &entry : document) {
        try {
            const auto achievement = entry.find(KEY_ACHIEVEMENT);
            if (achievement == entry.end() || !achievement->is_object()) {
                throw std::invalid_argument("missing 'achievement' object");
            }
            auto key = requiredField<std::string>(*achievement, KEY_KEY);

            // `current_value` is not read: /report/ stores per-rule values elsewhere, so it only
            // holds what the legacy /unlock/ `count` once wrote
            Baseline baseline;
            baseline.achieved = fieldOr<bool>(entry, KEY_ACHIEVED, false);
            baselines.insert_or_assign(std::move(key), std::move(baseline));
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
