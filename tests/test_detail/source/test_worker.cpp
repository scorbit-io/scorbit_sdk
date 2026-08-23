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

#include "worker.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <math.h>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

using namespace std::chrono_literals;

TEST_CASE("Worker", "[start/stop]")
{
    Worker worker;

    worker.start();
    CHECK(worker.isRunning());

    worker.stop();
    CHECK(!worker.isRunning());
}

TEST_CASE("Worker", "[postStrand]")
{
    Worker worker;

    worker.start();
    CHECK(worker.isRunning());

    const auto start = std::chrono::high_resolution_clock::now();
    worker.postQueue([] { std::this_thread::sleep_for(100ms); });
    const auto duration1 = std::chrono::high_resolution_clock::now() - start;
    CHECK(duration1 < 3ms);

    worker.stop();

    CHECK(!worker.isRunning());
}

TEST_CASE("Worker", "[postStrand multiple]")
{
    Worker worker;

    worker.start();
    CHECK(worker.isRunning());

    const auto start = std::chrono::high_resolution_clock::now();

    // Make sure that all tasks are executed in order
    int counter = 0;

    worker.postQueue([&counter] {
        if (counter == 0)
            ++counter;
        std::this_thread::sleep_for(50ms);
        if (counter == 1)
            ++counter;
    });

    worker.postQueue([&counter] {
        if (counter == 2)
            ++counter;
        std::this_thread::sleep_for(75ms);
        if (counter == 3)
            ++counter;
    });

    worker.postQueue([&counter] {
        if (counter == 4)
            ++counter;
        std::this_thread::sleep_for(100ms);
        if (counter == 5)
            ++counter;
    });

    worker.stop();
    const auto duration = std::chrono::high_resolution_clock::now() - start;

    CHECK(counter == 6);
    CHECK(!worker.isRunning());
}

TEST_CASE("Worker", "[timer fires]")
{
    Worker worker;
    worker.start();

    std::atomic_bool fired {false};
    worker.startTimer(Worker::Timer::CentrifugoIdleDisconnect, 50ms, [&fired] { fired = true; });

    CHECK(!fired); // not yet due
    std::this_thread::sleep_for(150ms);
    CHECK(fired);

    worker.stop();
}

TEST_CASE("Worker", "[timer cancelled]")
{
    Worker worker;
    worker.start();

    std::atomic_bool fired {false};
    worker.startTimer(Worker::Timer::CentrifugoIdleDisconnect, 100ms, [&fired] { fired = true; });
    worker.stopTimer(Worker::Timer::CentrifugoIdleDisconnect);

    std::this_thread::sleep_for(200ms);
    CHECK(!fired); // cancelled before it was due

    worker.stop();
}

TEST_CASE("Worker", "[timer restart extends deadline]")
{
    Worker worker;
    worker.start();

    std::atomic_int fireCount {0};

    // Restarting an armed timer must replace the pending wait, not queue a second one. This is
    // what makes the idle disconnect reset on activity rather than fire on the original deadline.
    worker.startTimer(Worker::Timer::CentrifugoIdleDisconnect, 100ms,
                      [&fireCount] { ++fireCount; });
    std::this_thread::sleep_for(50ms);
    worker.startTimer(Worker::Timer::CentrifugoIdleDisconnect, 100ms,
                      [&fireCount] { ++fireCount; });

    // Original deadline has passed, but the restart pushed it out
    std::this_thread::sleep_for(80ms);
    CHECK(fireCount == 0);

    std::this_thread::sleep_for(100ms);
    CHECK(fireCount == 1); // exactly once, not once per start

    worker.stop();
}

TEST_CASE("Worker", "[timers independent]")
{
    Worker worker;
    worker.start();

    std::atomic_bool tokenRefreshFired {false};
    std::atomic_bool idleFired {false};

    worker.startTimer(Worker::Timer::TokenRefresh, 50ms,
                      [&tokenRefreshFired] { tokenRefreshFired = true; });
    worker.startTimer(Worker::Timer::CentrifugoIdleDisconnect, 50ms,
                      [&idleFired] { idleFired = true; });

    // Cancelling one must not disturb the other
    worker.stopTimer(Worker::Timer::TokenRefresh);

    std::this_thread::sleep_for(150ms);
    CHECK(!tokenRefreshFired);
    CHECK(idleFired);

    worker.stop();
}

TEST_CASE("Worker", "[timers survive concurrent arming]")
{
    // Timers are armed and cancelled from whichever strand needs them, so the same steady_timer
    // gets touched from several threads at once. Run under a thread sanitizer to see the race
    // this guards against; unsanitized, this still catches a crash or a wedged timer.
    Worker worker;
    worker.start();

    std::atomic_bool stop {false};
    std::vector<std::thread> hammers;
    for (int i = 0; i < 4; ++i) {
        hammers.emplace_back([&worker, &stop] {
            while (!stop) {
                worker.startTimer(Worker::Timer::GameData, 5ms, [] { });
                worker.stopTimer(Worker::Timer::GameData);
            }
        });
    }

    std::this_thread::sleep_for(200ms);
    stop = true;
    for (auto &hammer : hammers) {
        hammer.join();
    }

    // An unrelated timer must still work after all that
    std::atomic_bool fired {false};
    worker.startTimer(Worker::Timer::AuthRetry, 20ms, [&fired] { fired = true; });
    std::this_thread::sleep_for(150ms);
    CHECK(fired);

    worker.stop();
}

TEST_CASE("Worker", "[blocking thread count is clamped]")
{
    // An out-of-range count must not leave the pool without a thread, which would silently
    // swallow every posted task instead of running it.
    for (const int requested : {-1, 0, 1, 4, 100}) {
        Worker worker {0, requested};
        worker.start();

        std::atomic_bool ran {false};
        worker.postQueue([&ran] { ran = true; });

        for (int i = 0; i < 100 && !ran; ++i) {
            std::this_thread::sleep_for(10ms);
        }
        CHECK(ran);

        worker.stop();
    }
}

TEST_CASE("Worker", "[blocking work does not delay timers]")
{
    Worker worker;
    worker.start();

    // Saturate every blocking thread. If timers shared that pool, the one below could not fire
    // until these tasks returned, which is exactly the stall this split exists to prevent.
    std::atomic_int blockersRunning {0};
    for (int i = 0; i < 8; ++i) {
        worker.postQueue([&blockersRunning] {
            ++blockersRunning;
            std::this_thread::sleep_for(400ms);
        });
        worker.post([&blockersRunning] {
            ++blockersRunning;
            std::this_thread::sleep_for(400ms);
        });
    }

    std::atomic_bool fired {false};
    worker.startTimer(Worker::Timer::TokenRefresh, 50ms, [&fired] { fired = true; });

    std::this_thread::sleep_for(200ms);
    CHECK(fired);
    CHECK(blockersRunning > 0); // the blockers really were occupying threads

    worker.stop();
}
