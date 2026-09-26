/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2026
 *
 ****************************************************************************/

#pragma once

#include <logger/logger.h>
#include <atca_status.h>

#include <chrono>
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

inline ATCA_STATUS atcaRetry(const std::function<ATCA_STATUS()> &func)
{
    ATCA_STATUS status = ATCA_SUCCESS;
    for (int i = 0; i < MAX_RETRY_TIMES; ++i) {
        status = func();
        if (status == ATCA_SUCCESS) {
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
