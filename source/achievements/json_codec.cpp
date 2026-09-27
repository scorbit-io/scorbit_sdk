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

namespace scorbit {
namespace detail {
namespace achievements {

using json = nlohmann::json;

namespace {

// Achievement (§10.2)
constexpr auto KEY_KEY = "key";
constexpr auto KEY_NAME = "name";
constexpr auto KEY_DESCRIPTION = "description";
constexpr auto KEY_SCOPE = "scope";
constexpr auto KEY_EVALUATION = "evaluation";
constexpr auto KEY_IS_TROPHY = "is_trophy";
constexpr auto KEY_VISIBLE = "visible";
constexpr auto KEY_OBSCURE = "obscure";
constexpr auto KEY_ICON_URL = "icon_url";
constexpr auto KEY_OBSCURE_IMAGE_URL = "obscure_image_url";
constexpr auto KEY_GROUP_ID = "group_id";
constexpr auto KEY_LEVEL = "level";
constexpr auto KEY_DISPLAY_POSITION = "display_position";
constexpr auto KEY_NOTIFY_WHEN_ACHIEVED = "notify_when_achieved";
constexpr auto KEY_RULES = "rules";

// Rule
constexpr auto KEY_TYPE = "type";
constexpr auto KEY_COMPARISON = "comparison";
constexpr auto KEY_TARGET = "target";
constexpr auto KEY_REFERENCE = "reference";

// Definitions / progress responses (§10.3, §10.4)
constexpr auto KEY_GAME = "game";
constexpr auto KEY_FRAMES_VERSION = "frames_version";
constexpr auto KEY_RESULTS = "results";

// UserAchievement
constexpr auto KEY_ACHIEVED = "achieved";
constexpr auto KEY_RULE_PROGRESS = "rule_progress";
constexpr auto KEY_INDEX = "index";
constexpr auto KEY_VALUE = "value";
constexpr auto KEY_SATISFIED = "satisfied";

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

/** Splits a MODE_STACK reference on commas; names are compared byte-for-byte, so no trimming. */
std::vector<std::string> splitStack(const std::string &reference)
{
    std::vector<std::string> modes;
    size_t start = 0;
    while (start <= reference.size()) {
        const auto comma = reference.find(',', start);
        const auto end = comma == std::string::npos ? reference.size() : comma;
        std::string mode = reference.substr(start, end - start);
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

        const auto evaluationStr = requiredField<std::string>(object, KEY_EVALUATION);
        const auto evaluation = evaluationClassFromString(evaluationStr);
        if (!evaluation) {
            throw std::invalid_argument(fmt::format("unknown evaluation '{}'", evaluationStr));
        }
        definition.evaluation = *evaluation;

        definition.isTrophy = fieldOr<bool>(object, KEY_IS_TROPHY, false);
        definition.visible = fieldOr<bool>(object, KEY_VISIBLE, true);
        definition.obscure = fieldOr<bool>(object, KEY_OBSCURE, false);
        definition.notifyWhenAchieved = fieldOr<bool>(object, KEY_NOTIFY_WHEN_ACHIEVED, false);
        definition.iconUrl = fieldOr<std::string>(object, KEY_ICON_URL, {});
        definition.obscureImageUrl = fieldOr<std::string>(object, KEY_OBSCURE_IMAGE_URL, {});
        definition.groupId = field<int64_t>(object, KEY_GROUP_ID);
        definition.level = field<int64_t>(object, KEY_LEVEL);
        definition.displayPosition = field<int64_t>(object, KEY_DISPLAY_POSITION);

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
    if (!document.is_object()) {
        ERR("Achievements: definitions response is not an object");
        return std::nullopt;
    }

    const auto results = document.find(KEY_RESULTS);
    if (results == document.end() || !results->is_array()) {
        ERR("Achievements: definitions response has no 'results' array");
        return std::nullopt;
    }

    std::string game;
    int64_t framesVersion = 0;
    try {
        game = fieldOr<std::string>(document, KEY_GAME, {});
        framesVersion = fieldOr<int64_t>(document, KEY_FRAMES_VERSION, 0);
    } catch (const std::exception &e) {
        WRN("Achievements: definitions response header is malformed: {}", e.what());
    }

    std::vector<Definition> definitions;
    definitions.reserve(results->size());
    for (size_t i = 0; i < results->size(); ++i) {
        std::string error;
        auto definition = parseDefinition((*results)[i], error);
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

    return DefinitionSet {std::move(game), framesVersion, std::move(definitions)};
}

RuleProgressMap parseRuleProgress(const json &array)
{
    RuleProgressMap rules;
    if (!array.is_array()) {
        return rules;
    }

    for (const auto &entry : array) {
        try {
            const auto index = requiredField<size_t>(entry, KEY_INDEX);
            const auto value = field<int64_t>(entry, KEY_VALUE);
            if (!value) {
                continue; // Never reported (§10.2): no measurement
            }
            rules[index] = RuleProgress {*value, fieldOr<bool>(entry, KEY_SATISFIED, false)};
        } catch (const std::exception &e) {
            WRN("Achievements: skipping malformed rule_progress entry: {}", e.what());
        }
    }
    return rules;
}

std::optional<Baselines> parseProgressResponse(const json &document)
{
    if (!document.is_object()) {
        ERR("Achievements: progress response is not an object");
        return std::nullopt;
    }

    const auto results = document.find(KEY_RESULTS);
    if (results == document.end() || !results->is_array()) {
        ERR("Achievements: progress response has no 'results' array");
        return std::nullopt;
    }

    Baselines baselines;
    for (const auto &entry : *results) {
        try {
            auto key = requiredField<std::string>(entry, KEY_KEY);
            Baseline baseline;
            baseline.achieved = fieldOr<bool>(entry, KEY_ACHIEVED, false);
            if (const auto it = entry.find(KEY_RULE_PROGRESS); it != entry.end()) {
                baseline.rules = parseRuleProgress(*it);
            }
            baselines.insert_or_assign(std::move(key), std::move(baseline));
        } catch (const std::exception &e) {
            WRN("Achievements: skipping malformed progress entry: {}", e.what());
        }
    }
    return baselines;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
