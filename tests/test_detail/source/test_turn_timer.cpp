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

// SB-6367: the competition timer counts only the player's own turns, on the elapsed clock, until
// the goal is first met.
//
// Every expected duration below is worked out by hand from the timeline beside it, never computed
// with the code under test. Elapsed and UTC times are deliberately unrelated, so a test only passes
// if durations come from the elapsed clock.

#include "competitions/turn_timer.h"
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <vector>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail;
using namespace scorbit::detail::achievements;
using namespace scorbit::detail::competitions;

namespace {

constexpr int64_t S = 1000; // one second, in ms

/** An arbitrary device UTC, far from any elapsed value: 2026-11-14T12:00:00Z. */
constexpr int64_t UTC_BASE = 1'794'657'600'000;

/** Builds a designated game's rows in order, numbering them as the device would. */
class Game
{
public:
    explicit Game(uint64_t firstSequence = 1)
        : m_sequence {firstSequence}
    {
    }

    /** A row committed at @p elapsedMs, with @p player current. UTC tracks elapsed unless set. */
    TimelineRow row(int64_t elapsedMs, PlayerNumber player)
    {
        TimelineRow r;
        r.elapsedMs = elapsedMs;
        r.timeMs = UTC_BASE + m_utcOffsetMs + elapsedMs;
        r.player = player;
        r.ball = 1;
        r.sequence = m_sequence++;
        r.clockSynchronised = m_synchronised;
        return r;
    }

    /** NTP steps device UTC by @p deltaMs from now on; the elapsed clock doesn't move. */
    void stepUtc(int64_t deltaMs) { m_utcOffsetMs += deltaMs; }

    void setSynchronised(bool synchronised) { m_synchronised = synchronised; }

    /** A row is committed that the timer never sees. */
    void lose() { ++m_sequence; }

private:
    uint64_t m_sequence;
    int64_t m_utcOffsetMs {0};
    bool m_synchronised {true};
};

TimingEvidence evidence()
{
    TimingEvidence e;
    e.deviceId = "machine-4379";
    e.bootId = "6c0e9d1a-2f3b-4c5d-8e9f-0a1b2c3d4e5f";
    return e;
}

/** A timer for player 1 that has begun its designated game on @p start. */
TurnTimer begun(const TimelineRow &start, PlayerNumber player = 1)
{
    TurnTimer timer {player, evidence()};
    timer.begin(start);
    return timer;
}

} // namespace

// ---- 1. The whole current-player interval ---------------------------------------------------

TEST_CASE("Launch waiting, play and bonus all count while the player is current",
          "[competitions][turn_timer]")
{
    // P1: 20 s waiting to launch + 40 s of play + 10 s of bonus = 70 s
    Game g;
    auto timer = begun(g.row(0, 1)); // game start, P1 up, ball not yet launched
    timer.onRow(g.row(20 * S, 1), false); // ball launched: first score
    timer.onRow(g.row(60 * S, 1), false); // drained: bonus counting
    CHECK(timer.elapsedAt(60 * S) == 60 * S);

    timer.onRow(g.row(70 * S, 2), false); // bonus over, P2 up
    CHECK(timer.closedMs() == 70 * S);
    CHECK_FALSE(timer.openSinceMs());
}

TEST_CASE("Another player's turn adds nothing; the player's next turn resumes",
          "[competitions][turn_timer]")
{
    // P1 70 s, P2 50 s (adds zero), P1 again 15 s until the goal: 70 + 15 = 85 s
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.onRow(g.row(70 * S, 2), false);
    timer.onRow(g.row(95 * S, 2), false); // P2 still playing
    CHECK(timer.elapsedAt(95 * S) == 70 * S);
    CHECK(timer.elapsedAt(119 * S) == 70 * S);

    timer.onRow(g.row(120 * S, 1), false); // P1's second turn
    CHECK(timer.closedMs() == 70 * S);
    CHECK(timer.openSinceMs() == 120 * S);

    timer.onRow(g.row(135 * S, 1), true);
    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 85 * S);
}

TEST_CASE("Time between balls counts in a one-player game", "[competitions][turn_timer]")
{
    // Ball 1 0–30 s, ball 2 starts at 45 s (15 s between balls), goal at 60 s: 60 s in all
    Game g;
    auto timer = begun(g.row(0, 1));
    auto drain = g.row(30 * S, 1);
    timer.onRow(drain, false);
    auto ball2 = g.row(45 * S, 1);
    ball2.ball = 2;
    timer.onRow(ball2, false);
    auto goal = g.row(60 * S, 1);
    goal.ball = 2;
    timer.onRow(goal, true);

    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 60 * S);
}

