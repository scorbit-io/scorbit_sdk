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
        rv.push_back(
                Occurrence {a.startMs, a.ball, a.span, 1, until(a.presenceEndMs, a.presenceOpen)});
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

std::vector<Duration> PlayerFacts::activationDurations(const std::string &mode) const
{
    std::vector<Duration> rv;
    for (const auto &a : entriesOf(m_activations, mode)) {
        rv.push_back(Duration {elapsed(a.startMs, a.endMs, a.open), a.ball, a.span, a.startMs,
                               until(a.presenceEndMs, a.presenceOpen)});
    }
    return rv;
}

std::vector<Duration> PlayerFacts::ballDurations() const
{
    std::vector<Duration> rv;
    for (size_t i = 0; i < m_balls.size(); ++i) {
        const auto &b = m_balls[i];
        rv.push_back(Duration {elapsed(b.startMs, b.endMs, b.open), b.ball, i, b.startMs,
                               until(b.endMs, b.open)});
    }
    return rv;
}

int64_t PlayerFacts::sessionTimeMs() const
{
    const int64_t running =
            m_openTurnStartMs ? std::max<int64_t>(0, m_nowMs - *m_openTurnStartMs) : 0;
    return m_closedTurnsMs + running;
}

std::vector<TimeInterval> PlayerFacts::turns() const
{
    auto rv = m_closedTurns;
    if (m_openTurnStartMs) {
        rv.push_back({*m_openTurnStartMs, until(0, true)});
    }
    return rv;
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

std::vector<Occurrence> SessionFacts::stacks(const std::vector<std::string> &modes,
                                             PlayerNumber player) const
{
    if (modes.empty()) {
        return {};
    }

    // Rows in which every mode seen so far is present: intersections of their runs, each
    // remembering the run that started last, as that is the row on which the stack formed
    struct Overlap {
        size_t startRow;
        size_t endRow;
        int64_t endMs;
        const PresenceRun *formedBy;
    };
    const auto overlapsOf = [this](const std::vector<PresenceRun> &runs) {
        std::vector<Overlap> rv;
        for (const auto &run : runs) {
            rv.push_back({run.startRow, run.endRow.value_or(SIZE_MAX),
                          run.endRow ? run.endMs : m_nowMs + 1, &run});
        }
        return rv;
    };

    auto overlaps = overlapsOf(entriesOf(m_presence, modes.front()));
    for (size_t m = 1; m < modes.size() && !overlaps.empty(); ++m) {
        const auto runs = overlapsOf(entriesOf(m_presence, modes[m]));
        std::vector<Overlap> next;
        for (size_t i = 0, j = 0; i < overlaps.size() && j < runs.size();) {
            const auto &a = overlaps[i];
            const auto &b = runs[j];
            if (const auto start = std::max(a.startRow, b.startRow),
                end = std::min(a.endRow, b.endRow);
                start < end) {
                next.push_back({start, end, a.endRow <= b.endRow ? a.endMs : b.endMs,
                                a.startRow >= b.startRow ? a.formedBy : b.formedBy});
            }
            (a.endRow < b.endRow ? i : j)++;
        }
        overlaps = std::move(next);
    }

    std::vector<Occurrence> rv;
    for (const auto &o : overlaps) {
        if (o.formedBy->player == player) {
            rv.push_back(Occurrence {o.formedBy->startMs, o.formedBy->ball, o.formedBy->span, 1,
                                     o.endMs});
        }
    }
    return rv;
}

std::vector<ModeRun> SessionFacts::modeRuns(const std::string &mode, PlayerNumber player) const
{
    std::vector<ModeRun> rv;
    for (const auto &run : entriesOf(m_presence, mode)) {
        if (run.player != player) {
            continue;
        }
        const bool open = !run.endRow;
        const auto gain =
                std::max<int64_t>(0, (open ? scoreOf(player) : run.endScore) - run.startScore);
        rv.push_back(ModeRun {run.startMs, open ? std::nullopt : std::optional {run.endMs},
                              run.ball, run.span, gain, std::max(run.peakGain, gain)});
    }
    return rv;
}

void SessionFacts::vouchFor(const TimelineRow &row, int64_t untilMs)
{
    // At least the row's own instant, even when the next row shares its millisecond
    const TimeInterval period {row.timeMs, std::max(untilMs, row.timeMs + 1)};
    if (!m_synchronisedPeriods.empty() && period.startMs <= m_synchronisedPeriods.back().endMs) {
        auto &last = m_synchronisedPeriods.back();
        last.endMs = std::max(last.endMs, period.endMs);
    } else {
        m_synchronisedPeriods.push_back(period);
    }
}

IntervalSet SessionFacts::synchronisedPeriods() const
{
    auto periods = m_synchronisedPeriods;
    if (m_prev && m_prev->clockSynchronised && !m_finished) {
        periods.push_back({m_prev->timeMs, m_nowMs + 1});
    }
    return IntervalSet {std::move(periods)};
}

int64_t SessionFacts::scoreOf(PlayerNumber player) const
{
    return this->player(player).latestScore().value_or(0);
}

void SessionFacts::closeRun(const std::string &mode, int64_t timeMs)
{
    auto &run = m_presence[mode].back();
    run.endRow = m_rows;
    run.endMs = timeMs;
    run.endScore = scoreOf(run.player);
    run.peakGain = std::max(run.peakGain, run.endScore - run.startScore);

    // The owner's activation of it is the latest one: a mode can't be activated twice at once
    if (auto *f = facts(run.player)) {
        if (auto &list = f->m_activations[mode]; !list.empty()) {
            list.back().presenceEndMs = timeMs;
            list.back().presenceOpen = false;
        }
    }
}

void SessionFacts::closeTurn(PlayerNumber player, int64_t timeMs)
{
    auto *f = facts(player);
    if (!f || !f->m_openTurnStartMs) {
        return;
    }
    f->m_closedTurnsMs += std::max<int64_t>(0, timeMs - *f->m_openTurnStartMs);
    f->m_closedTurns.push_back({*f->m_openTurnStartMs, timeMs});
    f->m_openTurnStartMs.reset();
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
    if (m_prev && m_prev->clockSynchronised) {
        vouchFor(*m_prev, row.timeMs);
    }

    const bool isFirstRow = !m_prev;
    const bool playerChanged = !isFirstRow && row.player != m_prev->player;
    const bool ballChanged = !isFirstRow && (playerChanged || row.ball != m_prev->ball);

    if (ballChanged) {
        m_ballEndedFor = m_prev->player;
        closeBall(row.timeMs);
        if (playerChanged) {
            closeTurn(m_prev->player, row.timeMs);
        }
    }
    if (isFirstRow || ballChanged) {
        openBall(row.player, row.ball, row.timeMs);
    }

    m_nowMs = row.timeMs;
    for (auto &[number, f] : m_players) {
        f.m_nowMs = row.timeMs;
    }

    auto *active = facts(row.player);
    const size_t span = active && !active->m_balls.empty() ? active->m_balls.size() - 1 : 0;

    // Scores first: a mode's activation row and closing row are measured with their own scores
    for (const auto &[number, score] : row.scores) {
        if (auto *f = facts(number)) {
            f->m_scoreValues.insert(score);
            f->m_latestScore = score;
            f->m_nowMs = row.timeMs;
        }
    }

    if (active && active->m_latestScore) {
        active->m_ownRowScores.push_back({row.timeMs, *active->m_latestScore});
    }

    // Modes: a state, diffed as a set against the previous row
    std::set<std::string> current;
    for (const auto &mode : row.modes) {
        if (isValidTimelineName(mode)) {
            current.insert(mode);
        }
    }
    for (const auto &mode : current) {
        if (m_presentModes.count(mode)) {
            continue;
        }
        m_presence[mode].push_back(PresenceRun {.startRow = m_rows,
                                                .startMs = row.timeMs,
                                                .player = row.player,
                                                .ball = row.ball,
                                                .span = span,
                                                .startScore = scoreOf(row.player)});
        if (active) {
            active->m_activations[mode].push_back(PlayerFacts::Activation {
                    .startMs = row.timeMs, .endMs = row.timeMs, .ball = row.ball, .span = span});
            m_openActivations[mode] = row.player;
        }
    }
    for (const auto &mode : m_presentModes) {
        if (!current.count(mode)) {
            closeRun(mode, row.timeMs);
            closeActivation(mode, row.timeMs);
        }
    }
    m_presentModes = std::move(current);
    for (const auto &mode : m_presentModes) {
        auto &run = m_presence[mode].back();
        run.peakGain = std::max(run.peakGain, scoreOf(run.player) - run.startScore);
    }

    // Completions and events: edges of this row only
    for (const auto &mode : row.completedModes) {
        if (!isValidTimelineName(mode)) {
            continue;
        }
        if (active) {
            active->m_completions[mode].push_back(
                    Occurrence {row.timeMs, row.ball, span, 1, row.timeMs + 1});
        }
        closeActivation(mode, row.timeMs);
    }

    if (active) {
        for (const auto &[name, value] : row.events) {
            if (isValidTimelineName(name)) {
                active->m_eventOps[name].push_back(
                        Occurrence {row.timeMs, row.ball, span, value, row.timeMs + 1});
            }
        }
    }

    m_prev = row;
    ++m_rows;
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

    m_nowMs = std::max(m_nowMs, timeMs);
    if (m_prev->clockSynchronised) {
        vouchFor(*m_prev, timeMs);
    }
    for (auto &[number, f] : m_players) {
        f.m_nowMs = std::max(f.m_nowMs, timeMs);
    }

    for (const auto &mode : m_presentModes) {
        closeRun(mode, timeMs);
    }

    m_ballEndedFor = m_prev->player;
    closeBall(timeMs);
    closeTurn(m_prev->player, timeMs);
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
