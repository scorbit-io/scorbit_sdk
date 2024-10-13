/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include "scorbit_sdk/net_types.h"
#include "scorbit_sdk/net_base.h"
#include "game_data.h"
#include <string>

namespace scorbit {
namespace detail {

class Net : public NetBase
{
public:
    Net(SignerCallback signer);

    std::string Hostaname() const;
    void setHostname(const std::string &hostname);

    void authenticate();
    bool isAuthenticated() const;

    void sendGameData(const detail::GameData &data) override;

private:
    SignerCallback m_signer;
    bool m_isAuthenticated {false};
    std::string m_hostname;
};

} // namespace detail
} // namespace scorbit
