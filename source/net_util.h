/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <string>

namespace scorbit {
namespace detail {

struct UrlInfo {
    std::string protocol;
    std::string hostname;
    std::string port;
};

UrlInfo exctractHostAndPort(const std::string &url);

} // namespace detail
} // namespace scorbit
