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

#include <filesystem>
#include <string>

namespace utils {

struct MakeWritableResult {
    bool ok;
    std::string mountPoint;
};

/// Makes the mount holding @p p writable. A non-empty mountPoint means this caller now holds the
/// mount: pass it to fsRemountReadOnly exactly once when done. Writers in one process share a
/// mount, and it goes read-only only after the last of them releases it.
MakeWritableResult fsMakeWritable(const std::filesystem::path &p);
/// Releases a mount point returned by fsMakeWritable, remounting read-only when it was the last
/// holder; an empty @p mp (nothing was held) is a no-op. False when the remount failed: the next
/// writer's release retries it. Only release what fsMakeWritable returned, once: a release with
/// no matching fsMakeWritable would pull the mount from under another writer. To retry a failed
/// remount, pair them: release what fsMakeWritable returns.
bool fsRemountReadOnly(const std::string &mp);

} // namespace utils
