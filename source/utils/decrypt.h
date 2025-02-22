/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Feb 2025
 *
 ****************************************************************************/

#pragma once

#include <vector>
#include <string>

namespace scorbit {
namespace detail {

std::vector<uint8_t> base64Decode(const std::string &encoded);
std::vector<uint8_t> decryptSecret(const std::string &encryptedData, const std::string &password);

} // namespace detail
} // namespace scorbit
