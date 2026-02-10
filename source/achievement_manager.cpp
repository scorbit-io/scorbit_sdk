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
#include "logger.h"

namespace scorbit {
namespace detail {

// ---- Achievement Definition Cache ----

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

// ---- User Progress Cache ----

void AchievementManager::setUserProgress(int64_t userId, std::vector<AchievementProgress> progress)
{
    std::lock_guard lock(m_progressMutex);
    auto &userMap = m_userProgress[userId];
    userMap.clear();

    for (auto &p : progress) {
        userMap[p.key] = std::move(p);
    }

    INF("Progress cache updated for user {} with {} entries", userId, userMap.size());
}

std::optional<std::vector<AchievementProgress>> AchievementManager::getUserProgress(
        int64_t userId) const
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

std::optional<AchievementProgress> AchievementManager::getProgress(int64_t userId,
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

void AchievementManager::updateProgress(int64_t userId, const std::string &key, int progress,
                                        bool unlocked)
{
    std::lock_guard lock(m_progressMutex);
    auto &userMap = m_userProgress[userId];
    auto &prog = userMap[key];
    prog.key = key;
    prog.progress = progress;
    prog.unlocked = unlocked;
    // Note: unlockedAt timestamp would be set by server
}

void AchievementManager::clearUserProgress(int64_t userId)
{
    std::lock_guard lock(m_progressMutex);
    m_userProgress.erase(userId);
}

void AchievementManager::clearAllProgress()
{
    std::lock_guard lock(m_progressMutex);
    m_userProgress.clear();
}

// ---- Local Achievement Matching ----

std::vector<std::string> AchievementManager::checkModeAchievements(const std::string &modeName,
                                                                   const std::string &modeType,
                                                                   int64_t userId) const
{
    std::vector<std::string> matched;

    std::lock_guard achLock(m_achievementsMutex);
    std::lock_guard progLock(m_progressMutex);

    for (const auto &ach : m_achievements) {
        // Skip if not a mode-based trigger
        if (ach.trigger != AchievementTrigger::Mode) {
            continue;
        }

        // Check mode name match
        if (ach.modeName != modeName) {
            continue;
        }

        // Check mode type match
        AchievementModeType expectedType = AchievementModeType::None;
        if (modeType == "start") {
            expectedType = AchievementModeType::Start;
        } else if (modeType == "complete") {
            expectedType = AchievementModeType::Complete;
        } else if (modeType == "stack") {
            expectedType = AchievementModeType::Stack;
        }

        if (ach.modeType != expectedType) {
            continue;
        }

        // Check if already unlocked (skip if permanent achievement already earned)
        auto userIt = m_userProgress.find(userId);
        if (userIt != m_userProgress.end()) {
            auto progIt = userIt->second.find(ach.key);
            if (progIt != userIt->second.end() && progIt->second.unlocked) {
                // Already unlocked and it's a permanent (unlimited) achievement
                if (ach.inputTime == AchievementInputTime::Unlimited) {
                    continue;
                }
            }
        }

        matched.push_back(ach.key);
    }

    return matched;
}

std::vector<std::string> AchievementManager::checkScoreAchievements(int64_t score,
                                                                    int64_t userId) const
{
    std::vector<std::string> matched;

    std::lock_guard achLock(m_achievementsMutex);
    std::lock_guard progLock(m_progressMutex);

    for (const auto &ach : m_achievements) {
        // Skip if not a score-based trigger
        if (ach.trigger != AchievementTrigger::Score) {
            continue;
        }

        // Check if score meets threshold
        if (score < ach.targetScore) {
            continue;
        }

        // Check if already unlocked
        auto userIt = m_userProgress.find(userId);
        if (userIt != m_userProgress.end()) {
            auto progIt = userIt->second.find(ach.key);
            if (progIt != userIt->second.end() && progIt->second.unlocked) {
                if (ach.inputTime == AchievementInputTime::Unlimited) {
                    continue;
                }
            }
        }

        matched.push_back(ach.key);
    }

    return matched;
}

bool AchievementManager::incrementProgress(const std::string &key, int64_t userId, int increment)
{
    auto achOpt = getAchievement(key);
    if (!achOpt) {
        WRN("Cannot increment progress for unknown achievement: {}", key);
        return false;
    }

    const auto &ach = *achOpt;

    std::lock_guard lock(m_progressMutex);
    auto &userMap = m_userProgress[userId];
    auto &prog = userMap[key];

    if (prog.key.empty()) {
        prog.key = key;
        prog.progress = 0;
        prog.unlocked = false;
    }

    // Already unlocked - no further progress needed (unless trophy that can be lost)
    if (prog.unlocked && !ach.isTrophy) {
        return false;
    }

    prog.progress += increment;

    // Check if achievement is now unlocked
    bool newlyUnlocked = false;
    if (!prog.unlocked && prog.progress >= ach.count) {
        prog.unlocked = true;
        newlyUnlocked = true;
        INF("Achievement unlocked locally: key={}, user={}, progress={}/{}", key, userId,
            prog.progress, ach.count);
    }

    // Notify callback
    notifyTriggered(key, userId, newlyUnlocked, prog.progress);

    return newlyUnlocked;
}

// ---- DMD Frame Cache ----

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
    m_triggeredCallback = std::move(callback);
}

void AchievementManager::notifyTriggered(const std::string &key, int64_t userId, bool isUnlock,
                                         int progress)
{
    if (m_triggeredCallback) {
        m_triggeredCallback(key, userId, isUnlock, progress);
    }
}

} // namespace detail
} // namespace scorbit
