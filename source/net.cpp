/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "net.h"
#include "net_util.h"
#include <fmt/format.h>

namespace scorbit {
namespace detail {

Net::Net(SignerCallback signer)
    : m_signer(std::move(signer))
{
}

std::string Net::Hostaname() const
{
    return m_hostname;
}

void Net::setHostname(const std::string &hostname)
{
    const auto host = exctractHostAndPort(hostname);
    m_hostname = fmt::format("{}://{}:{}", host.protocol, host.hostname, host.port);
}

void Net::authenticate()
{
}

bool Net::isAuthenticated() const
{
    return m_isAuthenticated;
}

void Net::sendGameData(const detail::GameData &data)
{
    (void)data;
}

} // namespace detail
} // namespace scorbit
