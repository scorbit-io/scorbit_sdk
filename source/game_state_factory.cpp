/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "scorbit_sdk/game_state_factory.h"
#include "scorbit_sdk/net_types.h"
#include "net.h"
#include <memory>

namespace scorbit {

GameState createGameState(SignerCallback signer, const std::string &hostname)
{
    auto net = std::make_unique<detail::Net>(std::move(signer));
    net->setHostname(hostname);
    return GameState(std::move(net));
}

} // namespace scorbit
