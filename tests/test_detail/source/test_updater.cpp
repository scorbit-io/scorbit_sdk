/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Apr 2025
 *
 ****************************************************************************/

#include <updater.h>
#include <catch2/catch_test_macros.hpp>

using namespace scorbit::detail;

TEST_CASE("getUpdateUrl - tgz")
{
    const auto json = R"(
        {
          "version": "1.0.5",
          "assets_json": [
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-arm64_testabi.tgz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-arm_testabi.tgz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-testarch_testabi.tgz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-amd64_testabi.tgz"
          ]
        }
    )";

    const auto s = getUpdateUrl(boost::json::parse(json));
    CHECK_FALSE(s.empty());
}

TEST_CASE("getUpdateUrl - tar.gz")
{
    const auto json = R"(
        {
          "version": "1.0.5",
          "assets_json": [
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-arm64_testabi.tar.gz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-arm_testabi.tar.gz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-testarch_testabi.tar.gz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-amd64_testabi.tar.gz"
          ]
        }
    )";

    const auto s = getUpdateUrl(boost::json::parse(json));
    CHECK_FALSE(s.empty());
}

TEST_CASE("getUpdateUrl - empty assets")
{
    const auto json = R"(
        {
          "version": "1.0.5",
          "assets_json": [
          ]
        }
    )";

    const auto s = getUpdateUrl(boost::json::parse(json));
    CHECK(s.empty());
}

TEST_CASE("getUpdateUrl - no version")
{
    const auto json = R"(
        {
          "assets_json": [
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-arm64_testabi.tgz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-arm_testabi.tgz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-testarch_testabi.tgz",
            "https://github.com/scorbit-io/scorbit_sdk/releases/download/1.0.5/scorbit_sdk-1.0.5-amd64_testabi.tgz"
          ]
        }
    )";

    const auto s = getUpdateUrl(boost::json::parse(json));
    CHECK(s.empty());
}