TEST_CASE("A player who is not up at the game's start waits at zero", "[competitions][turn_timer]")
{
    // P2 is timed: P1 plays 0–40 s, P2 40–65 s and meets the goal: 25 s
    Game g;
    auto timer = begun(g.row(0, 1), 2);
    CHECK(timer.state() == TurnTimer::State::Running);
    CHECK(timer.elapsedAt(39 * S) == 0);
    CHECK_FALSE(timer.openSinceMs());

    timer.onRow(g.row(40 * S, 2), false);
    timer.onRow(g.row(65 * S, 2), true);
    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 25 * S);
}

TEST_CASE("Several turns across a four-player game", "[competitions][turn_timer]")
{
    // P3 timed. Turns: P1 0–30, P2 30–55, P3 55–100 (45 s), P4 100–130, P1 130–150, P2 150–170,
    // P3 170–182 and goal at 182 (12 s): 45 + 12 = 57 s
    Game g;
    auto timer = begun(g.row(0, 1), 3);
    for (const auto &[at, player] : std::vector<std::pair<int64_t, PlayerNumber>> {
                 {30, 2}, {55, 3}, {80, 3}, {100, 4}, {130, 1}, {150, 2}, {170, 3}}) {
        timer.onRow(g.row(at * S, player), false);
    }
    timer.onRow(g.row(182 * S, 3), true);

    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 57 * S);
}

// ---- 2. Stop at the original completion -------------------------------------------------------

TEST_CASE("The first completion is final: later play changes nothing", "[competitions][turn_timer]")
{
    // Goal first met at 40 s; P1 keeps playing and meets it again at 90 s
    Game g;
    auto timer = begun(g.row(0, 1));
    const auto completing = g.row(40 * S, 1);
    timer.onRow(completing, true);
    REQUIRE(timer.result());
    const auto saved = *timer.result();

    timer.onRow(g.row(60 * S, 2), false);
    timer.onRow(g.row(80 * S, 1), false);
    timer.onRow(g.row(90 * S, 1), true);
    timer.end(120 * S);
    timer.interrupt("unrelated later fault");

    CHECK(timer.state() == TurnTimer::State::Completed);
    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 40 * S);
    CHECK(timer.result()->completionSequence == completing.sequence);
    CHECK(timer.result()->durationMs == saved.durationMs);
    CHECK_FALSE(timer.interruption());
}

TEST_CASE("Every newly designated game starts again at zero", "[competitions][turn_timer]")
{
    Game first;
    auto timer = begun(first.row(0, 1));
    timer.onRow(first.row(90 * S, 1), true);
    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 90 * S);

    // The next game: elapsed time keeps running on the same boot, the timer does not carry over
    Game second {50};
    timer.begin(second.row(500 * S, 1));
    CHECK(timer.state() == TurnTimer::State::Running);
    CHECK_FALSE(timer.result());
    CHECK(timer.closedMs() == 0);
    CHECK(timer.elapsedAt(500 * S) == 0);

    timer.onRow(second.row(530 * S, 1), true);
    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 30 * S);
    CHECK(timer.result()->startSequence == 50);
}

TEST_CASE("A next game also clears an earlier interruption", "[competitions][turn_timer]")
{
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.interrupt("input lost");
    REQUIRE(timer.interruption());

    timer.begin(g.row(200 * S, 1));
    CHECK(timer.state() == TurnTimer::State::Running);
    CHECK_FALSE(timer.interruption());
}

