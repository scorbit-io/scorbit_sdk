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

#include "nfc/Uf2.h"

#include <fstream>
#include <system_error>

namespace nfc::uf2 {

namespace {

uint32_t le32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

ParseResult failure(Error error, size_t block = 0)
{
    ParseResult result;
    result.error = error;
    result.block = block;
    return result;
}

} // namespace

const char *errorName(Error error)
{
    switch (error) {
    case Error::None:
        return "none";
    case Error::Unreadable:
        return "unreadable";
    case Error::Empty:
        return "empty";
    case Error::NotBlockAligned:
        return "not_block_aligned";
    case Error::BadMagic:
        return "bad_magic";
    case Error::BadPayloadSize:
        return "bad_payload_size";
    case Error::NotMainFlash:
        return "not_main_flash";
    case Error::WrongFamily:
        return "wrong_family";
    case Error::BadBlockNumber:
        return "bad_block_number";
    case Error::NotAtFlashStart:
        return "not_at_flash_start";
    case Error::NotContiguous:
        return "not_contiguous";
    case Error::TooLarge:
        return "too_large";
    }
    return "unknown";
}

ParseResult parse(const uint8_t *data, size_t size, size_t maxImageBytes)
{
    if (!data || size == 0) {
        return failure(Error::Empty);
    }
    if (size % kBlockSize != 0) {
        return failure(Error::NotBlockAligned);
    }
    const size_t blocks = size / kBlockSize;
    if (blocks > maxImageBytes / kPayloadSize) {
        return failure(Error::TooLarge);
    }

    ParseResult result;
    result.image.address = kFlashStart;
    result.image.data.reserve(blocks * kPayloadSize);
    for (size_t i = 0; i < blocks; ++i) {
        const uint8_t *block = data + i * kBlockSize;
        if (le32(block) != kMagicStart0 || le32(block + 4) != kMagicStart1
            || le32(block + kBlockSize - 4) != kMagicEnd) {
            return failure(Error::BadMagic, i);
        }
        const uint32_t flags = le32(block + 8);
        const uint32_t target = le32(block + 12);
        const uint32_t payloadSize = le32(block + 16);
        const uint32_t blockNo = le32(block + 20);
        const uint32_t numBlocks = le32(block + 24);
        const uint32_t familyId = le32(block + 28);

        if (flags & kFlagNotMainFlash) {
            return failure(Error::NotMainFlash, i);
        }
        if (!(flags & kFlagFamilyIdPresent) || familyId != kRp2040FamilyId) {
            return failure(Error::WrongFamily, i);
        }
        if (payloadSize != kPayloadSize) {
            return failure(Error::BadPayloadSize, i);
        }
        if (blockNo != i || numBlocks != blocks) {
            return failure(Error::BadBlockNumber, i);
        }
        if (target != kFlashStart + i * kPayloadSize) {
            return failure(i == 0 ? Error::NotAtFlashStart : Error::NotContiguous, i);
        }
        result.image.data.insert(result.image.data.end(), block + 32, block + 32 + kPayloadSize);
    }
    return result;
}

ParseResult parseFile(const std::filesystem::path &path, size_t maxImageBytes)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return failure(Error::Unreadable);
    }
    // Refuse before reading, so a wrong path to a large file costs nothing.
    if (size / kBlockSize > maxImageBytes / kPayloadSize) {
        return failure(Error::TooLarge);
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!in || !in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()))) {
        return failure(Error::Unreadable);
    }
    return parse(bytes.data(), bytes.size(), maxImageBytes);
}

} // namespace nfc::uf2
