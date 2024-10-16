/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/scorbit_sdk.h>
#include "net.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

// Using this indirection to mock free function
struct {
    MAKE_MOCK3(signer, void(uint8_t *, size_t *, const uint8_t *));
} Signer;

bool signer(Signature &signature, size_t &signatureLen, const Digest &digest)
{
    Signer.signer(signature.data(), &signatureLen, digest.data());
    return true;
}

TEST_CASE("Net authenticate")
{
    ALLOW_CALL(Signer, signer(_, _, _));
    DeviceInfo info;
    Net net {signer, std::move(info)};
}

TEST_CASE("Net hostname")
{
    ALLOW_CALL(Signer, signer(_, _, _));

    DeviceInfo info;
    Net net {signer, std::move(info)};
    CHECK(net.hostname() == "https://api.scorbit.io:443"); // By default it's production

    net.setHostname("production");
    CHECK(net.hostname() == "https://api.scorbit.io:443");

    net.setHostname("staging");
    CHECK(net.hostname() == "https://staging.scorbit.io:443");

    net.setHostname("");
    CHECK(net.hostname() == "https://api.scorbit.io:443");

    net.setHostname("http://localhost:8080");
    CHECK(net.hostname() == "http://localhost:8080");

    // Make sure that anything after port is thrown away
    net.setHostname("http://localhost:8080/api");
    CHECK(net.hostname() == "http://localhost:8080");

    net.setHostname("https://example.com/api");
    CHECK(net.hostname() == "https://example.com:443");
}
