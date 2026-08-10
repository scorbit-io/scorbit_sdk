/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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

#include "achievement_manager.h"
#include <logger/logger.h>

namespace scorbit {
namespace detail {

namespace {

// Rule type wire values. The server sends these as strings, and the C API re-exports them as
// strings, so they are compared as strings here rather than being mapped to an enum.
constexpr auto RULE_MODE = "MODE";
constexpr auto RULE_MODE_START = "MODE_START";
constexpr auto RULE_MODE_STACK = "MODE_STACK";
constexpr auto RULE_SCORE = "SCORE";
constexpr auto RULE_PROGRESS = "PROGRESS";
constexpr auto RULE_ACHIEVEMENT = "ACHIEVEMENT";

// A "global" achievement is always server-evaluated, regardless of its rule types - unlike
// every other scope, where routing is derived purely from the rules present. See
// scorbit::Achievement::scope.
constexpr auto SCOPE_GLOBAL = "global";

// Mode event types, as passed to the check* entry points.
constexpr auto MODE_EVENT_START = "start";
constexpr auto MODE_EVENT_COMPLETE = "complete";
constexpr auto MODE_EVENT_STACK = "stack";

/**
 * Rules the server owns outright. Skipped by every local match path without blocking the match -
 * this is what keeps the two engines non-overlapping. Every *other* type a context cannot judge
 * withholds the match instead; see evaluateForMode() / evaluateForScore().
 */
bool isServerOwnedRule(const std::string &type)
{
    return type == RULE_ACHIEVEMENT || type == RULE_PROGRESS;
}

bool isModeRule(const std::string &type)
{
    return type == RULE_MODE || type == RULE_MODE_START || type == RULE_MODE_STACK;
}

/** The mode event type a given mode rule requires. */
const char *requiredModeEvent(const std::string &ruleType)
{
    if (ruleType == RULE_MODE_START) {
        return MODE_EVENT_START;
    }
    if (ruleType == RULE_MODE_STACK) {
        return MODE_EVENT_STACK;
    }
    return MODE_EVENT_COMPLETE; // RULE_MODE
}

/**
 * Locate the PROGRESS rule that governs a counter.
 *
 * With a metric key, match it against rule.reference. Without one, accept the achievement's sole
 * PROGRESS rule; if it has several the request is ambiguous and no rule is returned, so no
 * threshold is applied and the server decides.
 */
const AchievementRule *findProgressRule(const Achievement &ach, const std::string &metricKey)
{
    const AchievementRule *found = nullptr;

    for (const auto &rule : ach.rules) {
        if (rule.type != RULE_PROGRESS) {
            continue;
        }
        if (!metricKey.empty()) {
            if (rule.reference == metricKey) {
                return &rule;
            }
            continue;
        }
        if (found) {
            return nullptr; // ambiguous: several PROGRESS rules and no metric key to pick one
        }
        found = &rule;
    }

    return found;
}

} // namespace

bool satisfies(int64_t value, const std::string &comparison, int64_t target)
{
    if (comparison == "<") {
        return value < target;
    }
    if (comparison == "=") {
        return value == target;
    }
    return value > target; // ">" and the documented default; strict, matching the server
}

// ---- Achievement definition cache ----

void AchievementManager::setAchievements(std::vector<Achievement> achievements)
{
    std::lock_guard lock(m_achievementsMutex);
    m_achievements = std::move(achievements);
    m_achievementIndex.clear();

    for (size_t i = 0; i < m_achievements.size(); ++i) {
        m_achievementIndex[m_achievements[i].key] = i;
    }

    INF("Achievement cache updated with {} achievements", m_achievements.size());
}

bool AchievementManager::hasAchievements() const
{
    std::lock_guard lock(m_achievementsMutex);
    return !m_achievements.empty();
}

std::vector<Achievement> AchievementManager::getAchievements() const
{
    std::lock_guard lock(m_achievementsMutex);
    return m_achievements;
}

std::optional<Achievement> AchievementManager::getAchievement(const std::string &key) const
{
    std::lock_guard lock(m_achievementsMutex);
    auto it = m_achievementIndex.find(key);
    if (it != m_achievementIndex.end() && it->second < m_achievements.size()) {
        return m_achievements[it->second];
    }
    return std::nullopt;
}

void AchievementManager::clearAchievements()
{
    std::lock_guard lock(m_achievementsMutex);
    m_achievements.clear();
    m_achievementIndex.clear();
}

// ---- Per-user progress cache ----

void AchievementManager::setUserProgress(const std::string &userId, std::vector<AchievementProgress> progress)
{
    std::lock_guard lock(m_progressMutex);
    auto &userMap = m_userProgress[userId];
    userMap.clear();

    for (auto &p : progress) {
        const auto key = p.key;
        userMap[key] = std::move(p);
    }

    INF("Progress cache updated for user {} with {} entries", userId, userMap.size());
}

std::optional<std::vector<AchievementProgress>>
AchievementManager::getUserProgress(const std::string &userId) const
{
    std::lock_guard lock(m_progressMutex);
    auto it = m_userProgress.find(userId);
    if (it == m_userProgress.end()) {
        return std::nullopt;
    }

    std::vector<AchievementProgress> result;
    result.reserve(it->second.size());
    for (const auto &[key, prog] : it->second) {
        result.push_back(prog);
    }
    return result;
}

std::optional<AchievementProgress> AchievementManager::getProgress(const std::string &userId,
                                                                  const std::string &key) const
{
    std::lock_guard lock(m_progressMutex);
    auto userIt = m_userProgress.find(userId);
    if (userIt == m_userProgress.end()) {
        return std::nullopt;
    }

    auto progIt = userIt->second.find(key);
    if (progIt == userIt->second.end()) {
        return std::nullopt;
    }

    return progIt->second;
}

void AchievementManager::updateProgress(const std::string &userId, const std::string &key, int progress,
                                        bool unlocked)
{
    std::lock_guard lock(m_progressMutex);
    auto &prog = m_userProgress[userId][key];
    prog.key = key;
    prog.progress = progress;
    prog.unlocked = unlocked;
    // unlockedAt is only ever supplied by the server.
}

void AchievementManager::clearUserProgress(const std::string &userId)
{
    std::lock_guard lock(m_progressMutex);
    m_userProgress.erase(userId);
}

void AchievementManager::clearAllProgress()
{
    std::lock_guard lock(m_progressMutex);
    m_userProgress.clear();
}

// ---- Local matching ----

bool AchievementManager::isAlreadyUnlocked(const Achievement &ach, const std::string &userId) const
{
    auto userIt = m_userProgress.find(userId);
    if (userIt == m_userProgress.end()) {
        return false;
    }

    auto progIt = userIt->second.find(ach.key);
    if (progIt == userIt->second.end() || !progIt->second.unlocked) {
        return false;
    }

    // Unlocked. Skip it only if it is a lifetime achievement; session-scoped ones stay eligible so
    // they can be earned again next session.
    return ach.inputTime == AchievementInputTime::Unlimited;
}

bool AchievementManager::evaluateForMode(const Achievement &ach, const std::string &modeName,
                                         const std::string &modeType,
                                         std::optional<int64_t> score) const
{
    // Mirror the server, which refuses to auto-unlock a rule-less achievement.
    if (ach.rules.empty()) {
        return false;
    }

    bool hasEvaluableRule = false;

    for (const auto &rule : ach.rules) {
        // Server-owned: skipped, not failed. This is the deliberate seam between the two engines.
        if (isServerOwnedRule(rule.type)) {
            continue;
        }

        if (isModeRule(rule.type)) {
            hasEvaluableRule = true;
            if (rule.reference != modeName) {
                return false;
            }
            if (modeType != requiredModeEvent(rule.type)) {
                return false;
            }
            // A mode event is one occurrence of that mode, so the live value is 1. Honouring the
            // rule's own operator means the canonical `> 0` is satisfied, `= 1` is satisfied, and
            // `> 1` is not - we cannot confirm a second occurrence from a single event.
            if (!satisfies(1, rule.comparison, rule.target)) {
                return false;
            }
            continue;
        }

        if (rule.type == RULE_SCORE) {
            // Unevaluable without a score: withhold rather than compare against a fabricated 0.
            if (!score) {
                return false;
            }
            hasEvaluableRule = true;
            if (!satisfies(*score, rule.comparison, rule.target)) {
                return false;
            }
            continue;
        }

        // GAME_CODE, TIMER, EVENT, ATTEMPT and anything unrecognised. Rules are ANDed, so a rule we
        // cannot judge is a reason to withhold the match, never to assume it passed. GAME_CODE in
        // particular must never auto-unlock - the game unlocks it explicitly.
        return false;
    }

    // An achievement whose every rule was skipped is not a local match.
    return hasEvaluableRule;
}

bool AchievementManager::evaluateForScore(const Achievement &ach, int64_t score) const
{
    if (ach.rules.empty()) {
        return false;
    }

    bool hasScoreRule = false;

    for (const auto &rule : ach.rules) {
        if (isServerOwnedRule(rule.type)) {
            continue;
        }

        if (rule.type == RULE_SCORE) {
            hasScoreRule = true;
            if (!satisfies(score, rule.comparison, rule.target)) {
                return false;
            }
            continue;
        }

        // A mode rule cannot be judged without a mode event, and the remaining types cannot be
        // judged at all. Withhold - the mode path will report this achievement when its mode event
        // arrives with the score attached.
        return false;
    }

    // Only report achievements this context actually measured something for.
    return hasScoreRule;
}

std::vector<std::string> AchievementManager::checkModeAchievements(const std::string &modeName,
                                                                  const std::string &modeType,
                                                                  const std::string &userId) const
{
    std::vector<std::string> matched;

    std::lock_guard achLock(m_achievementsMutex);
    std::lock_guard progLock(m_progressMutex);

    for (const auto &ach : m_achievements) {
        if (isAlreadyUnlocked(ach, userId) || ach.scope == SCOPE_GLOBAL) {
            continue;
        }
        if (evaluateForMode(ach, modeName, modeType, std::nullopt)) {
            matched.push_back(ach.key);
        }
    }

    return matched;
}

std::vector<std::string> AchievementManager::checkModeAchievementsWithScore(
        const std::string &modeName, const std::string &modeType, const std::string &userId,
        int64_t score) const
{
    std::vector<std::string> matched;

    std::lock_guard achLock(m_achievementsMutex);
    std::lock_guard progLock(m_progressMutex);

    for (const auto &ach : m_achievements) {
        if (isAlreadyUnlocked(ach, userId) || ach.scope == SCOPE_GLOBAL) {
            continue;
        }
        if (evaluateForMode(ach, modeName, modeType, score)) {
            matched.push_back(ach.key);
        }
    }

    return matched;
}

std::vector<std::string> AchievementManager::checkScoreAchievements(int64_t score,
                                                                    const std::string &userId) const
{
    std::vector<std::string> matched;

    std::lock_guard achLock(m_achievementsMutex);
    std::lock_guard progLock(m_progressMutex);

    for (const auto &ach : m_achievements) {
        if (isAlreadyUnlocked(ach, userId) || ach.scope == SCOPE_GLOBAL) {
            continue;
        }
        if (evaluateForScore(ach, score)) {
            matched.push_back(ach.key);
        }
    }

    return matched;
}

bool AchievementManager::incrementProgress(const std::string &key, const std::string &userId, int increment,
                                           const std::string &metricKey)
{
    // Takes and releases m_achievementsMutex before m_progressMutex, keeping the documented order.
    const auto achOpt = getAchievement(key);
    if (!achOpt) {
        WRN("Cannot increment progress for unknown achievement: {}", key);
        return false;
    }
    const auto &ach = *achOpt;

    // The threshold lives on the achievement's PROGRESS rule, never on the achievement itself.
    // Achievement::ballCount means "complete before ball N" and would unlock every counter on its
    // first increment; it is deliberately not consulted here.
    const AchievementRule *progressRule = findProgressRule(ach, metricKey);

    bool newlyUnlocked = false;
    int progressSnapshot = 0;
    bool notify = false;

    {
        std::lock_guard lock(m_progressMutex);
        auto &prog = m_userProgress[userId][key];

        if (prog.key.empty()) {
            prog.key = key;
        }

        // Already earned and not a trophy: nothing more to accumulate. A trophy can be lost and
        // re-won, so it keeps counting.
        if (prog.unlocked && !ach.isTrophy) {
            return false;
        }

        prog.progress += increment;
        progressSnapshot = prog.progress;
        notify = true;

        if (progressRule && !prog.unlocked
            && satisfies(prog.progress, progressRule->comparison, progressRule->target)) {
            prog.unlocked = true;
            newlyUnlocked = true;
        }
    } // m_progressMutex released here, before any callback runs

    if (newlyUnlocked) {
        INF("Achievement matched locally: key={}, user={}, progress={} {} {}", key, userId,
            progressSnapshot, progressRule->comparison, progressRule->target);
    } else if (!progressRule) {
        DBG("No PROGRESS rule for achievement {} (metric key '{}'); recorded progress {} and left "
            "the unlock decision to the server",
            key, metricKey, progressSnapshot);
    }

    if (notify) {
        notifyTriggered(key, userId, newlyUnlocked, progressSnapshot);
    }

    return newlyUnlocked;
}

// ---- DMD frame cache ----

void AchievementManager::setDmdFrame(const std::string &key, DmdFrame frame)
{
    std::lock_guard lock(m_framesMutex);
    m_framesCache.put(key, std::move(frame));
}

bool AchievementManager::hasDmdFrame(const std::string &key) const
{
    std::lock_guard lock(m_framesMutex);
    return m_framesCache.has(key);
}

DmdFrame AchievementManager::getDmdFrame(const std::string &key) const
{
    std::lock_guard lock(m_framesMutex);
    DmdFrame frame;
    if (m_framesCache.get(key, frame)) {
        return frame;
    }
    return {};
}

std::vector<std::pair<std::string, std::string>> AchievementManager::getFramesToDownload() const
{
    std::vector<std::pair<std::string, std::string>> result;

    std::lock_guard achLock(m_achievementsMutex);
    std::lock_guard frameLock(m_framesMutex);

    for (const auto &ach : m_achievements) {
        if (!ach.imageUrl.empty() && !m_framesCache.has(ach.key)) {
            result.emplace_back(ach.key, ach.imageUrl);
        }
    }

    return result;
}

// ---- Callback ----

void AchievementManager::setTriggeredCallback(AchievementTriggeredCallback callback)
{
    std::lock_guard lock(m_callbackMutex);
    m_triggeredCallback = std::move(callback);
}

void AchievementManager::notifyTriggered(const std::string &key, const std::string &userId, bool isUnlock,
                                         int progress) const
{
    // Copy the callback out, release the lock, then call it. A callback that reaches back into the
    // manager - to read the progress it was just told about, say - must not deadlock.
    AchievementTriggeredCallback callback;
    {
        std::lock_guard lock(m_callbackMutex);
        callback = m_triggeredCallback;
    }

    if (callback) {
        callback(key, userId, isUnlock, progress);
    }
}

} // namespace detail
} // namespace scorbit
