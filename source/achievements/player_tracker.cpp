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

#include "player_tracker.h"
#include <algorithm>
#include <utility>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

/**
 * What the server holds for an achievement nobody has reported yet, as the reference against
 * which "changed" is judged: every rule at zero. Anything the session measured beyond that —
 * even the one game `SESSION` counts from the start — is new.
 */
RuleProgressMap zeroState(const Definition &definition)
{
    RuleProgressMap rules;
    for (size_t i = 0; i < definition.rules.size(); ++i) {
        const auto &rule = definition.rules[i];
        rules[i] = RuleProgress {0, !isQualifier(rule.type)
                                            && satisfies(0, rule.comparison, rule.target)};
    }
    return rules;
}

const RuleProgressMap *rulesOf(const Baselines &baselines, const std::string &key)
{
    const auto it = baselines.find(key);
    return it == baselines.end() ? nullptr : &it->second.rules;
}

const RuleProgressMap *rulesOf(const std::map<std::string, RuleProgressMap> &values,
                               const std::string &key)
{
    const auto it = values.find(key);
    return it == values.end() ? nullptr : &it->second;
}

const RuleProgress *ruleIn(const RuleProgressMap *rules, size_t index)
{
    if (!rules) {
        return nullptr;
    }
    const auto it = rules->find(index);
    return it == rules->end() ? nullptr : &it->second;
}

} // namespace

PlayerTracker::PlayerTracker(std::string userId, Baselines baselines)
    : m_userId {std::move(userId)}
    , m_baselines {std::move(baselines)}
{
    for (const auto &[key, baseline] : m_baselines) {
        if (baseline.achieved) {
            m_held.insert(key);
        }
    }
}

const PlayerTracker::Record *PlayerTracker::record(const std::string &key) const
{
    const auto it = m_records.find(key);
    return it == m_records.end() ? nullptr : &it->second;
}

std::map<std::string, PlayerTracker::Record> PlayerTracker::records() const
{
    std::map<std::string, Record> rv;
    for (const auto &[key, record] : m_records) {
        auto &copy = rv.emplace(key, record).first->second;
        copy.held = m_held.count(key) != 0;
    }
    return rv;
}

void PlayerTracker::startTracking(const Definition &definition)
{
    const auto baselineIt = m_baselines.find(definition.key);
    const auto *baseline = baselineIt == m_baselines.end() ? nullptr : &baselineIt->second;

    // Already earned and it can't be earned twice: nothing to track
    if (definition.evaluation == EvaluationClass::InSession && baseline && baseline->achieved) {
        m_untracked.insert(definition.key);
        return;
    }

    Record record;
    record.evaluation = definition.evaluation;
    record.isTrophy = definition.isTrophy;
    record.acknowledged = definition.evaluation == EvaluationClass::Unlimited && baseline
                                ? baseline->rules
                                : zeroState(definition);
    m_records.emplace(definition.key, std::move(record));
}

bool PlayerTracker::evaluateAll(const PlayerSession &session, const DefinitionSet &definitions)
{
    bool unlockedAny = false;

    for (const auto &definition : definitions.all()) {
        const auto &key = definition.key;
        if (m_untracked.count(key)) {
            continue;
        }
        if (!m_records.count(key)) {
            startTracking(definition);
            if (m_untracked.count(key)) {
                continue;
            }
        }

        auto &record = m_records.at(key);

        // A claimed or refused in_session record is settled for this session
        if (record.evaluation == EvaluationClass::InSession
            && (record.unlockedLocally || record.retracted)) {
            continue;
        }

        const auto baselineIt = m_baselines.find(key);
        const EvaluationContext context {
                session, baselineIt == m_baselines.end() ? nullptr : &baselineIt->second, m_held,
                definitions};
        const auto evaluation = evaluate(definition, context);

        record.rules = evaluation.rules;
        record.allSatisfied = evaluation.allSatisfied;
        record.allJudged = evaluation.rules.size() == definition.rules.size();

        const bool canUnlock = !record.isTrophy && !record.unlockedLocally && !record.retracted
                            && !m_held.count(key);
        if (evaluation.allSatisfied && canUnlock) {
            record.unlockedLocally = true;
            m_held.insert(key);
            unlockedAny = true;

            m_updates.push_back({key, AchievementStatus::UnlockedLocally, record.rules});
            if (record.evaluation == EvaluationClass::InSession) {
                m_pendingClaims.push_back(key);
            }
        }
    }

    return unlockedAny;
}

void PlayerTracker::reevaluate(const PlayerSession &session, const DefinitionSet &definitions)
{
    // Each pass can only add to the held set, so this terminates within one pass per record
    while (evaluateAll(session, definitions)) { }
}

std::vector<ReportItem> PlayerTracker::takeClaims()
{
    std::vector<ReportItem> claims;
    for (const auto &key : m_pendingClaims) {
        auto &record = m_records.at(key);
        record.claimQueued = true;
        claims.push_back(reportItem(key, record));
    }
    m_pendingClaims.clear();
    return claims;
}

std::vector<ReportItem> PlayerTracker::collectReport()
{
    std::vector<ReportItem> items;
    for (const auto &[key, record] : m_records) {
        // A record with no judged rule has nothing to say, e.g. while its clock is unsynchronised
        if (record.evaluation != EvaluationClass::Unlimited || record.rules.empty()
            || record.rules == record.acknowledged) {
            continue;
        }
        items.push_back(reportItem(key, record));
        m_updates.push_back({key, AchievementStatus::Progress, record.rules});
    }
    return items;
}

