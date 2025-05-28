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
using namespace std::chrono_literals;

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

TEST_CASE("Remove single symbol", "[removeSymbols]")
{
    std::string s {"hello-world"};
    CHECK(removeSymbols(s, "-") == "helloworld");
}

TEST_CASE("Remove few of single symbols", "[removeSymbols]")
{
    std::string s {"=hello=world="};
    CHECK(removeSymbols(s, "=") == "helloworld");
}

TEST_CASE("Remove different symbols", "[removeSymbols]")
{
    std::string s {"{f0b188f8-9f2d-4f8d-abe4-c3107516e7ce}"};
    CHECK(removeSymbols(s, "-{}") == "f0b188f89f2d4f8dabe4c3107516e7ce");
}

TEST_CASE("Derive UUID v5 from given source", "[deriveUuid]")
{
    const auto uuid = deriveUuid("aaa");
    // https://uuidgenerator.dev/uuid-v5 - choose DNS namespace
    CHECK(uuid == "01d2f0ce-8f47-56e4-9a9c-0f368406feb7");

    const auto uuid2 = deriveUuid("52:00:66:74:98:50");
    CHECK(uuid2 == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse UUID", "[parseUuid]")
{
    const auto uuid = parseUuid("f4de2fc0-36bf-5209-b019-d40c961d079e");
    CHECK(uuid == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse UUID with curly braces", "[parseUuid]")
{
    const auto uuid = parseUuid("{f4de2fc0-36bf-5209-b019-d40c961d079e}");
    CHECK(uuid == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse UUID without dashes", "[parseUuid]")
{
    const auto uuid = parseUuid("f4de2fc036bf5209b019d40c961d079e");
    CHECK(uuid == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse UUID without dashes with curly braces", "[parseUuid]")
{
    const auto uuid = parseUuid("{f4de2fc036bf5209b019d40c961d079e}");
    CHECK(uuid == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse incorrect UUID returns empty string", "[parseUuid]")
{
    const auto uuid = parseUuid("f4de2fc0");
    CHECK(uuid.empty());
}

// Creating test case for gameHistoryToCsv
TEST_CASE("Game history to csv", "[gameHistoryToCsv]")
{
    GameHistory history;

    GameData data;
    data.isGameActive = true;
    data.ball = 1;
    data.activePlayer = 1;
    data.players.insert(std::make_pair(1, PlayerState {1, 100}));
    data.timestamp = std::chrono::system_clock::time_point(10s);
    history.push_back(data);

    data.players.at(1).setScore(200, 0);
    data.timestamp = std::chrono::system_clock::time_point(15s);
    history.push_back(data);

    data.isGameActive = false;
    data.ball = 3;
    data.activePlayer = 2;
    data.players.insert(std::make_pair(2, PlayerState {2, 1000}));
    data.timestamp = std::chrono::system_clock::time_point(20s);
    data.modes.addMode("MB:Multiball");
    data.modes.addMode("MB:Multiball2");
    history.push_back(data);

    std::string csv = gameHistoryToCsv(history);
    std::string expectedCsv = "time,p1,p2,p3,p4,p5,p6,player,ball,game_modes\n"
                              "10,100,,,,,,1,1,\n"
                              "15,200,,,,,,1,1,\n"
                              "20,200,1000,,,,,2,3,\"MB:Multiball;MB:Multiball2\"\n";
    CHECK(csv == expectedCsv);
}
