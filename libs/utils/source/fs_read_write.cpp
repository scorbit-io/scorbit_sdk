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

#include <utils/fs_read_write.h>

#include <vector>
#include <sstream>
#include <fstream>

#ifdef __linux__
#    include <sys/mount.h>
#endif

namespace fs = std::filesystem;

namespace {

struct MountInfo {
    std::string mountPoint; // e.g. "/"
    bool readOnly = false;
};

#ifdef __linux__

std::vector<MountInfo> parseMountInfo()
{
    std::ifstream in("/proc/self/mountinfo");
    if (!in)
        throw std::runtime_error("Cannot open /proc/self/mountinfo");

    std::vector<MountInfo> out;
    std::string line;
    while (std::getline(in, line)) {
        // Format: [see man 5 proc], fields separated by spaces
        // sample: 39 25 8:1 / / rw,relatime shared:1 - ext4 /dev/sda1 rw
        auto parts = std::stringstream(line);
        std::string discard;

        // skip to mount point (5th field)
        for (int i = 0; i < 4; ++i)
            parts >> discard;
        std::string mountPoint;
        parts >> mountPoint;

        // skip to mount options (after filesystem root)
        // find the "-" separator
        std::string token;
        while (parts >> token && token != "-") { }

        // filesystem type
        std::string fstype;
        parts >> fstype;

        // source
        parts >> discard;

        // superblock options
        std::string options;
        parts >> options;

        bool ro = options.find("ro") != std::string::npos;

        out.push_back({mountPoint, ro});
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

MakeWritableResult fsMakeWritable(const fs::path &p)
{
    (void)p;
    std::string mp;

#ifdef __linux__
    mp = findMountPoint(p);

    if (!isReadOnly(mp)) {
        return {true, std::string {}}; // already writable
    }

    if (!remountRW(mp)) {
        return {false, mp};
    }
#endif

    return {true, mp};
}

void fsRemountReadOnly(const std::string &mp)
{
    if (!mp.empty()) {
        remountRO(mp);
    }
}

} // namespace utils
