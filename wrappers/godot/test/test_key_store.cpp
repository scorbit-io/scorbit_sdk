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

// The device key store, without Godot: content round-trips, and on POSIX the key is owner-only.

#include "../src/key_store.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#    include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using scorbit_godot::loadKey;
using scorbit_godot::saveKey;

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "test_key_store FAILED: %s\n", what);
    }
}

#ifndef _WIN32
fs::perms modeOf(const fs::path &path)
{
    return fs::status(path).permissions() & fs::perms::mask;
}
#endif

} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / "scorbit_godot_test_key_store";
    std::error_code ec;
    fs::remove_all(root, ec);
#ifndef _WIN32
    ::umask(022); // the common default, under which plain file creation is 0644
#endif

    const auto dir = root / "scorbit";
    const auto file = dir / "device_key";

    check(!loadKey(file.string()).has_value(), "no key before the first save");
    check(saveKey(file.string(), "first-key"), "saving into a new directory");
    check(loadKey(file.string()).value_or("") == "first-key", "the saved key loads back");
#ifndef _WIN32
    check(modeOf(dir) == fs::perms::owner_all, "a new key directory is 0700");
    check(modeOf(file) == (fs::perms::owner_read | fs::perms::owner_write), "a new key is 0600");

    // A key file that another account can read is replaced with an owner-only one.
    fs::permissions(file,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read
                            | fs::perms::others_read,
                    fs::perm_options::replace);
#endif
    check(saveKey(file.string(), "second-key"), "replacing an existing key");
    check(loadKey(file.string()).value_or("") == "second-key", "the replaced key loads back");
#ifndef _WIN32
    check(modeOf(file) == (fs::perms::owner_read | fs::perms::owner_write),
          "a replaced key is 0600");
#endif
    check(!fs::exists(file.string() + ".tmp"), "no temporary file is left behind");
    check(!saveKey("", "key"), "an empty path saves nothing");

    fs::remove_all(root, ec);
    std::printf("test_key_store: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
