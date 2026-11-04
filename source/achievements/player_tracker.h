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

#pragma once

#include "definition.h"
#include "player_facts.h"
#include "progress.h"
#include "report.h"
#include "rule_evaluator.h"
#include "unlock_proximity.h"
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** What happened to an achievement, as told to game code. */
enum class AchievementStatus {
    /** Values changed; reported at a ball boundary. */
    Progress,
    /** The machine decided it is earned; it MAY be presented now (§7.2). */
    UnlockedLocally,
    /**
     * The server granted it in response to a report: `unlocked`, or `already_held` for a claim
     * resent after its first request got no answer, since that request was the grant.
     */
    Confirmed,
    /** The server says the player already held it (`already_held`). */
    AlreadyHeld,
    /** The server refused a local unlock; a client that presented it MUST retract (§7.2). */
    Retracted,
};

struct AchievementUpdate {
    std::string key;
    AchievementStatus status {AchievementStatus::Progress};
    RuleProgressMap rules;
};

/**
 * One claimed player's achievements during a session: the in-memory records of §7.7.
 *
 * - What is tracked: an `in_session` achievement the player already holds is not; any other
 *   `in_session` one starts at zero; every `unlimited` one is seeded from the claim-time baseline
 *   and keeps accumulating even once achieved.
 * - Re-evaluation iterates to a fixed point, so a chain of achievements delivered to this machine
 *   resolves in one pass.
 * - An `in_session` unlock is claimed immediately; `unlimited` records are reported at ball
 *   boundaries when their values changed since the last acknowledged report.
 * - A trophy is never claimed: its holder is a server decision (§7.5). Its values are still
 *   reported, as they are its ranked metric.
 */
class PlayerTracker
{
public:
    PlayerTracker(std::string userId, Baselines baselines);

    const std::string &userId() const { return m_userId; }

    /** Re-evaluates every tracked record against this session's facts, to a fixed point. */
    void reevaluate(const PlayerSession &session, const DefinitionSet &definitions);

    /** `in_session` unlocks decided since the last call, each to be reported at once. */
    std::vector<ReportItem> takeClaims();

    /** `unlimited` records whose values changed since their last acknowledged report. */
    std::vector<ReportItem> collectReport();

    /** Applies the server's verdict on a reported item. */
    void applyOutcome(const ReportItem &sent, const ReportOutcome &outcome,
                      const PlayerSession &session, const DefinitionSet &definitions);

    /** Updates for game code since the last call, in order. */
    std::vector<AchievementUpdate> takeUpdates();

    struct Record {
        EvaluationClass evaluation {EvaluationClass::InSession};

        /** The player holds it: at claim time, or earned this game and not retracted. */
        bool held {false};
        bool isTrophy {false};
        RuleProgressMap rules;
        bool allSatisfied {false};
        bool allJudged {false};
        bool unlockedLocally {false};
        bool confirmed {false};
        bool retracted {false};
        bool claimQueued {false};

        /** The last values the server acknowledged (initially what it already holds). */
        RuleProgressMap acknowledged;
    };

    /** The record for @p key, or nullptr if it is not tracked. */
    const Record *record(const std::string &key) const;

    /** Every tracked record, by key. */
    std::map<std::string, Record> records() const;

    /**
     * The tracked achievements this player looks close to unlocking (see @ref unlockProximity),
     * with how close each is. Those the player holds, has unlocked, or can't unlock on this
     * machine (trophies, refused unlocks) are left out.
     */
    std::vector<std::pair<std::string, UnlockProximity>>
    nearUnlocks(const DefinitionSet &definitions) const;

    /** Whether the player holds @p key: at claim time, or earned and not retracted since. */
    bool holds(const std::string &key) const { return m_held.count(key) != 0; }

    /**
     * The lifetime values of every `unlimited` record, as last sent. Taken when the slot changes
     * hands, after its final @ref collectReport, for @ref reclaimBaselines.
     */
    std::map<std::string, RuleProgressMap> sentValues() const;

private:
    void startTracking(const Definition &definition);
    bool evaluateAll(const PlayerSession &session, const DefinitionSet &definitions);
    void withdrawUnsupported(const PlayerSession &session, const DefinitionSet &definitions);
    void retract(const std::string &key, Record &record);
    ReportItem reportItem(const std::string &key, const Record &record) const;

    std::string m_userId;
    Baselines m_baselines;
    HeldSet m_held;

    std::unordered_map<std::string, Record> m_records;
    std::unordered_set<std::string> m_untracked;

    std::vector<std::string> m_pendingClaims;
    std::vector<AchievementUpdate> m_updates;
};

/**
 * The state for a player claiming a slot again in the same session. @p first is the baseline their
 * previous claim there started from, @p fresh the new fetch, made once every report the slot sent
 * was answered, and @p sent what the slot sent (@ref PlayerTracker::sentValues).
 *
 * The slot's facts run from the start of the session and @p fresh holds what the slot sent, so for
 * each accumulator rule the slot's own contribution (`sent - first`) is taken back out of @p fresh.
 * Whatever else changed meanwhile, on another slot or machine, increase or decrease, is kept.
 * Observation rules merge to a best value, so @p fresh is used as it is; holdings always come from
 * @p fresh.
 */
Baselines reclaimBaselines(const Baselines &first, Baselines fresh,
                           const std::map<std::string, RuleProgressMap> &sent,
                           const DefinitionSet &definitions);

} // namespace achievements
} // namespace detail
} // namespace scorbit
