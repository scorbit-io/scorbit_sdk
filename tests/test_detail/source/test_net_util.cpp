/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "net_util.h"
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Valid HTTPS URL with port", "[exctractHostAndPort]")
{
    std::string url = "https://example.com:443/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol == "https");
    CHECK(result.hostname == "example.com");
    CHECK(result.port == "443");
}

TEST_CASE("Valid HTTP URL with port", "[exctractHostAndPort]")
{
    std::string url = "http://example.com:8080/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol == "http");
    CHECK(result.hostname == "example.com");
    CHECK(result.port == "8080");
}

TEST_CASE("Valid HTTPS URL without port", "[exctractHostAndPort]")
{
    std::string url = "https://example.com/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol == "https");
    CHECK(result.hostname == "example.com");
    CHECK(result.port == "443"); // Default port for HTTPS
}

TEST_CASE("Valid HTTP URL without port", "[exctractHostAndPort]")
{
    std::string url = "http://example.com/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol == "http");
    CHECK(result.hostname == "example.com");
    CHECK(result.port == "80"); // Default port for HTTP
}

TEST_CASE("Invalid URL", "[exctractHostAndPort]")
{
    std::string url = "ftp://example.com/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol.empty());
    CHECK(result.hostname.empty());
    CHECK(result.port.empty());
}
