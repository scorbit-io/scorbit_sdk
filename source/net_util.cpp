/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "net_util.h"
#include <regex>

namespace scorbit {
namespace detail {

// Function to extract protocol, hostname, and port
UrlInfo exctractHostAndPort(const std::string &url)
{
    static const std::regex url_regex(R"((https?):\/\/([^\/:]+)(?::(\d+))?)");
    std::smatch url_match_result;

    UrlInfo result;

    if (std::regex_search(url, url_match_result, url_regex)) {
        result.protocol = url_match_result[1].str();
        result.hostname = url_match_result[2].str();
        result.port = url_match_result[3].str();

        // If no port is found, assign a default one based on the protocol
        if (result.port.empty()) {
            if (result.protocol == "https") {
                result.port = "443"; // Default port for HTTPS
            } else if (result.protocol == "http") {
                result.port = "80"; // Default port for HTTP
            }
        }
    }

    return result;
}

std::string removeSymbols(std::string_view str, std::string_view symbols)
{
    std::string result {str};
    for (const auto &symbol : symbols) {
        result.erase(std::remove(result.begin(), result.end(), symbol), result.end());
    }

    return result;
}

} // namespace detail
} // namespace scorbit
