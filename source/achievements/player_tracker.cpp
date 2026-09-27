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

bool PlayerTracker::evaluateAll(const PlayerFacts &facts, const DefinitionSet &definitions)
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
                facts, baselineIt == m_baselines.end() ? nullptr : &baselineIt->second, m_held,
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

void PlayerTracker::reevaluate(const PlayerFacts &facts, const DefinitionSet &definitions)
{
    // Each pass can only add to the held set, so this terminates within one pass per record
    while (evaluateAll(facts, definitions)) { }
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
        if (record.evaluation != EvaluationClass::Unlimited
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

void PlayerTracker::retract(const std::string &key, Record &record)
{
    record.retracted = true;
    record.unlockedLocally = false;
    m_held.erase(key);
    m_updates.push_back({key, AchievementStatus::Retracted, record.rules});
}

void PlayerTracker::withdrawUnsupported(const PlayerFacts &facts, const DefinitionSet &definitions)
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
                    facts, baselineIt == m_baselines.end() ? nullptr : &baselineIt->second, m_held,
                    definitions};
            if (!evaluate(*definition, context).allSatisfied) {
                retract(key, record);
                changed = true;
            }
        }
    }
}

void PlayerTracker::applyOutcome(const ReportItem &sent, const ReportOutcome &outcome,
                                 const PlayerFacts &facts, const DefinitionSet &definitions)
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
        reevaluate(facts, definitions); // Dependents may now resolve
        break;

    case ReportStatus::AlreadyHeld:
        m_held.insert(sent.key);
        if (record.unlockedLocally && !record.confirmed) {
            m_updates.push_back({sent.key, AchievementStatus::AlreadyHeld, record.rules});
        }
        record.confirmed = true;
        break;

    case ReportStatus::InProgress:
        // A claim answered "in progress" was not granted
        if (sent.achieved == true && record.unlockedLocally && !record.confirmed) {
            retract(sent.key, record);
            withdrawUnsupported(facts, definitions);
        }
        break;

    case ReportStatus::Rejected:
        if (record.unlockedLocally && !record.confirmed) {
            retract(sent.key, record);
            withdrawUnsupported(facts, definitions);
        }
        break;
    }
}

std::vector<AchievementUpdate> PlayerTracker::takeUpdates()
{
    return std::exchange(m_updates, {});
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
