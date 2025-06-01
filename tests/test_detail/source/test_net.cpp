/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "net.h"
#include <scorbit_sdk/scorbit_sdk.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

// Using this indirection to mock free function
struct {
    MAKE_MOCK1(signer, Signature(const Digest &digest));
} Signer;

Signature signer(const Digest &digest)
{
    return Signer.signer(digest);
}

TEST_CASE("Net constructor")
{
    ALLOW_CALL(Signer, signer(_)).RETURN(Signature {});
    DeviceInfo info;
    Net net {signer, std::move(info)};
}

TEST_CASE("Net hostname")
{
    ALLOW_CALL(Signer, signer(_)).RETURN(Signature {});

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

TEST_CASE("getSignature calls signer with correct digest")
{
    using trompeloeil::_;

    const auto uuid = "a61c63823b4b42ccbad2eb7bcd831a0d";
    const auto timestamp = "1729086480";

    /*
        import hashlib
        uuid = 'a61c63823b4b42ccbad2eb7bcd831a0d'
        timestamp_str = "1729086480"
        nonce = bytes.fromhex(uuid) + bytes(timestamp_str, 'utf-8')
        sha256 = hashlib.sha256(nonce)
        print('SHA256 hash: ' + sha256.hexdigest())
    */

    // Pre-calculated digest which is SHA256 of the concatenated UUID and timestamp using
    // python script above
    Digest expectedDigest = {0x8f, 0xcb, 0xbb, 0x3e, 0x2e, 0x1f, 0x9c, 0x3b, 0x74, 0xfc, 0x64,
                             0x3b, 0xd4, 0xb0, 0xec, 0xd5, 0xd1, 0x99, 0xb6, 0x3a, 0x65, 0xab,
                             0x98, 0xb5, 0xdf, 0x89, 0xab, 0x6b, 0xf2, 0xa6, 0xd5, 0xb7};

    // Expect the mock signer to be called with the correct digest
    REQUIRE_CALL(Signer, signer(_)).WITH(_1 == expectedDigest).RETURN(Signature{}).TIMES(1);

    // Invoke the method
    getSignature(signer, uuid, timestamp);
}