TEST_CASE("A switch and a completion on one row give the same time whoever completed",
          "[competitions][turn_timer]")
{
    // One row at 70 s both hands the turn from P1 to P2 and completes the goal

    SECTION("The player whose turn ends on that row: all 70 s are theirs")
    {
        Game g;
        auto timer = begun(g.row(0, 1), 1);
        timer.onRow(g.row(70 * S, 2), true);
        REQUIRE(timer.result());
        CHECK(timer.result()->durationMs == 70 * S);
    }

    SECTION("The player whose turn starts on that row: none of those 70 s are theirs")
    {
        // P2 0–30, P1 30–70, P2 up again at 70 and completing there: 30 s
        Game g;
        auto timer = begun(g.row(0, 2), 2);
        timer.onRow(g.row(30 * S, 1), false);
        timer.onRow(g.row(70 * S, 2), true);
        REQUIRE(timer.result());
        CHECK(timer.result()->durationMs == 30 * S);
    }

    SECTION("Replaying the same rows gives the same result")
    {
        const auto replay = [] {
            Game g;
            auto timer = begun(g.row(0, 1), 1);
            timer.onRow(g.row(25 * S, 1), false);
            timer.onRow(g.row(70 * S, 2), true);
            return timer.result()->durationMs;
        };
        CHECK(replay() == 70 * S);
        CHECK(replay() == replay());
    }
}

TEST_CASE("A game that ends without the goal leaves no result but uses the attempt",
          "[competitions][turn_timer]")
{
    // P1 0–50, P2 50–80, P1 80–100, game over at 100: 70 s played, no result
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.onRow(g.row(50 * S, 2), false);
    timer.onRow(g.row(80 * S, 1), false);
    timer.end(100 * S);

    CHECK(timer.state() == TurnTimer::State::Ended);
    CHECK_FALSE(timer.result());
    CHECK(timer.attemptConsumed());
    CHECK(timer.closedMs() == 70 * S);
    CHECK_FALSE(timer.openSinceMs());

    timer.onRow(g.row(110 * S, 1), true); // after the end: ignored
    CHECK_FALSE(timer.result());
}

// ---- 3. An elapsed clock wall-clock corrections cannot change ---------------------------------

TEST_CASE("UTC steps forward or backward add and remove nothing", "[competitions][turn_timer]")
{
    // 70 s of P1 time on the elapsed clock, whatever NTP does to UTC meanwhile
    Game g;
    const auto start = g.row(0, 1);
    auto timer = begun(start);
    timer.onRow(g.row(20 * S, 1), false);

    SECTION("Forward an hour")
    {
        g.stepUtc(3600 * S);
    }
    SECTION("Backward ten minutes, to before the game started")
    {
        g.stepUtc(-600 * S);
    }

    timer.onRow(g.row(50 * S, 1), false);
    const auto completing = g.row(70 * S, 1);
    timer.onRow(completing, true);

    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 70 * S);
    // The original UTC is kept as it was, for schedule and calendar eligibility only
    CHECK(timer.result()->startUtcMs == start.timeMs);
    CHECK(timer.result()->completionUtcMs == completing.timeMs);
}

TEST_CASE("UTC that doesn't move at all doesn't stop the timer", "[competitions][turn_timer]")
{
    // Every row carries the same UTC (a frozen RTC); the elapsed clock still measures 45 s
    Game g;
    auto start = g.row(0, 1);
    auto timer = begun(start);
    auto row = g.row(45 * S, 1);
    row.timeMs = start.timeMs;
    timer.onRow(row, true);

    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 45 * S);
}

// ---- 4. Precision and evidence ----------------------------------------------------------------

TEST_CASE("Results carry the evidence they rest on", "[competitions][turn_timer]")
{
    Game g {7};
    const auto start = g.row(1000, 1);
    auto timer = begun(start);
    timer.onRow(g.row(2000, 2), false);
    timer.onRow(g.row(4000, 1), false);
    timer.onRow(g.row(5000, 1), false);
    const auto goal = g.row(6000, 1);
    timer.onRow(goal, true);

    REQUIRE(timer.result());
    const auto &r = *timer.result();
    CHECK(r.durationMs == 1000 + 2000); // 1–2 s, then 4–6 s
    CHECK(r.startSequence == 7);
    CHECK(r.completionSequence == goal.sequence);
    CHECK(r.completionSequence > r.startSequence);
    CHECK(r.startUtcMs == start.timeMs);
    CHECK(r.completionUtcMs == goal.timeMs);
    CHECK(r.utcSynchronised);

    CHECK(r.evidence.policyVersion == TURN_TIMER_POLICY_VERSION);
    CHECK(r.evidence.unit == "ms");
    CHECK(r.evidence.resolutionMs >= 1);
    CHECK(r.evidence.resolutionMs == elapsedClockResolutionMs());
    CHECK(r.evidence.clockSource == elapsedClockSource());
    CHECK(r.evidence.deviceId == "machine-4379");
    CHECK(r.evidence.bootId == "6c0e9d1a-2f3b-4c5d-8e9f-0a1b2c3d4e5f");
}

