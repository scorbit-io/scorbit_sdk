/****************************************************************************
 *
 * Sep 2026
 *
 ****************************************************************************/

#include <doctest/doctest.h>

#include "tpm_locate.h"

#include <string>

TEST_CASE("tpm_locate: the node the TPM moved to is retried")
{
    const auto moved = tpm_locate::movedPath("/dev/ttyACM1", [] { return "/dev/ttyACM2"; });
    CHECK(moved == "/dev/ttyACM2");
}

TEST_CASE("tpm_locate: the node that just failed is not retried")
{
    CHECK(tpm_locate::movedPath("/dev/ttyACM1", [] { return "/dev/ttyACM1"; }).empty());
}

TEST_CASE("tpm_locate: nothing found means no retry")
{
    CHECK(tpm_locate::movedPath("/dev/ttyACM1", [] { return std::string {}; }).empty());
}

TEST_CASE("tpm_locate: without a locator there is no retry")
{
    CHECK(tpm_locate::movedPath("/dev/ttyACM1", {}).empty());
}
