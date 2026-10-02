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
#include "../utils/archiver.h"
#include <logger/logger.h>
#include <boost/filesystem.hpp>
#include <fstream>
#include <iterator>
#include <sstream>

namespace scorbit {
namespace detail {
namespace achievements {

namespace fs = boost::filesystem;

namespace {

constexpr auto DEFINITIONS_FILE = "definitions.json";
constexpr auto FRAMES_DIR = "frames";
constexpr auto FRAMES_VERSION_FILE = "frames.version";
constexpr auto FRAMES_DOWNLOAD_FILE = "frames.zip";
constexpr auto FRAMES_STAGING_DIR = "frames.new";

// Left by older SDKs, which revalidated with an ETag
constexpr auto LEGACY_ETAG_FILE = "definitions.etag";

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

} // namespace

AchievementStorage::AchievementStorage(std::string dataDir)
    : m_root {(fs::path(dataDir) / "achievements").string()}
{
    boost::system::error_code ec;
    fs::remove(fs::path(m_root) / LEGACY_ETAG_FILE, ec);
}

std::optional<std::string> AchievementStorage::loadDefinitions() const
{
    return readFile(fs::path(m_root) / DEFINITIONS_FILE);
}

bool AchievementStorage::saveDefinitions(const std::string &body) const
{
    return writeFileAtomically(fs::path(m_root) / DEFINITIONS_FILE, body);
}

std::optional<int64_t> AchievementStorage::framesVersion() const
{
    const auto content = readFile(fs::path(m_root) / FRAMES_VERSION_FILE);
    if (!content || !fs::is_directory(fs::path(m_root) / FRAMES_DIR)) {
        return std::nullopt;
    }
    std::istringstream in(*content);
    int64_t version = 0;
    if (!(in >> version)) {
        return std::nullopt;
    }
    return version;
}

std::string AchievementStorage::framesDownloadPath() const
{
    boost::system::error_code ec;
    fs::create_directories(m_root, ec);
    return (fs::path(m_root) / FRAMES_DOWNLOAD_FILE).string();
}

bool AchievementStorage::installFrames(const std::string &archivePath, int64_t version) const
{
    const auto staging = fs::path(m_root) / FRAMES_STAGING_DIR;
    const auto installed = fs::path(m_root) / FRAMES_DIR;

    try {
        fs::remove_all(staging);
        if (!extract(archivePath, staging.string(), ArchiveTrust::Untrusted)) {
            ERR("Achievements: can't extract frame bundle {}", archivePath);
            fs::remove_all(staging);
            return false;
        }

        fs::remove_all(installed);
        fs::rename(staging, installed);
        fs::remove(archivePath);
    } catch (const fs::filesystem_error &e) {
        ERR("Achievements: can't install frame bundle: {}", e.what());
        return false;
    }

    return writeFileAtomically(fs::path(m_root) / FRAMES_VERSION_FILE, std::to_string(version));
}

std::optional<std::vector<uint8_t>> AchievementStorage::frame(const std::string &key) const
{
    // A key is a lowercase slug (§3.1); anything else can't name a bundle file
    if (!isValidTimelineName(key) || key.find('/') != std::string::npos
        || key.find("..") != std::string::npos) {
        return std::nullopt;
    }

    try {
        const auto dir = fs::path(m_root) / FRAMES_DIR;
        if (!fs::is_directory(dir)) {
            return std::nullopt;
        }
        for (const auto &entry : fs::recursive_directory_iterator(dir)) {
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

} // namespace achievements
} // namespace detail
} // namespace scorbit