TEST_CASE("UTC is only vouched for when both rows had a synchronised clock",
          "[competitions][turn_timer]")
{
    Game g;
    g.setSynchronised(false);
    auto timer = begun(g.row(0, 1));
    g.setSynchronised(true);
    timer.onRow(g.row(10 * S, 1), true);

    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 10 * S); // the elapsed clock needs no server's word
    CHECK_FALSE(timer.result()->utcSynchronised);
}

TEST_CASE("The elapsed clock's resolution is never finer than a millisecond",
          "[competitions][turn_timer]")
{
    CHECK(elapsedClockResolutionMs() >= 1);
}

TEST_CASE("The elapsed clock counts suspended time and never runs backwards",
          "[competitions][turn_timer]")
{
    // Suspend itself can't be staged here; what can be checked is that the clock is the one that
    // counts it, rather than steady_clock, whose source stops while the machine sleeps
    const std::string source = elapsedClockSource();
#if defined(_WIN32)
    CHECK((source == "QueryInterruptTimePrecise" || source == "GetTickCount64"));
#elif defined(__APPLE__)
    CHECK(source == "CLOCK_MONOTONIC");
#else
    CHECK(source == "CLOCK_BOOTTIME");
#endif

    auto previous = ElapsedClock::now();
    CHECK(previous.time_since_epoch().count() > 0);
    for (int i = 0; i < 1000; ++i) {
        const auto now = ElapsedClock::now();
        CHECK(now >= previous);
        previous = now;
    }
}

TEST_CASE("A later-UTC 70 s result is faster than an earlier-UTC 90 s one",
          "[competitions][turn_timer]")
{
    // Game A is played first and takes 90 s; game B is played an hour later and takes 70 s
    Game a;
    auto timerA = begun(a.row(0, 1));
    timerA.onRow(a.row(90 * S, 1), true);

    Game b {100};
    b.stepUtc(3600 * S);
    auto timerB = begun(b.row(4000 * S, 1));
    timerB.onRow(b.row(4070 * S, 1), true);

    REQUIRE(timerA.result());
    REQUIRE(timerB.result());
    CHECK(timerB.result()->completionUtcMs > timerA.result()->completionUtcMs);
    CHECK(compareDurations(*timerB.result(), *timerA.result()) < 0);
    CHECK(compareDurations(*timerA.result(), *timerB.result()) > 0);
}

