/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Mar 2025
 *
 ****************************************************************************/

#pragma once

#include <boost/json.hpp>
#include <string>

namespace scorbit {
namespace detail {

std::string getUpdateUrl(const boost::json::value &sdkVal);
bool update(const std::string &archivePath);

} // namespace detail
} // namespace scorbit
