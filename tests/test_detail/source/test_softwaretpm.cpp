/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

// SB-6128: a host that falls back to SoftwareTpm with no key file got an uninitialised serial
// beside an empty uuid. With no key file there is no identity at all.

#include <catch2/catch_test_macros.hpp>

#include <tpm/softwaretpm.h>
#include <utils/bytearray.h>

#include <filesystem>

using utils::ByteArray;

namespace {

// The key pair libs/tpm's own tests provision with.
ByteArray signKey()
{
    return ByteArray {"4111A6D1EECD017A7321043EAB912320C82FD2721E2EB5B69217ADF24172DE8C"};
}

ByteArray verifyPublicKey()
{
    return ByteArray {"048DB4F344FE0D50BDB4AF918F2A3F12E82DEED8471157C9758B174BEC8FCA68F1"
                      "D859FD7DF3C4E36C69D292361FFDF44C8A2105507D9D224B7F9CB742282F9A8B"};
}

std::filesystem::path testDir()
{
    auto dir = std::filesystem::temp_directory_path() / "scorbit_softwaretpm_sb6128";
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("SoftwareTpm: no key file means serial 0 and no uuid", "[tpm][softwaretpm]")
{
    const auto missing = (testDir() / "missing.json").string();
    std::filesystem::remove(missing);

    SoftwareTpm tpm(missing, verifyPublicKey());

    CHECK_FALSE(tpm.isValid());
    CHECK(tpm.serial() == 0);
    CHECK(tpm.uuid().empty());
}

TEST_CASE("SoftwareTpm: re-reading a missing key file drops the identity it held",
          "[tpm][softwaretpm]")
{
    const auto dir = testDir();
    const auto keyFile = (dir / "provisioned.json").string();
    const auto missing = (dir / "missing.json").string();
    std::filesystem::remove(missing);

    SoftwareTpm tpm(missing, verifyPublicKey());
    ByteArray uuid(16);
    for (size_t i = 0; i < uuid.size(); ++i) {
        uuid[i] = static_cast<uint8_t>(i + 1);
    }
    REQUIRE(tpm.startProvisioning(keyFile, 12345, uuid, signKey()));
    REQUIRE(tpm.serial() == 12345);

    tpm.readData(missing);

    CHECK_FALSE(tpm.isValid());
    CHECK(tpm.serial() == 0);
    CHECK(tpm.uuid().empty());
    std::filesystem::remove_all(dir);
}