TEST_CASE("Exactly equal durations stay equal whatever UTC or score says",
          "[competitions][turn_timer]")
{
    Game a;
    auto timerA = begun(a.row(0, 1));
    auto rowA = a.row(70 * S, 1);
    rowA.scores = {{1, 5'000'000}};
    timerA.onRow(rowA, true);

    Game b;
    b.stepUtc(86'400 * S);
    auto timerB = begun(b.row(300 * S, 1));
    timerB.onRow(b.row(330 * S, 2), false);
    timerB.onRow(b.row(345 * S, 1), false);
    auto rowB = b.row(385 * S, 1);
    rowB.scores = {{1, 12'000}};
    timerB.onRow(rowB, true); // 30 s + 40 s = 70 s

    REQUIRE(timerA.result());
    REQUIRE(timerB.result());
    CHECK(timerA.result()->durationMs == 70 * S);
    CHECK(timerB.result()->durationMs == 70 * S);
    CHECK(compareDurations(*timerA.result(), *timerB.result()) == 0);
}

TEST_CASE("Differences finer than a second are kept", "[competitions][turn_timer]")
{
    const auto timeTo = [](int64_t completionMs) {
        Game g;
        auto timer = begun(g.row(0, 1));
        timer.onRow(g.row(completionMs, 1), true);
        return *timer.result();
    };

    const auto a = timeTo(70'400);
    const auto b = timeTo(70'900);
    const auto c = timeTo(70'901);
    CHECK(a.durationMs == 70'400);
    CHECK(b.durationMs == 70'900);
    CHECK(compareDurations(a, b) < 0); // both "70 s" to the second, still told apart
    CHECK(compareDurations(b, c) < 0); // a single millisecond counts
}

// ---- 5. Timer state, and stopping when continuity is unknown ----------------------------------

TEST_CASE("Timer state exposes closed time and the open turn's start",
          "[competitions][turn_timer]")
{
    Game g;
    TurnTimer timer {1, evidence()};
    CHECK(timer.state() == TurnTimer::State::Idle);
    CHECK_FALSE(timer.attemptConsumed());

    timer.begin(g.row(10 * S, 1));
    CHECK(timer.attemptConsumed());
    CHECK(timer.closedMs() == 0);
    CHECK(timer.openSinceMs() == 10 * S);

    timer.onRow(g.row(40 * S, 2), false);
    CHECK(timer.closedMs() == 30 * S);
    CHECK_FALSE(timer.openSinceMs());

    timer.onRow(g.row(55 * S, 1), false);
    CHECK(timer.closedMs() == 30 * S);
    CHECK(timer.openSinceMs() == 55 * S);
    CHECK(timer.elapsedAt(65 * S) == 40 * S);
}

TEST_CASE("Rows before any designated game are ignored", "[competitions][turn_timer]")
{
    Game g;
    TurnTimer timer {1, evidence()};
    timer.onRow(g.row(10 * S, 1), true);
    timer.end(20 * S);
    timer.interrupt("no game");
    CHECK(timer.state() == TurnTimer::State::Idle);
    CHECK_FALSE(timer.result());
    CHECK_FALSE(timer.interruption());
    CHECK_FALSE(timer.attemptConsumed());
}

TEST_CASE("Unknown continuity stops qualification instead of dropping time",
          "[competitions][turn_timer]")
{
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.onRow(g.row(30 * S, 1), false);

    SECTION("A row without an elapsed time")
    {
        auto row = g.row(40 * S, 1);
        row.elapsedMs.reset();
        timer.onRow(row, false);
    }
    SECTION("The elapsed clock going backwards")
    {
        timer.onRow(g.row(29 * S, 1), false);
    }
    SECTION("The game ending before the latest row")
    {
        timer.end(20 * S);
    }
    SECTION("A fault reported from outside")
    {
        timer.interrupt("input lost");
    }

    CHECK(timer.state() == TurnTimer::State::Interrupted);
    CHECK(timer.interruption());
    CHECK(timer.attemptConsumed());

    // A completion afterwards can't qualify: the doubtful time would make it look faster
    timer.onRow(g.row(50 * S, 1), true);
    CHECK_FALSE(timer.result());
    CHECK(timer.state() == TurnTimer::State::Interrupted);
}

TEST_CASE("A game start without an elapsed time can't be timed", "[competitions][turn_timer]")
{
    Game g;
    auto start = g.row(0, 1);
    start.elapsedMs.reset();
    auto timer = begun(start);

    CHECK(timer.state() == TurnTimer::State::Interrupted);
    CHECK(timer.attemptConsumed());
    timer.onRow(g.row(10 * S, 1), true);
    CHECK_FALSE(timer.result());
}

TEST_CASE("A fault after the goal keeps the valid result", "[competitions][turn_timer]")
{
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.onRow(g.row(42 * S, 1), true);

    auto broken = g.row(50 * S, 1);
    broken.elapsedMs.reset();
    timer.onRow(broken, false);
    timer.onRow(g.row(10 * S, 1), false); // clock backwards
    timer.interrupt("restart");

    CHECK(timer.state() == TurnTimer::State::Completed);
    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 42 * S);
}

TEST_CASE("Rows that share an elapsed instant are not a fault", "[competitions][turn_timer]")
{
    // The bonus row and the switch row a commit produces share one instant
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.onRow(g.row(30 * S, 1), false); // previous player's bonus row
    timer.onRow(g.row(30 * S, 2), false); // the real row, P2 up
    timer.onRow(g.row(50 * S, 1), false);
    timer.onRow(g.row(55 * S, 1), true);

    CHECK(timer.state() == TurnTimer::State::Completed);
    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 35 * S);
}

TEST_CASE("A lost, repeated or reordered row stops qualification", "[competitions][turn_timer]")
{
    // P1 0–30, P2 30–60; the row at 60 s handing the turn back to P1 is lost. Counting on would
    // credit P1 only from their next row, a faster time than was played.
    Game g;
    auto timer = begun(g.row(0, 1));
    const auto handOver = g.row(30 * S, 2);
    timer.onRow(handOver, false);

    SECTION("Lost")
    {
        g.lose();
        timer.onRow(g.row(90 * S, 1), true);
    }
    SECTION("Repeated")
    {
        timer.onRow(handOver, false);
    }
    SECTION("Out of order")
    {
        const auto later = g.row(60 * S, 1);
        const auto latest = g.row(90 * S, 1);
        timer.onRow(latest, false);
        timer.onRow(later, false);
    }

    CHECK(timer.state() == TurnTimer::State::Interrupted);
    CHECK_FALSE(timer.result());
    CHECK(timer.attemptConsumed());
}

TEST_CASE("A goal already met on the start row completes at zero", "[competitions][turn_timer]")
{
    Game g {12};
    const auto start = g.row(5 * S, 1);

    SECTION("For the player who is up")
    {
        TurnTimer timer {1, evidence()};
        timer.begin(start, true);

        CHECK(timer.state() == TurnTimer::State::Completed);
        REQUIRE(timer.result());
        CHECK(timer.result()->durationMs == 0);
        CHECK(timer.result()->startSequence == 12);
        CHECK(timer.result()->completionSequence == 12);
        CHECK(timer.result()->completionUtcMs == start.timeMs);
        CHECK_FALSE(timer.openSinceMs());

        // Later play neither lengthens nor replaces it
        timer.onRow(g.row(40 * S, 1), true);
        CHECK(timer.result()->durationMs == 0);
        CHECK(timer.result()->completionSequence == 12);
    }
    SECTION("For a player who isn't up yet")
    {
        TurnTimer timer {2, evidence()};
        timer.begin(start, true);
        REQUIRE(timer.result());
        CHECK(timer.result()->durationMs == 0);
    }
}

TEST_CASE("The start row applied again is still a repeated row", "[competitions][turn_timer]")
{
    // The start row's verdict belongs in begin(); feeding the row back is a continuity fault
    Game g;
    const auto start = g.row(0, 1);
    auto timer = begun(start);
    timer.onRow(start, true);

    CHECK(timer.state() == TurnTimer::State::Interrupted);
    CHECK_FALSE(timer.result());
}

TEST_CASE("A game start without a place in the row order can't be timed",
          "[competitions][turn_timer]")
{
    Game g;
    auto start = g.row(0, 1);
    start.sequence = 0;
    auto timer = begun(start);
    CHECK(timer.state() == TurnTimer::State::Interrupted);
}

TEST_CASE("The goal met on a bonus row is final; the switch row at that instant changes nothing",
          "[competitions][turn_timer]")
{
    // P1 0–40; one commit at 40 s gives P1's bonus row (goal met there) and the row with P2 up
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.onRow(g.row(40 * S, 1), true);
    timer.onRow(g.row(40 * S, 2), false);

    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 40 * S);
    CHECK(timer.state() == TurnTimer::State::Completed);
}

TEST_CASE("A game designated while another runs discards the earlier time",
          "[competitions][turn_timer]")
{
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.onRow(g.row(80 * S, 1), false);

    timer.begin(g.row(100 * S, 1));
    timer.onRow(g.row(120 * S, 1), true);
    REQUIRE(timer.result());
    CHECK(timer.result()->durationMs == 20 * S);
}

TEST_CASE("A game ending in another player's turn adds nothing past their switch",
          "[competitions][turn_timer]")
{
    // P1 0–25, P2 25–100 and the game ends at 100: 25 s
    Game g;
    auto timer = begun(g.row(0, 1));
    timer.onRow(g.row(25 * S, 2), false);
    timer.end(100 * S);

    CHECK(timer.state() == TurnTimer::State::Ended);
    CHECK(timer.closedMs() == 25 * S);
}

TEST_CASE("Results measured under another policy or resolution are not ordered",
          "[competitions][turn_timer]")
{
    const auto timeTo = [](int64_t completionMs) {
        Game g;
        auto timer = begun(g.row(0, 1));
        timer.onRow(g.row(completionMs, 1), true);
        return *timer.result();
    };
    const auto current = timeTo(70 * S);

    auto otherPolicy = timeTo(90 * S);
    otherPolicy.evidence.policyVersion = TURN_TIMER_POLICY_VERSION + 1;
    CHECK(compareDurations(current, otherPolicy) == std::partial_ordering::unordered);

    auto coarser = timeTo(70 * S);
    coarser.evidence.resolutionMs = current.evidence.resolutionMs * 10;
    CHECK(compareDurations(current, coarser) == std::partial_ordering::unordered);
}
