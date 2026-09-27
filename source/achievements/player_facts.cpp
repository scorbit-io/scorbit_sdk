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


#include "player_facts.h"
#include "../event_ops.h"
#include <algorithm>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

const PlayerFacts EMPTY_FACTS {};

template<typename T>
const std::vector<T> &entriesOf(const std::unordered_map<std::string, std::vector<T>> &map,
                                const std::string &name)
{
    static const std::vector<T> none;
    const auto it = map.find(name);
    return it == map.end() ? none : it->second;
}

} // namespace

// ---- PlayerFacts -------------------------------------------------------------------------------

int64_t PlayerFacts::elapsed(int64_t startMs, int64_t endMs, bool open) const
{
    return std::max<int64_t>(0, (open ? m_nowMs : endMs) - startMs);
}

std::vector<Occurrence> PlayerFacts::activations(const std::string &mode) const
{
    std::vector<Occurrence> rv;
    for (const auto &a : entriesOf(m_activations, mode)) {
        rv.push_back(Occurrence {a.startMs, a.ball, a.span, 1});
    }
    return rv;
}

std::vector<Occurrence> PlayerFacts::completions(const std::string &mode) const
{
    return entriesOf(m_completions, mode);
}

std::vector<Occurrence> PlayerFacts::eventOps(const std::string &name) const
{
    return entriesOf(m_eventOps, name);
}

std::vector<Occurrence> PlayerFacts::stacks(const std::vector<std::string> &modes) const
{
    const auto isActiveAt = [this](const std::string &mode, int64_t timeMs) {
        return std::ranges::any_of(entriesOf(m_activations, mode), [timeMs](const Activation &a) {
            return a.startMs <= timeMs && (a.open || a.endMs > timeMs);
        });
    };

    // A stack can only form at the instant one of its modes activates
    std::vector<Occurrence> rv;
    for (const auto &mode : modes) {
        for (const auto &a : entriesOf(m_activations, mode)) {
            const bool allActive = std::ranges::all_of(
                    modes, [&](const std::string &m) { return isActiveAt(m, a.startMs); });
            const bool alreadyCounted = std::ranges::any_of(
                    rv, [&](const Occurrence &o) { return o.timeMs == a.startMs; });
            if (allActive && !alreadyCounted) {
                rv.push_back(Occurrence {a.startMs, a.ball, a.span, 1});
            }
        }
    }

    std::ranges::sort(rv, {}, &Occurrence::timeMs);
    return rv;
}

std::vector<Duration> PlayerFacts::activationDurations(const std::string &mode) const
{
    std::vector<Duration> rv;
    for (const auto &a : entriesOf(m_activations, mode)) {
        rv.push_back(Duration {elapsed(a.startMs, a.endMs, a.open), a.ball, a.span});
    }
    return rv;
}

std::vector<Duration> PlayerFacts::ballDurations() const
{
    std::vector<Duration> rv;
    for (size_t i = 0; i < m_balls.size(); ++i) {
        const auto &b = m_balls[i];
        rv.push_back(Duration {elapsed(b.startMs, b.endMs, b.open), b.ball, i});
    }
    return rv;
}

int64_t PlayerFacts::sessionTimeMs() const
{
    const int64_t running =
            m_openTurnStartMs ? std::max<int64_t>(0, m_nowMs - *m_openTurnStartMs) : 0;
    return m_closedTurnsMs + running;
}

// ---- SessionFacts ------------------------------------------------------------------------------

PlayerFacts *SessionFacts::facts(PlayerNumber player)
{
    if (player < 1 || player > MAX_TIMELINE_PLAYERS) {
        return nullptr;
    }
    return &m_players[player];
}

const PlayerFacts &SessionFacts::player(PlayerNumber player) const
{
    const auto it = m_players.find(player);
    return it == m_players.end() ? EMPTY_FACTS : it->second;
}

void SessionFacts::closeActivation(const std::string &mode, int64_t timeMs)
{
    const auto it = m_openActivations.find(mode);
    if (it == m_openActivations.end()) {
        return;
    }

    if (auto *f = facts(it->second)) {
        auto &list = f->m_activations[mode];
        if (!list.empty() && list.back().open) {
            list.back().endMs = timeMs;
            list.back().open = false;
        }
    }
    m_openActivations.erase(it);
}

