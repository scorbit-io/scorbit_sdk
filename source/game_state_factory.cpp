/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "scorbit_sdk/game_state_factory.h"
#include "scorbit_sdk/net_types.h"
#include "net.h"
#include "logger.h"
#include "utils/decrypt.h"
#include "utils/signer.h"
#include <obfuscate.h>
#include <memory>

namespace scorbit {

using namespace detail;

GameState createGameState(SignerCallback signer, const DeviceInfo &deviceInfo)
{
    auto net = std::make_unique<detail::Net>(std::move(signer), deviceInfo);
    return GameState(std::move(net));
}

GameState createGameState(std::string encryptedKey, const DeviceInfo &deviceInfo)
{
    auto signer = [encryptedKey = std::move(encryptedKey),
                   provider = deviceInfo.provider](const Digest &digest) {
        Signature signature;

        auto decrypted = decryptSecret(
                encryptedKey, provider + std::string(AY_OBFUSCATE(SCORBIT_SDK_ENCRYPT_SECRET)));
        if (!decrypted.empty()) {
            const auto key = decryptSecret(encryptedKey, provider + SCORBIT_SDK_ENCRYPT_SECRET);

            const auto result = Signer::sign(signature, digest, key);
            if (result != SignErrorCode::Ok) {
                ERR("Failed to sign the digest. Error code: {}", static_cast<int>(result));
                signature.clear();
            }
        }
        return signature;
    };

    return createGameState(std::move(signer), deviceInfo);
}


} // namespace scorbit
