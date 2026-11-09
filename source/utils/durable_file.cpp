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

#include "durable_file.h"

#include <cstdio>
#include <system_error>

#if defined(_WIN32)
#    include <fcntl.h>
#    include <io.h>
#else
#    include <fcntl.h>
#    include <unistd.h>
#endif

namespace scorbit {
namespace detail {

namespace fs = std::filesystem;

bool writeDurably(const fs::path &file, const std::string &content)
{
    std::FILE *out = std::fopen(file.string().c_str(), "wb");
    if (!out) {
        return false;
    }
    bool ok = std::fwrite(content.data(), 1, content.size(), out) == content.size()
           && std::fflush(out) == 0;
#if defined(_WIN32)
    ok = ok && ::_commit(::_fileno(out)) == 0;
#else
    ok = ok && ::fsync(::fileno(out)) == 0;
#endif
    return std::fclose(out) == 0 && ok;
}

bool syncDirectory([[maybe_unused]] const fs::path &dir)
{
#if defined(_WIN32)
    return true;
#else
    const int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

bool syncFile(const fs::path &file)
{
#if defined(_WIN32)
    const int fd = ::_open(file.string().c_str(), _O_RDWR | _O_BINARY);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::_commit(fd) == 0;
    return ::_close(fd) == 0 && ok;
#else
    const int fd = ::open(file.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

namespace {

/**
 * Creates @p dir and any missing parents, flushing each new entry into its parent: a directory
 * created and never synced can vanish with everything written under it.
 */
bool createDirectoriesDurably(const fs::path &dir)
{
    std::error_code ec;
    if (dir.empty() || fs::is_directory(dir, ec)) {
        return true;
    }
    if (!createDirectoriesDurably(dir.parent_path())) {
        return false;
    }
    fs::create_directory(dir, ec);
    if (ec && !fs::is_directory(dir, ec)) {
        return false;
    }
    return dir.parent_path().empty() || syncDirectory(dir.parent_path());
}

} // namespace

bool replaceDurably(const fs::path &file, const std::string &content)
{
    std::error_code ec;
    if (!createDirectoriesDurably(file.parent_path())) {
        return false;
    }
    const bool existed = fs::exists(file, ec);
    auto staged = file;
    staged += ".tmp";
    if (!writeDurably(staged, content)) {
        fs::remove(staged, ec);
        return false;
    }
    fs::rename(staged, file, ec);
    if (ec) {
        fs::remove(staged, ec);
        return false;
    }
    if (syncDirectory(file.parent_path())) {
        return true;
    }
    // The rename is in place but may not survive a power cut. A new file is taken back, so that
    // what the caller is told matches what a restart finds; a replaced one can't be, as its old
    // content is gone, and is reported saved: it reads the same now and after a restart either
    // way, unless the power fails first.
    if (!existed) {
        fs::remove(file, ec);
        return false;
    }
    return true;
}

} // namespace detail
} // namespace scorbit
