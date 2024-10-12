/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "net.h"

namespace scorbit {
namespace detail {

Net::Net(SignerCallback signer)
    : m_signer(std::move(signer))
{
}

void Net::setHostAndPort(std::string hostname)
{
    m_hostname = std::move(hostname);
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
