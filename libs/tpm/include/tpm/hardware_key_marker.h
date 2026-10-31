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

#include <atomic>
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
 * waited for instead of being replaced by a soft key (a different identity). For processes that
 * build the SDK from source and link libs/tpm, such as one that selects the tap pad's TPM itself
 * and gives the SDK a signer; not part of the shared library's API. Never throws.
 *
 * On POSIX a directory is used only if it is a real directory (not a link) owned by this process's
 * effective user or root and not writable by group or others, and every directory above it (links
 * resolved) is owned by the effective user or root and not writable by group or others unless
 * sticky. A marker is used only if it is a regular file owned by the effective user or root. A
 * marker anyone else could plant, delete or move away is ignored.
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
     * $XDG_STATE_HOME/scorbit or ~/.local/state/scorbit, the user's home taken from the password
     * database when HOME is unset (then %LOCALAPPDATA%\\scorbit on Windows). The same for every
     * process of a user. Never the temp directory, which does not survive a reboot.
     */
    static std::vector<std::filesystem::path> defaultDirs();

    /**
     * Writes, fsyncs and renames the marker into the first usable directory, and syncs that
     * directory; one whose sync fails counts as unusable. A directory of this user's that group or
     * others can write to is made private (0700) first.
     */
    bool store(const HardwareKeyIdentity &identity) const;
    /// The marker in the first directory holding a valid one; untrusted or malformed are skipped.
    std::optional<HardwareKeyIdentity> load() const;
    /// Removes the marker from every usable directory.
    void clear() const;
    /**
     * Stores @p identity unless the marker already holds it and this instance has stored it
     * durably; a different one is replaced. False if it could not be stored, or an older marker
     * this process cannot replace still wins.
     */
    bool record(const HardwareKeyIdentity &identity) const;

    const std::vector<std::filesystem::path> &dirs() const { return m_dirs; }

private:
    std::vector<std::filesystem::path> m_dirs;
    /// A store by this instance completed, directory sync included.
    mutable std::atomic<bool> m_durable {false};
};
