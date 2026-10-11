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

// The lock's behaviour is tested in tests/test_detail. This binary only has to keep the Tpm
// tests in it (which take the lock) off the real lock path.

#include <doctest/doctest.h>

#include <tpm/tappad_lock.h>

#if defined(SCORBIT_TAPPAD_LOCK)

#    include <filesystem>
#    include <string>
#    include <system_error>

#    include <unistd.h>

namespace {

const std::string &privateLockPath()
{
    static const std::string path =
            (std::filesystem::temp_directory_path()
             / ("scorbit-tappad-tpmtest-" + std::to_string(::getpid()) + ".lock"))
                    .string();
    return path;
}

struct Redirect {
    Redirect()
    {
        const char *paths[] = {privateLockPath().c_str()};
        hal_tappad_lock_set_paths(paths, 1);
    }
    ~Redirect()
    {
        std::error_code ec;
        std::filesystem::remove(privateLockPath(), ec);
    }
};

const Redirect redirect;

} // namespace

TEST_CASE("tap pad lock: this binary uses a private lock file")
{
    const TappadLock hold;
    REQUIRE(hold.ok());
    REQUIRE(hal_tappad_lock_path() != nullptr);
    CHECK(std::string(hal_tappad_lock_path()) == privateLockPath());
}

#else

TEST_CASE("tap pad lock: without the patch a hold is a no-op that succeeds")
{
    const TappadLock hold;
    CHECK(hold.ok());
}

#endif
