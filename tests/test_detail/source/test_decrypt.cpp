/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Feb 2025
 *
 ****************************************************************************/

#include "utils/decrypt.h"
#include "utils/bytearray.h"

#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Decrypt")
{
    // Make sure that encrypted key is decrypted correctly.
    // Encrypted key TEST_ENCRYPTED_KEY is generated during cmake configuration
    auto decrypted = ByteArray(decryptSecret(TEST_ENCRYPTED_KEY, TEST_PROVIDER TEST_SECRET)).hex();
    CHECK(decrypted == TEST_KEY);
}
