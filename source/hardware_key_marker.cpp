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

#include "hardware_key_marker.h"

#include "identifiers.h"
#include "nfc_flash.h"

#include <logger/logger.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#    include <io.h>
#    include <process.h>
#else
#    include <dlfcn.h>
#    include <fcntl.h>
#    include <unistd.h>
#endif

namespace scorbit {
namespace detail {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::uintmax_t MAX_MARKER_SIZE = 4096;

std::string uniqueSuffix()
{
    static std::atomic<unsigned> writes {0};
#if defined(_WIN32)
    const long pid = long(::_getpid());
#else
    const long pid = long(::getpid());
#endif
    return "." + std::to_string(pid) + "." + std::to_string(++writes) + ".tmp";
}

// The library directory may be writable by every user, so the staged file is created exclusively
// and never through a link someone planted under its name.
bool writeDurably(const fs::path &file, const std::string &content)
{
#if defined(_WIN32)
    std::FILE *out = std::fopen(file.string().c_str(), "wb"); // Windows: a private dir only
    if (!out) {
        return false;
    }
    bool ok = std::fwrite(content.data(), 1, content.size(), out) == content.size()
           && std::fflush(out) == 0 && ::_commit(::_fileno(out)) == 0;
    return std::fclose(out) == 0 && ok;
#else
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = ::write(fd, content.data(), content.size()) == ssize_t(content.size())
           && ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

bool syncDirectory([[maybe_unused]] const fs::path &dir)
{
#if defined(_WIN32)
    return true;
#else
    const int fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

std::optional<fs::path> libraryDirectory()
{
#if defined(_WIN32)
    return std::nullopt;
#else
    static const char anchor = 0; // any address inside this library will do
    Dl_info info {};
    if (::dladdr(&anchor, &info) == 0 || !info.dli_fname || !*info.dli_fname) {
        return std::nullopt;
    }
    std::error_code ec;
    const auto dir = fs::absolute(fs::path(info.dli_fname), ec).parent_path();
    if (ec || dir.empty()) {
        return std::nullopt;
    }
    return dir;
#endif
}

std::optional<HardwareKeyIdentity> readMarker(const fs::path &file)
{
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(file, ec))) {
        return std::nullopt;
    }
    try {
        const auto size = fs::file_size(file, ec);
        if (ec || size > MAX_MARKER_SIZE) {
            WRN("Hardware-key marker {} is unreadable, ignoring it", file.string());
            return std::nullopt;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in.is_open()) {
            WRN("Hardware-key marker {} is unreadable, ignoring it", file.string());
            return std::nullopt;
        }
        const std::string body {std::istreambuf_iterator<char>(in), {}};
        const auto j = json::parse(body);
        HardwareKeyIdentity identity {j.at(JKEY_HWKEY_SERIAL).get<uint64_t>(),
                                      j.at(JKEY_HWKEY_UUID).get<std::string>()};
        if (identity.uuid.empty()) {
            WRN("Hardware-key marker {} has no uuid, ignoring it", file.string());
            return std::nullopt;
        }
        return identity;
    } catch (const std::exception &e) {
        WRN("Hardware-key marker {} is malformed, ignoring it: {}", file.string(), e.what());
        return std::nullopt;
    }
}

} // namespace

HardwareKeyMarker::HardwareKeyMarker()
    : HardwareKeyMarker(defaultDirs())
{
}

HardwareKeyMarker::HardwareKeyMarker(std::vector<fs::path> dirs)
    : m_dirs {std::move(dirs)}
{
}

std::vector<fs::path> HardwareKeyMarker::defaultDirs()
{
    std::vector<fs::path> dirs;
    try {
        if (auto lib = libraryDirectory()) {
            dirs.push_back(std::move(*lib));
        }
        for (auto &dir : nfc_flash::RecordStore::defaultDirs()) {
            dirs.push_back(std::move(dir));
        }
    } catch (const std::exception &e) {
        WRN("Hardware-key marker: can't work out its directories: {}", e.what());
    }
    return dirs;
}

bool HardwareKeyMarker::store(const HardwareKeyIdentity &identity) const
{
    try {
        const auto body = json {
                {JKEY_HWKEY_SERIAL, identity.serial},
                {JKEY_HWKEY_UUID,
                 identity.uuid}}.dump();
        for (const auto &dir : m_dirs) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            const auto target = dir / FILE_NAME;
            auto staged = target;
            staged += uniqueSuffix();
            if (!writeDurably(staged, body)) {
                fs::remove(staged, ec);
                continue;
            }
            fs::rename(staged, target, ec);
            if (ec) {
                fs::remove(staged, ec);
                continue;
            }
            if (!syncDirectory(dir)) {
                WRN("Hardware-key marker: could not sync {}", dir.string());
            }
            return true;
        }
    } catch (const std::exception &e) {
        ERR("Hardware-key marker: store failed: {}", e.what());
        return false;
    }
    ERR("Hardware-key marker: no writable directory to store it in");
    return false;
}

std::optional<HardwareKeyIdentity> HardwareKeyMarker::load() const
{
    for (const auto &dir : m_dirs) {
        if (auto identity = readMarker(dir / FILE_NAME)) {
            return identity;
        }
    }
    return std::nullopt;
}

void HardwareKeyMarker::clear() const
{
    for (const auto &dir : m_dirs) {
        std::error_code ec;
        fs::remove(dir / FILE_NAME, ec);
    }
}

bool HardwareKeyMarker::record(const HardwareKeyIdentity &identity) const
{
    const auto existing = load();
    if (existing == identity) {
        return true;
    }
    if (existing) {
        INF("Hardware key changed (serial {} -> {}); updating the hardware-key marker",
            existing->serial, identity.serial);
    }
    return store(identity);
}

} // namespace detail
} // namespace scorbit
