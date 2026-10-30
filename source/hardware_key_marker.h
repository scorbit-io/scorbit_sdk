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

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {

/// The identity of a hardware (tap pad TPM) key this device has authenticated with.
struct HardwareKeyIdentity {
    uint64_t serial {0};
    std::string uuid;

    bool operator==(const HardwareKeyIdentity &) const = default;
};

/**
 * SB-6133: a durable note that this device's identity is a hardware key, so an absent tap pad is
 * waited for instead of being replaced by a soft key (a different identity). Never throws.
 */
class HardwareKeyMarker
{
public:
    static constexpr const char *FILE_NAME = "hardware-key.json";

    /// defaultDirs().
    HardwareKeyMarker();
    /** @p dirs in order of preference; the first that can be written is stored in. */
    explicit HardwareKeyMarker(std::vector<std::filesystem::path> dirs);

    /**
     * The directory of the loaded SDK library (not on Windows, nor when it cannot be found), then
     * nfc_flash::RecordStore::defaultDirs().
     */
    static std::vector<std::filesystem::path> defaultDirs();

    /// Writes, fsyncs and renames the marker into the first writable directory.
    bool store(const HardwareKeyIdentity &identity) const;
    /// The marker in the first directory holding a valid one; unreadable or malformed are skipped.
    std::optional<HardwareKeyIdentity> load() const;
    /// Removes the marker from every directory.
    void clear() const;
    /// Stores @p identity unless the marker already holds it; a different one is replaced.
    bool record(const HardwareKeyIdentity &identity) const;

    const std::vector<std::filesystem::path> &dirs() const { return m_dirs; }

private:
    std::vector<std::filesystem::path> m_dirs;
};

} // namespace detail
} // namespace scorbit