void SessionFacts::closeBall(int64_t timeMs)
{
    while (!m_openActivations.empty()) {
        closeActivation(m_openActivations.begin()->first, timeMs);
    }

    if (auto *f = facts(m_prev->player); f && !f->m_balls.empty() && f->m_balls.back().open) {
        f->m_balls.back().endMs = timeMs;
        f->m_balls.back().open = false;
    }
}

void SessionFacts::openBall(PlayerNumber player, BallNumber ball, int64_t timeMs)
{
    auto *f = facts(player);
    if (!f) {
        return;
    }
    f->m_balls.push_back(PlayerFacts::BallSpan {ball, timeMs, timeMs, true});
    if (!f->m_openTurnStartMs) {
        f->m_openTurnStartMs = timeMs;
    }
}

void SessionFacts::apply(const TimelineRow &row)
{
    if (m_finished) {
        return;
    }

    m_ballEndedFor.reset();

    const bool isFirstRow = !m_prev;
    const bool playerChanged = !isFirstRow && row.player != m_prev->player;
    const bool ballChanged = !isFirstRow && (playerChanged || row.ball != m_prev->ball);

    if (ballChanged) {
        m_ballEndedFor = m_prev->player;
        closeBall(row.timeMs);
        if (auto *f = facts(m_prev->player); f && playerChanged && f->m_openTurnStartMs) {
            f->m_closedTurnsMs += std::max<int64_t>(0, row.timeMs - *f->m_openTurnStartMs);
            f->m_openTurnStartMs.reset();
        }
    }
    if (isFirstRow || ballChanged) {
        openBall(row.player, row.ball, row.timeMs);
    }

    for (auto &[number, f] : m_players) {
        f.m_nowMs = row.timeMs;
    }

    auto *active = facts(row.player);
    const size_t span = active && !active->m_balls.empty() ? active->m_balls.size() - 1 : 0;

    // Modes: a state, diffed as a set against the previous row
    std::set<std::string> current;
    for (const auto &mode : row.modes) {
        if (isValidTimelineName(mode)) {
            current.insert(mode);
        }
    }
    for (const auto &mode : current) {
        if (!m_presentModes.count(mode) && active) {
            active->m_activations[mode].push_back(
                    PlayerFacts::Activation {row.timeMs, row.timeMs, true, row.ball, span});
            m_openActivations[mode] = row.player;
        }
    }
    for (const auto &mode : m_presentModes) {
        if (!current.count(mode)) {
            closeActivation(mode, row.timeMs);
        }
    }
    m_presentModes = std::move(current);

    // Completions and events: edges of this row only
    for (const auto &mode : row.completedModes) {
        if (!isValidTimelineName(mode)) {
            continue;
        }
        if (active) {
            active->m_completions[mode].push_back(Occurrence {row.timeMs, row.ball, span, 1});
        }
        closeActivation(mode, row.timeMs);
    }

    if (active) {
        for (const auto &[name, value] : row.events) {
            if (isValidTimelineName(name)) {
                active->m_eventOps[name].push_back(Occurrence {row.timeMs, row.ball, span, value});
            }
        }
    }

    // Scores: every value each player's column takes
    for (const auto &[number, score] : row.scores) {
        if (auto *f = facts(number)) {
            f->m_scoreValues.insert(score);
            f->m_latestScore = score;
            f->m_nowMs = row.timeMs;
        }
    }

    m_prev = row;
}

void SessionFacts::finish(int64_t timeMs)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_ballEndedFor.reset();

    if (!m_prev) {
        return;
    }

    for (auto &[number, f] : m_players) {
        f.m_nowMs = std::max(f.m_nowMs, timeMs);
    }

    m_ballEndedFor = m_prev->player;
    closeBall(timeMs);
    if (auto *f = facts(m_prev->player); f && f->m_openTurnStartMs) {
        f->m_closedTurnsMs += std::max<int64_t>(0, timeMs - *f->m_openTurnStartMs);
        f->m_openTurnStartMs.reset();
    }
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
