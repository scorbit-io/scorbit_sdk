/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Mar 2025
 *
 ****************************************************************************/

#include <utils/date_time_parser.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("parseHttpDateToUnixTimestamp - Valid Dates") {
    CHECK(parseHttpDateToUnixTimestamp("Fri, 21 Mar 2025 12:34:56 GMT") == 1742560496);
    CHECK(parseHttpDateToUnixTimestamp("Wed, 01 Jan 2020 00:00:00 GMT") == 1577836800);
    CHECK(parseHttpDateToUnixTimestamp("Fri, 31 Dec 1999 23:59:59 GMT") == 946684799);
}

TEST_CASE("parseHttpDateToUnixTimestamp - Invalid Dates") {
    CHECK(parseHttpDateToUnixTimestamp("Invalid Date String") == -1);
    CHECK(parseHttpDateToUnixTimestamp("2025-03-21T12:34:56Z") == -1);
    CHECK(parseHttpDateToUnixTimestamp("21 Mar 2025 12:34:56") == -1);
    CHECK(parseHttpDateToUnixTimestamp("") == -1);
}
