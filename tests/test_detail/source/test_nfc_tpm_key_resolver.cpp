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

#include "nfc_tpm_key_resolver.h"
#include "device_info.h"

#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

// =============================================================================
// NfcTpmKeyResolver
// =============================================================================

#ifndef TPM_USE_HW

TEST_CASE("NfcTpmKeyResolver returns false on non-HW platform", "[NfcTpmKeyResolver]")
{
    DeviceInfo info;
    info.provider = "testprovider";

    NfcTpmKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, "1773013652"));
}

TEST_CASE("NfcTpmKeyResolver createSigner returns empty on non-HW platform", "[NfcTpmKeyResolver]")
{
    NfcTpmKeyResolver resolver;
    auto signer = resolver.createSigner();
    REQUIRE_FALSE(signer);
}

#else

TEST_CASE("NfcTpmKeyResolver tryResolve on HW platform", "[NfcTpmKeyResolver]")
{
    DeviceInfo info;
    info.provider = "testprovider";

    NfcTpmKeyResolver resolver;
    bool resolved = resolver.tryResolve(info, "1773013652");
    if (!resolved) {
        CHECK(info.uuid.empty());
        CHECK(info.serialNumber == 0);
    }
    // If resolved (actual TPM present), verify fields were populated
    if (resolved) {
        CHECK_FALSE(info.uuid.empty());
        CHECK(info.serialNumber != 0);

        auto signer = resolver.createSigner();
        REQUIRE(signer);
    }
}

#endif
