/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */
#pragma once

#include "identifiers.h"

#include <scorbit_sdk/achievements.h>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {

// nlohmann's json::value() returns the fallback only when the key is ABSENT. When the key is
// present but null it still attempts the conversion and throws type_error.302.
//
// The v2 API serialises unset optional fields as null: icon, obscure_image, group_id, level,
// display_position, ball_count, duration, frame and a rule's subachievement are all null on an
// achievement created without artwork, grouping or limits. Because the whole parse runs inside a
// single try in Net::fetchAchievements, one such field aborted the entire array and the device
// silently ended up with zero achievements, with one ERR line to show for it.
//
// Treat null as equivalent to absent.
template<typename T>
T jsonValue(const nlohmann::json &j, const char *key, T fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return fallback;
    }
    return it->get<T>();
}

/**
 * @brief Parse a GET api/v2/achievements/scorbitron/ payload into Achievement objects.
 *
 * Split out of Net::fetchAchievements so it can be exercised against captured API payloads with no
 * network stack, which is how the null-field defect above is regression-tested.
 *
 * Non-array input yields an empty vector; non-object entries are skipped. A genuine type mismatch
 * (a string where a number belongs) still throws nlohmann::json::exception, so real contract
 * breakage stays loud rather than being silently defaulted away.
 */
inline std::vector<Achievement> parseAchievements(const nlohmann::json &j)
{
    std::vector<Achievement> achievements;
    if (!j.is_array()) {
        return achievements;
    }

    achievements.reserve(j.size());
    for (const auto &item : j) {
        if (!item.is_object()) {
            continue;
        }

        Achievement ach;
        ach.key = jsonValue(item, JKEY_ACH_KEY, std::string {});
        ach.name = jsonValue(item, JKEY_ACH_NAME, std::string {});
        ach.description = jsonValue(item, JKEY_ACH_DESCRIPTION, std::string {});
        ach.scope = jsonValue(item, JKEY_ACH_SCOPE, std::string {});
        ach.imageUrl = jsonValue(item, JKEY_ACH_ICON, std::string {});
        ach.obscureImageUrl = jsonValue(item, JKEY_ACH_OBSCURE_IMAGE, std::string {});
        ach.obscure = jsonValue(item, JKEY_ACH_OBSCURE, false);
        ach.visible = jsonValue(item, JKEY_ACH_VISIBLE, true);
        ach.isTrophy = jsonValue(item, JKEY_ACH_IS_TROPHY, false);
        ach.notifyWhenAchieved = jsonValue(item, JKEY_ACH_NOTIFY_WHEN_ACHIEVED, false);
        ach.groupId = jsonValue(item, JKEY_ACH_GROUP_ID, 0);
        ach.level = jsonValue(item, JKEY_ACH_LEVEL, 0);
        // "complete before ball N" qualifier, never a counter threshold.
        ach.ballCount = jsonValue(item, JKEY_ACH_BALL_COUNT, 0);
        ach.inputTime = jsonValue(item, JKEY_ACH_IS_SINGLE_SESSION, false)
                ? AchievementInputTime::Limited
                : AchievementInputTime::Unlimited;

        if (const auto rulesIt = item.find(JKEY_ACH_RULES);
            rulesIt != item.end() && rulesIt->is_array()) {
            ach.rules.reserve(rulesIt->size());
            for (const auto &ruleJson : *rulesIt) {
                if (!ruleJson.is_object()) {
                    continue;
                }
                AchievementRule rule;
                rule.type = jsonValue(ruleJson, JKEY_ACH_RULE_TYPE, std::string {});
                rule.comparison = jsonValue(ruleJson, JKEY_ACH_RULE_COMPARISON, std::string {">"});
                rule.target = jsonValue(ruleJson, JKEY_ACH_RULE_TARGET, int64_t {0});
                rule.reference = jsonValue(ruleJson, JKEY_ACH_RULE_REFERENCE, std::string {});
                rule.subachievementId = jsonValue(ruleJson, JKEY_ACH_RULE_SUBACHIEVEMENT, 0);
                ach.rules.push_back(std::move(rule));
            }
        }

        // Flat convenience fields are derived from rules[0] only and are therefore lossy for
        // multi-rule achievements; the rules array stays authoritative.
        if (!ach.rules.empty()) {
            const auto &primary = ach.rules.front();
            if (primary.type == "MODE") {
                ach.trigger = AchievementTrigger::Mode;
                ach.modeType = AchievementModeType::Complete;
                ach.modeName = primary.reference;
            } else if (primary.type == "MODE_START") {
                ach.trigger = AchievementTrigger::Mode;
                ach.modeType = AchievementModeType::Start;
                ach.modeName = primary.reference;
            } else if (primary.type == "MODE_STACK") {
                ach.trigger = AchievementTrigger::Mode;
                ach.modeType = AchievementModeType::Stack;
                ach.modeName = primary.reference;
            } else if (primary.type == "SCORE") {
                ach.trigger = AchievementTrigger::Score;
                ach.targetScore = primary.target;
            } else if (primary.type == "ACHIEVEMENT") {
                ach.trigger = AchievementTrigger::SubAchievement;
            }
        }

        achievements.push_back(std::move(ach));
    }

    return achievements;
}

} // namespace detail
} // namespace scorbit
