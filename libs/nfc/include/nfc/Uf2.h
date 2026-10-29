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

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfc::uf2 {

constexpr size_t kBlockSize = 512;
constexpr uint32_t kMagicStart0 = 0x0A324655;
constexpr uint32_t kMagicStart1 = 0x9E5D5157;
constexpr uint32_t kMagicEnd = 0x0AB16F30;
constexpr uint32_t kFlagNotMainFlash = 0x00000001;
constexpr uint32_t kFlagFamilyIdPresent = 0x00002000;
constexpr uint32_t kRp2040FamilyId = 0xE48BFF56;
constexpr uint32_t kPayloadSize = 256;

constexpr uint32_t kFlashStart = 0x10000000;
// Sanity cap: the smallest flash RP2040 boards ship with. Tap-pad images are ~176 KiB.
constexpr size_t kMaxImageBytes = 2 * 1024 * 1024;

/** A flash image ready to write: @ref data starts at @ref address. */
struct FlashImage {
    uint32_t address {0};
    std::vector<uint8_t> data;
};

enum class Error {
    None,
    Unreadable,
    Empty,
    NotBlockAligned,
    BadMagic,
    BadPayloadSize,
    NotMainFlash,
    WrongFamily,
    BadBlockNumber,
    NotAtFlashStart,
    NotContiguous,
    TooLarge,
};

const char *errorName(Error error);

struct ParseResult {
    Error error {Error::None};
    /** Index of the offending 512-byte block, when the error is about one. */
    size_t block {0};
    FlashImage image;

    bool ok() const { return error == Error::None; }
};

/**
 * Validates a UF2 for an RP2040 tap pad and returns its flash image.
 *
 * Accepts only one contiguous image from the start of flash, in block order, whose blocks all
 * carry the RP2040 family id; anything else (another chip, a gap, a RAM or non-flash block) is
 * refused rather than guessed at, since the result is written over the pad's only firmware.
 */
ParseResult parse(const uint8_t *data, size_t size, size_t maxImageBytes = kMaxImageBytes);
ParseResult parseFile(const std::filesystem::path &path, size_t maxImageBytes = kMaxImageBytes);

} // namespace nfc::uf2