ReportItem PlayerTracker::reportItem(const std::string &key, const Record &record) const
{
    ReportItem item {key, std::nullopt, record.rules};

    // Silence is how a client says "I don't know" (§10.5): a trophy is the server's call, an
    // unjudged rule withholds the verdict, and a refused unlock is not claimed again
    if (!record.isTrophy && record.allJudged && !record.retracted) {
        item.achieved = record.allSatisfied;
    }
    return item;
}

std::vector<std::pair<std::string, UnlockProximity>>
PlayerTracker::nearUnlocks(const DefinitionSet &definitions) const
{
    std::vector<std::pair<std::string, UnlockProximity>> near;
    for (const auto &[key, record] : m_records) {
        if (record.isTrophy || record.retracted || record.unlockedLocally || m_held.count(key)) {
            continue;
        }
        const auto *definition = definitions.find(key);
        if (!definition) {
            continue;
        }
        if (const auto proximity = unlockProximity(*definition, record.rules); proximity.near) {
            near.emplace_back(key, proximity);
        }
    }
    return near;
}

void PlayerTracker::retract(const std::string &key, Record &record)
{
    record.retracted = true;
    record.unlockedLocally = false;
    m_held.erase(key);
    m_updates.push_back({key, AchievementStatus::Retracted, record.rules});
}

void PlayerTracker::withdrawUnsupported(const PlayerSession &session,
                                        const DefinitionSet &definitions)
{
    // A retraction can pull the ground from under local unlocks that depended on it
    for (bool changed = true; changed;) {
        changed = false;
        for (auto &[key, record] : m_records) {
            if (!record.unlockedLocally || record.confirmed) {
                continue;
            }
            const auto *definition = definitions.find(key);
            if (!definition) {
                continue;
            }
            const auto baselineIt = m_baselines.find(key);
            const EvaluationContext context {
                    session, baselineIt == m_baselines.end() ? nullptr : &baselineIt->second,
                    m_held, definitions};
            if (!evaluate(*definition, context).allSatisfied) {
                retract(key, record);
                changed = true;
            }
        }
    }
}

void PlayerTracker::applyOutcome(const ReportItem &sent, const ReportOutcome &outcome,
                                 const PlayerSession &session, const DefinitionSet &definitions)
{
    const auto it = m_records.find(sent.key);
    if (it == m_records.end()) {
        return;
    }
    auto &record = it->second;
    record.acknowledged = sent.rules;

    switch (outcome.status) {
    case ReportStatus::Unlocked:
        record.confirmed = true;
        m_held.insert(sent.key);
        m_updates.push_back({sent.key, AchievementStatus::Confirmed, record.rules});
        reevaluate(session, definitions); // Dependents may now resolve
        break;

    case ReportStatus::AlreadyHeld:
        m_held.insert(sent.key);
        if (record.unlockedLocally && !record.confirmed) {
            // A lost reply: the claim was granted by the earlier request, which this retry
            // repeats (§7.2)
            const bool grantedEarlier = sent.resent && sent.achieved == true;
            m_updates.push_back(
                    {sent.key,
                     grantedEarlier ? AchievementStatus::Confirmed : AchievementStatus::AlreadyHeld,
                     record.rules});
        }
        record.confirmed = true;
        break;

    case ReportStatus::InProgress:
        // A claim answered "in progress" was not granted
        if (sent.achieved == true && record.unlockedLocally && !record.confirmed) {
            retract(sent.key, record);
            withdrawUnsupported(session, definitions);
        }
        break;

    case ReportStatus::Rejected:
        if (record.unlockedLocally && !record.confirmed) {
            retract(sent.key, record);
            withdrawUnsupported(session, definitions);
        }
        break;
    }
}

std::vector<AchievementUpdate> PlayerTracker::takeUpdates()
{
    return std::exchange(m_updates, {});
}

std::map<std::string, RuleProgressMap> PlayerTracker::sentValues() const
{
    std::map<std::string, RuleProgressMap> sent;
    for (const auto &[key, record] : m_records) {
        if (record.evaluation == EvaluationClass::Unlimited && !record.rules.empty()) {
            sent.emplace(key, record.rules);
        }
    }
    return sent;
}

Baselines reclaimBaselines(const Baselines &first, Baselines fresh,
                           const std::map<std::string, RuleProgressMap> &sent,
                           const DefinitionSet &definitions)
{
    for (auto &[key, baseline] : fresh) {
        const auto *definition = definitions.find(key);
        if (!definition) {
            continue; // not evaluated here, so its values are never added to
        }
        for (size_t index = 0; index < definition->rules.size(); ++index) {
            const auto type = definition->rules[index].type;
            if (isObservation(type) || type == RuleType::Achievement) {
                continue; // merged, not added: the fresh best value is safe to reuse
            }
            const auto *before = ruleIn(rulesOf(first, key), index);
            const auto *slotSent = ruleIn(rulesOf(sent, key), index);
            const int64_t start = before ? before->value : 0;
            const int64_t contribution = slotSent ? subtractSaturating(slotSent->value, start) : 0;

            const auto stored = baseline.rules.find(index);
            if (stored == baseline.rules.end()) {
                // Nothing reached the server for it: the first fetch still stands
                if (before) {
                    baseline.rules[index] = *before;
                }
                continue;
            }
            // The fetch waited for the slot's reports, so it holds all of `contribution`: what is
            // left is everything else, a decrease made elsewhere included
            stored->second.value = subtractSaturating(stored->second.value, contribution);
        }
    }
    return fresh;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
