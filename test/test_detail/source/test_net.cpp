/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/scorbit_sdk.h>
#include "net.h"
#include "net_util.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

TEST_CASE("version")
{
    CHECK(std::string(SCORBIT_SDK_VERSION) == std::string("0.1.0"));
}

// Using this indirection to mock free function
struct {
    MAKE_MOCK4(signer, void(uint8_t *, size_t *, const uint8_t *, const uint8_t *));
} Signer;

bool signer(Signature &signature, size_t &signatureLen, const Digest &digest, const Key &key,
            void *)
{
    Signer.signer(signature.data(), &signatureLen, digest.data(), key.data());
    return true;
}

TEST_CASE("Net authenticate")
{
    ALLOW_CALL(Signer, signer(_, _, _, _));
    Net net {signer};
}
