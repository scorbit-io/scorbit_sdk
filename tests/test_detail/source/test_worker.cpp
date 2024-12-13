/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Dec 2024
 *
 ****************************************************************************/

#include "worker.h"
#include <catch2/catch_test_macros.hpp>
#include <thread>
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
    CHECK(duration1 < 1ms);

    worker.stop();
    const auto duration2 = std::chrono::high_resolution_clock::now() - start;
    CHECK(abs(duration2 - 100ms) < 10ms);

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
    CHECK(abs(duration - 50ms - 75ms - 100ms) < 20ms);
    CHECK(!worker.isRunning());
}
