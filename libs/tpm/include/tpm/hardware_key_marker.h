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

/// The identity of a hardware (tap pad TPM) key this device has authenticated with.
struct HardwareKeyIdentity {
    uint64_t serial {0};
    std::string uuid;

    bool operator==(const HardwareKeyIdentity &) const = default;
};

/**
 * SB-6133: a durable note that this device's identity is a hardware key, so an absent tap pad is
 * waited for instead of being replaced by a soft key (a different identity). Shared by the SDK and
 * any host process that selects the tap pad's TPM itself, so both agree. Never throws.
 *
 * On POSIX a directory is used only if it is a real directory (not a link) owned by this process's
 * effective user or root and not writable by group or others; a marker only if it is a regular
 * file owned by the effective user or root. A marker anyone else could plant or delete is ignored.
 */
class HardwareKeyMarker
{
public:
    static constexpr const char *FILE_NAME = "hardware-key.json";

    /// defaultDirs().
    HardwareKeyMarker();
    /** @p dirs in order of preference, all non-volatile; the first usable one is stored in. */
    explicit HardwareKeyMarker(std::vector<std::filesystem::path> dirs);

    /**
     * The directory of the module this code is linked into (not on Windows, nor when it cannot be
     * found as an absolute path), then $XDG_STATE_HOME/scorbit or ~/.local/state/scorbit (the
     * user's home from the password database when HOME is unset; %LOCALAPPDATA%\\scorbit on
     * Windows). Never the temp directory, which does not survive a reboot.
     */
    static std::vector<std::filesystem::path> defaultDirs();

    /**
     * Writes, fsyncs and renames the marker into the first usable directory. A directory of this
     * user's that group or others can write to is made private (0700) first, unless it is the
     * module's own directory.
     */
    bool store(const HardwareKeyIdentity &identity) const;
    /// The marker in the first directory holding a valid one; untrusted or malformed are skipped.
    std::optional<HardwareKeyIdentity> load() const;
    /// Removes the marker from every usable directory.
    void clear() const;
    /// Stores @p identity unless the marker already holds it; a different one is replaced.
    bool record(const HardwareKeyIdentity &identity) const;

    const std::vector<std::filesystem::path> &dirs() const { return m_dirs; }

private:
    std::vector<std::filesystem::path> m_dirs;
};
