/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Mar 2025
 *
 ****************************************************************************/

#pragma once

#include <string>
#include <cstdint>

// Function to parse HTTP date header and convert it to Unix timestamp
// E.g. Wed, 21 Oct 2037 07:28:00 GMT
int64_t parseHttpDateToUnixTimestamp(const std::string &httpDate);
