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
#include <logger/logger.h>
#include <boost/filesystem.hpp>
#include <fstream>
#include <iterator>

namespace scorbit {
namespace detail {
namespace achievements {

namespace fs = boost::filesystem;

namespace {

constexpr auto DEFINITIONS_FILE = "definitions.json";
constexpr auto DEFINITIONS_ETAG_FILE = "definitions.etag";

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
}

std::optional<CachedDefinitions> AchievementStorage::loadDefinitions() const
{
    auto body = readFile(fs::path(m_root) / DEFINITIONS_FILE);
    if (!body) {
        return std::nullopt;
    }
    return CachedDefinitions {std::move(*body),
                              readFile(fs::path(m_root) / DEFINITIONS_ETAG_FILE).value_or("")};
}

bool AchievementStorage::saveDefinitions(const CachedDefinitions &definitions) const
{
    // The ETag goes first and comes back last: an interrupted save loses the validator, which
    // costs one full download, but never pairs it with a body it does not describe
    const fs::path root {m_root};
    boost::system::error_code ec;
    fs::remove(root / DEFINITIONS_ETAG_FILE, ec);
    if (!writeFileAtomically(root / DEFINITIONS_FILE, definitions.body)) {
        return false;
    }
    return definitions.etag.empty()
        || writeFileAtomically(root / DEFINITIONS_ETAG_FILE, definitions.etag);
}

void AchievementStorage::discardDefinitions() const
{
    const fs::path root {m_root};
    boost::system::error_code ec;
    fs::remove(root / DEFINITIONS_ETAG_FILE, ec);
    fs::remove(root / DEFINITIONS_FILE, ec);
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
