/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "net_types_c.h"
#include <array>
#include <functional>

namespace scorbit {

constexpr auto DIGEST_LENGTH = SB_DIGEST_LENGTH;
constexpr auto UUID_LENGTH = SB_UUID_LENGTH;
constexpr auto SIGNATURE_MAX_LENGTH = SB_SIGNATURE_MAX_LENGTH;
constexpr auto KEY_LENGTH = SB_KEY_LENGTH;

using Signature = std::array<uint8_t, SIGNATURE_MAX_LENGTH>;
using Digest = std::array<uint8_t, DIGEST_LENGTH>;
using Key = std::array<uint8_t, KEY_LENGTH>;

using SignerCallback = std::function<bool(Signature &signature, size_t &signatureLen,
                                          const Digest &digest, const Key &key, void *userData)>;

} // namespace scorbit
