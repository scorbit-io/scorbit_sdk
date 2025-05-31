/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * May 2025
 *
 ****************************************************************************/

#include "safe_multipart.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("SafeMultipart preserves buffer data", "[SafeMultipart]")
{
    std::string key = "file";
    std::string original_data = "This is some test data";

    cpr::Multipart multipart {
            {key, cpr::Buffer {original_data.cbegin(), original_data.cend(), "filename.txt"}},
    };

    SafeMultipart safe_multipart(std::move(multipart));
    cpr::Multipart &safe = safe_multipart.get();

    REQUIRE(safe.parts.size() == 1);
    const auto &part = safe.parts[0];

    REQUIRE(part.is_buffer);
    CHECK(part.name == key);
    CHECK(part.value == "filename.txt");
    CHECK(part.datalen == original_data.size());
    CHECK(part.data != original_data.data()); // must not point to original string
    CHECK(std::memcmp(part.data, original_data.data(), original_data.size()) == 0);

    // Following means that it multipart doesn't point to original data
    original_data = "another data";
    CHECK(std::memcmp(part.data, original_data.data(), original_data.size()) != 0);
}

TEST_CASE("SafeMultipart supports move construction", "[SafeMultipart]") {
    std::string key = "move";
    std::string payload = "Move me!";

    cpr::Multipart multipart{{key, cpr::Buffer{payload.cbegin(), payload.cend(), "movefile.txt"}}};
    SafeMultipart original(std::move(multipart));

    SafeMultipart moved(std::move(original));
    cpr::Multipart& moved_multipart = moved.get();

    REQUIRE(moved_multipart.parts.size() == 1);
    CHECK(moved_multipart.parts[0].name == key);
    CHECK(std::memcmp(moved_multipart.parts[0].data, payload.data(), payload.size()) == 0);
}

TEST_CASE("SafeMultipart supports move assignment", "[SafeMultipart]") {
    std::string key = "assign";
    std::string payload = "Assignment test";

    cpr::Multipart multipart{{key, cpr::Buffer{payload.cbegin(), payload.cend(), "assign.txt"}}};
    SafeMultipart lhs(std::move(multipart));

    SafeMultipart rhs = std::move(lhs); // move assignment via initialization
    cpr::Multipart& rhs_multipart = rhs.get();

    REQUIRE(rhs_multipart.parts.size() == 1);
    CHECK(rhs_multipart.parts[0].name == key);
    CHECK(std::memcmp(rhs_multipart.parts[0].data, payload.data(), payload.size()) == 0);
}

TEST_CASE("SafeMultipart has shallow copy assignment", "[SafeMultipart]") {
    std::string key = "assign";
    std::string payload = "Assignment test";

    cpr::Multipart multipart{{key, cpr::Buffer{payload.cbegin(), payload.cend(), "assign.txt"}}};
    SafeMultipart lhs(std::move(multipart));

    SafeMultipart rhs = lhs; // copy assignment
    cpr::Multipart& rhs_multipart = rhs.get();

    REQUIRE(rhs_multipart.parts.size() == 1);
    CHECK(rhs_multipart.parts[0].name == key);
    CHECK(rhs_multipart.parts[0].data == lhs.get().parts[0].data);
}
