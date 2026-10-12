#include <catch2/catch_test_macros.hpp>

#include "atca_retry.h"
#include "tappad_lock_test_path.h"

#include <atca_status.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace atca_retry;
using namespace std::chrono_literals;

TEST_CASE("atcaRetry succeeds without retrying", "[atca_retry]")
{
    int calls = 0;
    const auto status = atcaRetry([&] {
        ++calls;
        return ATCA_SUCCESS;
    });

    CHECK(status == ATCA_SUCCESS);
    CHECK(calls == 1);
}

TEST_CASE("atcaRetry retries a transient bus fault up to the limit", "[atca_retry]")
{
    int calls = 0;
    const auto status = atcaRetry([&] {
        ++calls;
        return ATCA_COMM_FAIL;
    });

    CHECK(status == ATCA_COMM_FAIL);
    CHECK(calls == MAX_RETRY_TIMES);
}

TEST_CASE("atcaRetry stops retrying once the device answers", "[atca_retry]")
{
    int calls = 0;
    const auto status = atcaRetry([&] {
        ++calls;
        return calls < 3 ? ATCA_COMM_FAIL : ATCA_SUCCESS;
    });

    CHECK(status == ATCA_SUCCESS);
    CHECK(calls == 3);
}

// The regression this policy exists for: a stale device path left calib_sign
// being called with a null ATCADevice, and every one of those -30s was retried
// five times over, with a log line each.
TEST_CASE("atcaRetry does not retry a bad parameter", "[atca_retry]")
{
    int calls = 0;
    const auto status = atcaRetry([&] {
        ++calls;
        return ATCA_BAD_PARAM;
    });

    CHECK(status == ATCA_BAD_PARAM);
    CHECK(calls == 1);
}

TEST_CASE("bad parameter is the only status that is not retriable", "[atca_retry]")
{
    CHECK_FALSE(isRetriableStatus(ATCA_BAD_PARAM));

    CHECK(isRetriableStatus(ATCA_COMM_FAIL));
    CHECK(isRetriableStatus(ATCA_GEN_FAIL));
    CHECK(isRetriableStatus(ATCA_RX_CRC_ERROR));
    CHECK(isRetriableStatus(ATCA_RX_FAIL));
    CHECK(isRetriableStatus(ATCA_RX_NO_RESPONSE));
    CHECK(isRetriableStatus(ATCA_WAKE_FAILED));
}

#if defined(SCORBIT_TAPPAD_LOCK)

using tappad_lock_test::OtherHolder;

TEST_CASE("atcaRetry holds the tap pad lock around each attempt", "[atca_retry]")
{
    int depth = -1;
    const auto status = atcaRetry([&] {
        depth = hal_tappad_lock_depth();
        return ATCA_SUCCESS;
    });

    CHECK(status == ATCA_SUCCESS);
    CHECK(depth == 1);
    CHECK(hal_tappad_lock_depth() == 0);
}

TEST_CASE("atcaRetry waits for a busy tap pad within its budget, then runs", "[atca_retry]")
{
    OtherHolder other;
    std::thread releaser([&] {
        std::this_thread::sleep_for(100ms);
        other.release();
    });

    int calls = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto status = atcaRetry(
            [&] {
                ++calls;
                return ATCA_SUCCESS;
            },
            true, 2000);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    releaser.join();

    CHECK(status == ATCA_SUCCESS);
    // The chip sequence never ran while another holder had the lock.
    CHECK(calls == 1);
    CHECK(elapsed >= 80ms);
}

TEST_CASE("atcaRetry reports busy once its lock budget is spent, without waiting again",
          "[atca_retry]")
{
    OtherHolder other;

    int calls = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto status = atcaRetry(
            [&] {
                ++calls;
                return ATCA_SUCCESS;
            },
            true, 100);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(status == TappadLock::BUSY);
    CHECK(isRetriableStatus(status));
    CHECK(calls == 0);
    // One budget for the whole call, not one per retry.
    CHECK(elapsed < 300ms);
}

TEST_CASE("atcaRetry charges only lock waits to its budget, not chip time or retry sleeps",
          "[atca_retry]")
{
    // A COMM_FAIL retries after DELAY_BEFORE_RETRY (RX_FAIL would sleep seconds). Another thread,
    // already started and waiting before the first attempt returns, takes the lock as soon as that
    // attempt releases it and holds it for 40 ms. The second attempt starts while it is held: 60 ms
    // of chip time spent the old 50 ms deadline, but only lock waits count now, and each attempt
    // may wait MIN_ATTEMPT_LOCK_WAIT_MS (150 ms) anyway, so it waits the holder out.
    std::thread holder;
    std::atomic<bool> waiting {false};
    std::atomic<bool> holderHeld {false};
    bool heldAtSecondAttempt = false;
    int calls = 0;
    const auto status = atcaRetry(
            [&] {
                if (++calls == 1) {
                    holder = std::thread([&] {
                        waiting = true;
                        const TappadLock hold; // taken once this attempt releases it
                        holderHeld = hold.ok();
                        std::this_thread::sleep_for(40ms);
                    });
                    while (!waiting) {
                        std::this_thread::sleep_for(1ms);
                    }
                    std::this_thread::sleep_for(60ms); // chip time, not lock wait
                    return ATCA_COMM_FAIL;
                }
                heldAtSecondAttempt = holderHeld;
                return ATCA_SUCCESS;
            },
            true, 50);
    holder.join();

    CHECK(status == ATCA_SUCCESS);
    CHECK(calls == 2);
    // The holder really was in between the attempts, so the second one had to wait for it.
    CHECK(heldAtSecondAttempt);
}

TEST_CASE("atcaRetry gives an attempt its minimum lock wait when the budget is spent",
          "[atca_retry]")
{
    // Held for 40 ms from before the call; with no budget left an attempt still waits up to
    // MIN_ATTEMPT_LOCK_WAIT_MS instead of a 0 ms trylock that would report BUSY.
    OtherHolder other;
    REQUIRE(other.held());
    std::thread releaser([&] {
        std::this_thread::sleep_for(40ms);
        other.release();
    });

    int calls = 0;
    const auto status = atcaRetry(
            [&] {
                ++calls;
                return ATCA_SUCCESS;
            },
            true, 0);
    releaser.join();

    CHECK(status == ATCA_SUCCESS);
    CHECK(calls == 1);
}

TEST_CASE("atcaRetry on an I2C chip does not take the tap pad lock", "[atca_retry]")
{
    OtherHolder other;

    int depth = -1;
    const auto start = std::chrono::steady_clock::now();
    const auto status = atcaRetry(
            [&] {
                depth = hal_tappad_lock_depth();
                return ATCA_SUCCESS;
            },
            false);

    CHECK(status == ATCA_SUCCESS);
    CHECK(depth == 0);
    CHECK(std::chrono::steady_clock::now() - start < 100ms);
}

#endif

TEST_CASE("receive errors back off for seconds, everything else for milliseconds", "[atca_retry]")
{
    CHECK(retryDelayForStatus(ATCA_COMM_FAIL) == DELAY_BEFORE_RETRY);
    CHECK(retryDelayForStatus(ATCA_GEN_FAIL) == DELAY_BEFORE_RETRY);

    for (int i = 0; i < 16; ++i) {
        const auto delay = retryDelayForStatus(ATCA_RX_CRC_ERROR);
        CHECK(delay >= RX_ERROR_RETRY_MIN_DELAY);
        CHECK(delay <= RX_ERROR_RETRY_MAX_DELAY);
    }
}
