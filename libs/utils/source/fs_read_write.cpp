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

#include <utils/fs_read_write.h>
#include "fs_mount_holders.h"

#include <vector>
#include <sstream>
#include <fstream>

#ifdef __linux__
#    include <sys/mount.h>
#endif

namespace fs = std::filesystem;

namespace {

#ifdef __linux__

using MountInfo = utils::detail::MountInfoLine;

std::vector<MountInfo> parseMountInfo()
{
    std::ifstream in("/proc/self/mountinfo");
    if (!in)
        throw std::runtime_error("Cannot open /proc/self/mountinfo");

    std::vector<MountInfo> out;
    std::string line;
    while (std::getline(in, line)) {
        out.push_back(utils::detail::parseMountInfoLine(line));
    }
    return out;
}

static std::string findMountPoint(const fs::path &p)
{
    const auto abs = fs::canonical(p);
    const auto mounts = parseMountInfo();

    std::string bestMatch;
    size_t bestLen = 0;

    for (const auto &m : mounts) {
        if (abs.string().rfind(m.mountPoint, 0) == 0) { // starts_with
            if (m.mountPoint.size() > bestLen) {
                bestLen = m.mountPoint.size();
                bestMatch = m.mountPoint;
            }
        }
    }

    if (bestMatch.empty())
        throw std::runtime_error("No mount point found for " + abs.string());

    return bestMatch;
}

bool isReadOnly(const std::string &mountPoint)
{
    const auto mounts = parseMountInfo();
    for (const auto &m : mounts) {
        if (m.mountPoint == mountPoint)
            return m.readOnly;
    }
    throw std::runtime_error("Mountpoint not found: " + mountPoint);
}

bool remountRW(const std::string &mountPoint)
{
    unsigned long flags = MS_REMOUNT;
    // Let kernel reuse its existing mount flags except read-only
    if (mount(nullptr, mountPoint.c_str(), nullptr, flags, nullptr) == 0) {
        return true;
    }
    return false;
}

bool remountRO(const std::string &mountPoint)
{
    unsigned long flags = MS_REMOUNT | MS_RDONLY;
    if (mount(nullptr, mountPoint.c_str(), nullptr, flags, nullptr) == 0) {
        return true;
    }
    return false;
}

#else // NON-LINUX (macOS, Windows, etc.)

bool remountRO(const std::string &)
{
    return false;
}

#endif

} // namespace

namespace utils {

#ifdef __linux__

namespace {

// One per process: every writer's remounts go through it
detail::MountHolders &mountHolders()
{
    static detail::MountHolders holders {detail::MountOps {
            [](const fs::path &p) { return findMountPoint(p); },
            [](const std::string &mp) { return isReadOnly(mp); },
            [](const std::string &mp) { return remountRW(mp); },
            [](const std::string &mp) { return remountRO(mp); },
    }};
    return holders;
}

} // namespace

MakeWritableResult fsMakeWritable(const fs::path &p)
{
    return mountHolders().makeWritable(p);
}

bool fsRemountReadOnly(const std::string &mp)
{
    return mountHolders().remountReadOnly(mp);
}

#else // NON-LINUX: nothing is remounted

MakeWritableResult fsMakeWritable(const fs::path &)
{
    return {true, std::string {}};
}

bool fsRemountReadOnly(const std::string &mp)
{
    return mp.empty() || remountRO(mp);
}

#endif

} // namespace utils
