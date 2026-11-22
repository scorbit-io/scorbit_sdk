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

#include "fs_mount_holders.h"

#include <sstream>
#include <utility>

namespace utils {
namespace detail {

MountHolders::MountHolders(MountOps ops)
    : m_ops {std::move(ops)}
{
}

MakeWritableResult MountHolders::makeWritable(const std::filesystem::path &p)
{
    // Held across the remount, so a writer arriving meanwhile waits for its outcome
    std::scoped_lock lock(m_mutex);
    const auto mountPoint = m_ops.findMountPoint(p);
    auto &holders = m_holders[mountPoint];

    if (holders > 0) {
        // Another writer here already holds it. Something outside this process may have remounted
        // it read-only since; if so, take it back rather than report it writable.
        if (m_ops.isReadOnly(mountPoint) && !m_ops.remountRW(mountPoint)) {
            return {false, mountPoint};
        }
        ++holders;
        return {true, mountPoint};
    }

    if (!m_ops.isReadOnly(mountPoint)) {
        if (m_owed.count(mountPoint) != 0) {
            // Left writable by this process's own failed remount: this writer takes it over, so
            // its release retries the remount
            holders = 1;
            return {true, mountPoint};
        }
        // Writable, but not by this process's doing: nothing to hold or to give back
        return {true, std::string {}};
    }
    if (!m_ops.remountRW(mountPoint)) {
        return {false, mountPoint};
    }
    holders = 1;
    return {true, mountPoint};
}

bool MountHolders::remountReadOnly(const std::string &mountPoint)
{
    if (mountPoint.empty()) {
        return true;
    }
    std::scoped_lock lock(m_mutex);
    const auto it = m_holders.find(mountPoint);
    if (it != m_holders.end() && it->second > 1) {
        --it->second; // another writer still needs it
        return true;
    }
    // The last holder, or one whose earlier remount failed: count from zero again either way, so
    // a retry just tries the remount once more and never underflows
    if (it != m_holders.end()) {
        m_holders.erase(it);
    }
    const auto remounted = m_ops.remountRO(mountPoint);
    if (remounted) {
        m_owed.erase(mountPoint);
    } else {
        m_owed.insert(mountPoint);
    }
    return remounted;
}

int MountHolders::holders(const std::string &mountPoint) const
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_holders.find(mountPoint);
    return it == m_holders.end() ? 0 : it->second;
}

bool MountHolders::owed(const std::string &mountPoint) const
{
    std::scoped_lock lock(m_mutex);
    return m_owed.count(mountPoint) != 0;
}

bool optionsReadOnly(const std::string &options)
{
    std::istringstream in(options);
    std::string option;
    while (std::getline(in, option, ',')) {
        if (option == "ro") {
            return true;
        }
    }
    return false;
}

} // namespace detail
} // namespace utils
