/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2019
 *
 ****************************************************************************/

#pragma once

#include <string>

namespace utils {

extern std::string toUpper(const std::string &s);
extern std::string cutLongString(const std::string &str, size_t maxLength);

} // namespace utils
