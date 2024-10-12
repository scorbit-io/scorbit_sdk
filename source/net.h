/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/export.h>
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

    void setHostAndPort(std::string host);

    void authenticate();
    bool isAuthenticated() const;

    void sendGameData(const detail::GameData &data) override;

private:
    SignerCallback m_signer;
    std::string m_hostname;
    bool m_isAuthenticated {false};
};

} // namespace detail
} // namespace scorbit
