/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include <utils/fs_read_write.h>

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace utils {
namespace detail {

/// The system calls behind fsMakeWritable and fsRemountReadOnly, replaceable in tests.
struct MountOps {
    std::function<std::string(const std::filesystem::path &)> findMountPoint;
    std::function<bool(const std::string &)> isReadOnly;
    std::function<bool(const std::string &)> remountRW;
    std::function<bool(const std::string &)> remountRO;
};

/// Counts this process's writers per mount point, so a mount goes read-only only when the last of
/// them releases it. Without it, one writer finishing remounted read-only under another still
/// writing.
class MountHolders
{
public:
    explicit MountHolders(MountOps ops);

    /// As utils::fsMakeWritable: a non-empty mount point means this caller holds it and must hand it
    /// to remountReadOnly; an empty one means it was already writable and nothing is held.
    MakeWritableResult makeWritable(const std::filesystem::path &p);

    /// As utils::fsRemountReadOnly: remounts read-only only when the last holder releases.
    bool remountReadOnly(const std::string &mountPoint);

    /// This process's holders of @p mountPoint, for tests.
    int holders(const std::string &mountPoint) const;

private:
    MountOps m_ops;
    mutable std::mutex m_mutex;
    std::map<std::string, int> m_holders;
};

/// True when a mountinfo option list (comma separated) has the "ro" option itself; an option
/// merely containing "ro", like "errors=remount-ro", does not count.
bool optionsReadOnly(const std::string &options);

} // namespace detail
} // namespace utils
