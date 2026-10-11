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

#include "tappad_lock_test_path.h"

#include <tpm/tappad_lock.h>

#include <filesystem>
#include <system_error>

#if defined(SCORBIT_TAPPAD_LOCK)
#    include <unistd.h>
#endif

namespace tappad_lock_test {

std::string privateLockPath()
{
#if defined(SCORBIT_TAPPAD_LOCK)
    static const std::string path =
            (std::filesystem::temp_directory_path()
             / ("scorbit-tappad-test-" + std::to_string(::getpid()) + ".lock"))
                    .string();
    return path;
#else
    return {};
#endif
}

void usePrivateLockPath()
{
#if defined(SCORBIT_TAPPAD_LOCK)
    const std::string path = privateLockPath();
    const char *paths[] = {path.c_str()};
    hal_tappad_lock_set_paths(paths, 1);
#endif
}

namespace {

struct Redirect {
    Redirect() { usePrivateLockPath(); }
    ~Redirect()
    {
        std::error_code ec;
        std::filesystem::remove(privateLockPath(), ec);
    }
};

const Redirect redirect;

} // namespace

} // namespace tappad_lock_test
