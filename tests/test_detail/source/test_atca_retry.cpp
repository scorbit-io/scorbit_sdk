#include <catch2/catch_test_macros.hpp>

#include "atca_retry.h"

#include <atca_status.h>

using namespace atca_retry;

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
