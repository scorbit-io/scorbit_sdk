/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2019
 *
 ****************************************************************************/

#include <fmt/format.h>
#include <algorithm>

namespace utils {

std::string toUpper(const std::string &s)
{
    std::string upperStr = s;
    std::transform(upperStr.begin(), upperStr.end(), upperStr.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return upperStr;
}

std::string cutLongString(const std::string &str, size_t maxLength)
{
    auto shortStr = str;
    const auto halfSize = maxLength / 2;
    if (shortStr.size() > maxLength) {
        shortStr = fmt::format("{} ... {}", shortStr.substr(0, halfSize),
                               shortStr.substr(shortStr.length() - halfSize));
    }

    return shortStr;
}

} // namespace utils
