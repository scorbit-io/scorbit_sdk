/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2026
 *
 ****************************************************************************/

#pragma once

#include "tpm/tappad_lock.h"

#include <logger/logger.h>
#include <atca_status.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <thread>

namespace atca_retry {

using namespace std::chrono_literals;

constexpr auto MAX_RETRY_TIMES = 5;

constexpr auto DELAY_BEFORE_RETRY = 20ms;
constexpr auto RX_ERROR_RETRY_MIN_DELAY = 2s;
constexpr auto RX_ERROR_RETRY_MAX_DELAY = 5s;

inline bool shouldUseRxErrorRetryDelay(ATCA_STATUS status)
{
    return status == ATCA_RX_CRC_ERROR || status == ATCA_RX_FAIL;
}

/**
 * @brief Whether asking the device again could plausibly give a different answer.
 *
 * ATCA_BAD_PARAM is the one status where it cannot. cryptoauthlib returns it
 * when a calib_* call is handed a null device or a malformed buffer -- a
 * caller bug, not a transient bus fault. The device is never even reached, so
 * the retry loop just burns MAX_RETRY_TIMES sleeps and logs a line each time
 * while the answer stays identical.
 *
 * That is not hypothetical: when a re-enumerated probe left HardwareTpm
 * holding a stale device path, every signature attempt opened nothing, called
 * calib_sign with a null ATCADevice, and produced five -30 log lines. At the
 * signer's retry rate that was ~100k lines an hour, which rotated away every
 * other line of history in the log.
 */
inline bool isRetriableStatus(ATCA_STATUS status)
{
    return status != ATCA_BAD_PARAM;
}

inline std::chrono::milliseconds retryDelayForStatus(ATCA_STATUS status)
{
    if (!shouldUseRxErrorRetryDelay(status)) {
        return DELAY_BEFORE_RETRY;
    }

    static thread_local std::mt19937 randomGenerator {std::random_device {}()};

    std::uniform_int_distribution<int64_t> delayDistribution(
            std::chrono::duration_cast<std::chrono::milliseconds>(RX_ERROR_RETRY_MIN_DELAY).count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(RX_ERROR_RETRY_MAX_DELAY)
                    .count());

    return std::chrono::milliseconds(delayDistribution(randomGenerator));
}

/// Every attempt may wait at least this long for the lock, so an attempt that follows a chip
/// error and a retry sleep is not reduced to a 0 ms trylock that reports a spurious BUSY.
constexpr uint32_t MIN_ATTEMPT_LOCK_WAIT_MS = 150;

/**
 * @brief One attempt at @p func, holding the tap pad lock for the whole chip sequence.
 *
 * Each func is one calib_* call, which may issue several commands relying on
 * TempKey surviving between them; the hold keeps other processes out until it
 * returns. It is released before any retry sleep so they can get in between.
 *
 * @param waited Set to the time spent waiting for the lock, chip time excluded.
 */
inline ATCA_STATUS attemptLocked(const std::function<ATCA_STATUS()> &func, uint32_t lockTimeoutMs,
                                 std::chrono::milliseconds &waited)
{
    const auto start = std::chrono::steady_clock::now();
    const TappadLock hold {lockTimeoutMs};
    waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()
                                                                   - start);
    if (!hold.ok()) {
        return hold.status();
    }
    if (!TappadLock::isCrossProcess()) {
        static std::atomic<bool> warned {false};
        if (!warned.exchange(true)) {
            WRN("{}: no usable tap pad lock file; excluding this process only", __func__);
        }
    }
    return func();
}

/**
 * @brief Runs @p func, retrying transient failures.
 *
 * @param tappad Whether the chip is on a tap pad, so each attempt holds the tap pad lock. An I2C
 *               chip passes false and never waits on it.
 * @param lockBudgetMs Total time all attempts together may spend waiting for the lock. Chip time
 *               and retry sleeps do not count against it, and each attempt may wait at least
 *               MIN_ATTEMPT_LOCK_WAIT_MS. An attempt that still finds the tap pad busy has used up
 *               the budget, so the call returns TappadLock::BUSY at once rather than waiting again
 *               on every retry.
 */
inline ATCA_STATUS atcaRetry(const std::function<ATCA_STATUS()> &func, bool tappad = true,
                             uint32_t lockBudgetMs = TappadLock::DEFAULT_TIMEOUT_MS)
{
    std::chrono::milliseconds lockWaited {0};
    ATCA_STATUS status = ATCA_SUCCESS;
    for (int i = 0; i < MAX_RETRY_TIMES; ++i) {
        uint32_t lockWait = 0;
        if (tappad) {
            const int64_t left = static_cast<int64_t>(lockBudgetMs) - lockWaited.count();
            lockWait = static_cast<uint32_t>(
                    std::max<int64_t>(left, static_cast<int64_t>(MIN_ATTEMPT_LOCK_WAIT_MS)));
            std::chrono::milliseconds waited {0};
            status = attemptLocked(func, lockWait, waited);
            lockWaited += waited;
        } else {
            status = func();
        }
        if (status == ATCA_SUCCESS) {
            return status;
        }

        if (tappad && status == TappadLock::BUSY) {
            WRN("{}: tap pad busy (lock not free within {}ms), giving up for now", __func__,
                lockWait);
            return status;
        }

        if (!isRetriableStatus(status)) {
            WRN("{}: error {}, not retriable", __func__, static_cast<int>(status));
            return status;
        }

        if (i + 1 < MAX_RETRY_TIMES) {
            const auto retryDelay = retryDelayForStatus(status);
            if (status != ATCA_COMM_FAIL) {
                WRN("{}: error {}, retrying in {}ms...", __func__, static_cast<int>(status),
                    retryDelay.count());
            }
            std::this_thread::sleep_for(retryDelay);
        }
    }
    if (status != ATCA_COMM_FAIL) {
        WRN("{}: status code {}, giving up after {} retries...", __func__, static_cast<int>(status),
            MAX_RETRY_TIMES);
    }
    return status;
}

} // namespace atca_retry
