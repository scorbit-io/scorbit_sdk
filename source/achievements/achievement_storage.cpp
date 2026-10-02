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

#include "achievement_storage.h"
#include "../event_ops.h"
#include <logger/logger.h>
#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace scorbit {
namespace detail {
namespace achievements {

namespace fs = boost::filesystem;

namespace {

constexpr auto DEFINITIONS_FILE = "definitions.json";
constexpr auto FRAMES_DIR = "frames";
constexpr auto FRAME_VERSIONS_FILE = "frames.json";
constexpr auto DOWNLOAD_SUFFIX = ".download";
constexpr auto DEFAULT_FRAME_EXTENSION = ".png";
constexpr size_t MAX_FRAME_EXTENSION_SIZE = 8;

// Left by older SDKs, which revalidated with an ETag and downloaded one bundle per title
constexpr auto LEGACY_ETAG_FILE = "definitions.etag";
constexpr auto LEGACY_FRAMES_VERSION_FILE = "frames.version";
constexpr auto LEGACY_FRAMES_DOWNLOAD_FILE = "frames.zip";
constexpr auto LEGACY_FRAMES_STAGING_DIR = "frames.new";

std::optional<std::string> readFile(const fs::path &path)
{
    std::ifstream in(path.string(), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/** Writes @p content next to @p path, then renames it into place, so a reader never sees half. */
bool writeFileAtomically(const fs::path &path, const std::string &content)
{
    try {
        fs::create_directories(path.parent_path());
        const auto temp = fs::path(path.string() + ".tmp");
        {
            std::ofstream out(temp.string(), std::ios::binary | std::ios::trunc);
            if (!out || !out.write(content.data(), static_cast<std::streamsize>(content.size()))) {
                ERR("Achievements: can't write {}", temp.string());
                return false;
            }
        }
        fs::rename(temp, path);
        return true;
    } catch (const fs::filesystem_error &e) {
        ERR("Achievements: can't write {}: {}", path.string(), e.what());
        return false;
    }
}

/** A key is a lowercase slug (§3.1); anything else can't name a frame file. */
bool isFrameKey(const std::string &key)
{
    return isValidTimelineName(key) && key.find('/') == std::string::npos
        && key.find('\\') == std::string::npos && key.find("..") == std::string::npos;
}

} // namespace

AchievementStorage::AchievementStorage(std::string dataDir)
    : m_root {(fs::path(dataDir) / "achievements").string()}
{
    const fs::path root {m_root};
    boost::system::error_code ec;
    fs::remove(root / LEGACY_ETAG_FILE, ec);
    fs::remove(root / LEGACY_FRAMES_DOWNLOAD_FILE, ec);
    fs::remove_all(root / LEGACY_FRAMES_STAGING_DIR, ec);
    if (fs::exists(root / LEGACY_FRAMES_VERSION_FILE, ec)) {
        // The extracted bundle shares the frames directory's name
        fs::remove_all(root / FRAMES_DIR, ec);
        fs::remove(root / LEGACY_FRAMES_VERSION_FILE, ec);
    }
}

std::optional<std::string> AchievementStorage::loadDefinitions() const
{
    return readFile(fs::path(m_root) / DEFINITIONS_FILE);
}

bool AchievementStorage::saveDefinitions(const std::string &body) const
{
    return writeFileAtomically(fs::path(m_root) / DEFINITIONS_FILE, body);
}

std::map<std::string, int64_t> AchievementStorage::frameVersions() const
{
    std::map<std::string, int64_t> versions;
    const auto content = readFile(fs::path(m_root) / FRAME_VERSIONS_FILE);
    if (!content) {
        return versions;
    }

    try {
        const auto document = nlohmann::json::parse(*content);
        for (const auto &[key, version] : document.items()) {
            if (version.is_number_integer()) {
                versions.emplace(key, version.get<int64_t>());
            }
        }
    } catch (const std::exception &e) {
        // Every frame is downloaded again, which heals it
        WRN("Achievements: ignoring malformed {}: {}", FRAME_VERSIONS_FILE, e.what());
        versions.clear();
    }
    return versions;
}

bool AchievementStorage::saveFrameVersions(const std::map<std::string, int64_t> &versions) const
{
    return writeFileAtomically(fs::path(m_root) / FRAME_VERSIONS_FILE,
                               nlohmann::json(versions).dump());
}

std::string AchievementStorage::frameDownloadPath(const std::string &key) const
{
    boost::system::error_code ec;
    fs::create_directories(m_root, ec);
    return (fs::path(m_root) / (key + DOWNLOAD_SUFFIX)).string();
}

void AchievementStorage::removeFrameFiles(const std::string &key) const
{
    const auto dir = fs::path(m_root) / FRAMES_DIR;
    boost::system::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return;
    }
    std::vector<fs::path> stale;
    for (fs::directory_iterator it {dir, ec}, end; !ec && it != end; it.increment(ec)) {
        if (it->path().stem() == key) {
            stale.push_back(it->path());
        }
    }
    for (const auto &path : stale) {
        fs::remove(path, ec);
    }
}

bool AchievementStorage::installFrame(const std::string &key, const std::string &downloadedPath,
                                      const std::string &extension, int64_t version) const
{
    if (!isFrameKey(key)) {
        ERR("Achievements: can't install a frame for '{}'", key);
        return false;
    }

    try {
        const auto dir = fs::path(m_root) / FRAMES_DIR;
        fs::create_directories(dir);
        // An extension change would otherwise leave the previous file to shadow the new one
        removeFrameFiles(key);
        fs::rename(downloadedPath, dir / (key + extension));
    } catch (const fs::filesystem_error &e) {
        ERR("Achievements: can't install frame for '{}': {}", key, e.what());
        return false;
    }

    auto versions = frameVersions();
    versions[key] = version;
    return saveFrameVersions(versions);
}

bool AchievementStorage::removeFrame(const std::string &key) const
{
    auto versions = frameVersions();
    if (isFrameKey(key)) {
        removeFrameFiles(key);
    }
    if (versions.erase(key) == 0) {
        return true;
    }
    return saveFrameVersions(versions);
}

std::optional<std::vector<uint8_t>> AchievementStorage::frame(const std::string &key) const
{
    if (!isFrameKey(key)) {
        return std::nullopt;
    }

    try {
        const auto dir = fs::path(m_root) / FRAMES_DIR;
        if (!fs::is_directory(dir)) {
            return std::nullopt;
        }
        for (const auto &entry : fs::directory_iterator(dir)) {
            if (fs::is_regular_file(entry.path()) && entry.path().stem() == key) {
                auto content = readFile(entry.path());
                if (!content) {
                    return std::nullopt;
                }
                return std::vector<uint8_t>(content->begin(), content->end());
            }
        }
    } catch (const fs::filesystem_error &e) {
        ERR("Achievements: can't read frame for '{}': {}", key, e.what());
    }
    return std::nullopt;
}

std::string frameExtension(std::string_view url)
{
    const auto end = url.find_first_of("?#");
    const auto path = url.substr(0, end);
    const auto slash = path.rfind('/');
    const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return DEFAULT_FRAME_EXTENSION;
    }

    const auto extension = name.substr(dot + 1);
    if (extension.empty() || extension.size() > MAX_FRAME_EXTENSION_SIZE
        || !std::ranges::all_of(extension, [](unsigned char c) { return std::isalnum(c) != 0; })) {
        return DEFAULT_FRAME_EXTENSION;
    }

    std::string result {"."};
    for (const unsigned char c : extension) {
        result += static_cast<char>(std::tolower(c));
    }
    return result;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
