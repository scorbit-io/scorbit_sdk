/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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

#include "updater.h"
#include "logger.h"
#include "utils/archiver.h"
#include <platform_id.h>
#include <scorbit_sdk/version.h>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/predef.h>
#include <boost/filesystem.hpp>
#include <regex>
#include <string_view>

#ifndef SCORBIT_SDK_PRODUCTION_KEY_HASH
#define SCORBIT_SDK_PRODUCTION_KEY_HASH "unknown1"
#endif

#ifndef SCORBIT_SDK_THIS_KEY_HASH
#define SCORBIT_SDK_THIS_KEY_HASH "unknown2"
#endif

namespace {

constexpr auto UPDATE_DIR = "scorbit_sdk_update";
constexpr auto LIBRARY_PATTERN = R"(^(lib)?scorbit_sdk\.(so(\.\d+)*|(\d+\.)*dylib|dll)$)";
constexpr auto URL_PATTERN = R"(^.*scorbit_sdk-((\d+\.?)+)-(\w+)\.(tar\.gz|tgz)$)";

namespace fs = boost::filesystem;
using namespace scorbit;
using namespace detail;

constexpr bool isValidPlatformId(std::string_view id)
{
    return !id.empty() && id.find("unknown") == std::string_view::npos;
}

fs::path getLibraryPath()
{
    // Get the current library path, but if it's symlink, follow it to find the real path
    return fs::canonical(boost::dll::this_line_location());
}

fs::path findFile(const fs::path &dir, const std::regex &pattern)
{
    for (const auto &entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()
            && std::regex_search(entry.path().filename().string(), pattern)) {
            return entry.path().string();
        }
    }

    return {};
}

} // namespace

namespace scorbit {
namespace detail {

Updater::Updater(NetBase &net, bool useEncryptedKey)
    : m_net {net}
    , m_useEncryptedKey {useEncryptedKey}
{
}

void Updater::checkNewVersionAndUpdate(const boost::json::object &json)
{
    if (m_updateInProgress)
        return;

    if (const auto sdk = json.if_contains("sdk")) {
        m_updateInProgress = true;
        bool ok = true;
        try {
            parseUrl(*sdk);
            if (!m_url.empty()) {
                auto tempFile = fs::temp_directory_path() / fs::unique_path();
                tempFile.replace_extension(".tar.gz");

                m_net.download(
                        [this](Error error, const std::string &filename) {
                            bool success = false;
                            if (error == Error::Success) {
                                INF("Updater: downloaded successfully: {}", filename);
                                success = update(filename);
                                if (success) {
                                    m_feedback =
                                            fmt::format("Updated successfully, ver: {}", m_version);
                                    INF("Updater: {}", m_feedback);
                                }

                                // Cleanup downloaded archive
                                boost::system::error_code ec;
                                fs::remove(filename, ec);
                                if (ec) {
                                    ERR("Updater: failed to remove temp file: {}, {}", filename,
                                        ec.message());
                                }
                            } else {
                                m_feedback = fmt::format("Updater: download failed: {}, {}",
                                                         static_cast<int>(error), filename);
                                ERR("Updater: download failed: {}", m_feedback);
                            }

                            m_updateInProgress = false;
                            m_net.sendInstalled("sdk", SCORBIT_SDK_VERSION, success, m_feedback);
                        },
                        m_url, tempFile.string());
            } else {
                INF("Cant' update the library");
                ok = false;
            }
        } catch (const std::exception &e) {
            m_feedback = fmt::format("Updater: error: {} {}", e.what(), m_feedback);
            ERR("Updater: {}", m_feedback);
            ok = false;
        }

        // Some error happened, update in progress cleared
        if (!ok) {
            m_updateInProgress = false;
            m_net.sendInstalled("sdk", SCORBIT_SDK_VERSION, false, m_feedback);
        }
    }
}

void Updater::parseUrl(const boost::json::value &sdkVal)
{
    // Make sure that platform id is correct
    if (!isValidPlatformId(SCORBIT_SDK_PLATFORM_ID)) {
        m_feedback = fmt::format("Invalid platform ID: {}", SCORBIT_SDK_PLATFORM_ID);
        ERR("Updater: {}", m_feedback);
        return;
    }

    // If using encrypted key, make sure this SDK has been built with correct production key hash
    if (m_useEncryptedKey
        && std::string {SCORBIT_SDK_THIS_KEY_HASH}
                   != std::string {SCORBIT_SDK_PRODUCTION_KEY_HASH}) {
        m_feedback = fmt::format(
                "Using encrypted key, production key hash mismatch: expected {}, found {}",
                SCORBIT_SDK_PRODUCTION_KEY_HASH, SCORBIT_SDK_THIS_KEY_HASH);
        ERR("Updater: {}", m_feedback);
        return;
    }

    m_version.clear();
    m_url.clear();
    m_feedback.clear();

    try {
        const auto sdk = sdkVal.as_object();
        m_version = sdk.at("version").as_string();
        const auto assets = sdk.at("assets_json").as_array();
        if (assets.empty()) {
            m_feedback = fmt::format("Assets list empty");
            ERR("Updater: {}", m_feedback);
            return;
        }

        // Make sure that if current version is x.y.z then it updates only by x.y.*
        const auto majorMinor =
                fmt::format("{}.{}.", SCORBIT_SDK_VERSION_MAJOR, SCORBIT_SDK_VERSION_MINOR);
        if (m_version.substr(0, majorMinor.size()) != majorMinor) {
            m_feedback = fmt::format("Version mismatch: can only update by {}x, found: {}",
                                     majorMinor, m_version);
            ERR("Updater: {}", m_feedback);
            return;
        }

        // Find the first asset with the correct platform
        for (const auto &asset : assets) {
            const auto url = asset.as_string();
            static const std::regex re(URL_PATTERN);
            std::cmatch match;
            if (!std::regex_match(url.c_str(), match, re)) {
                continue;
            }
            const auto url_version = match[1].str();
            if (url_version != m_version) {
                continue;
            }
            const auto platformId = match[3].str();
            if (platformId == SCORBIT_SDK_PLATFORM_ID) {
                m_url = url.c_str();
                return;
            }
        }
        m_feedback = fmt::format("Couldn't find update file in assets for the platform: {}",
                                 SCORBIT_SDK_PLATFORM_ID);
        ERR("Updater: {}", m_feedback);

    } catch (const std::exception &e) {
        m_feedback = fmt::format("Error parsing 'sdk': {}, in json: {}", e.what(),
                                 boost::json::serialize(sdkVal).c_str());
        ERR("Updater: {}", m_feedback);
    }
}

bool Updater::update(const std::string &archive)
{
    bool rv = false;
    const auto tempDir = fs::temp_directory_path() / UPDATE_DIR;

    try {
        const static std::regex pattern(LIBRARY_PATTERN);
        const auto libPath = getLibraryPath();
        if (libPath.empty()) {
            m_feedback = fmt::format("Library path is empty");
            ERR("Updater: {}", m_feedback);
            return false;
        }

        // Make sure that this is shared library and matches the pattern
        if (!std::regex_search(libPath.filename().string(), pattern)) {
            m_feedback =
                    fmt::format("Library path doesn't match the pattern, path: {}, pattern: {}",
                                libPath.string(), LIBRARY_PATTERN);
            ERR("Updater: {}", m_feedback);
            return false;
        }

        // update from tgz archive
        const auto archivePath = fs::path {archive};

        // Extract the archive to a temporary directory
        if (!extract(archivePath.string(), tempDir.string())) {
            m_feedback = fmt::format("Failed to extract archive: {}", archivePath.string());
            ERR("Updater: {}", m_feedback);
            return false;
        }

        const auto candidatePath = findFile(tempDir, pattern);
        if (candidatePath.empty()) {
            m_feedback = fmt::format("Library not found in the archive");
            ERR("Updater: {}", m_feedback);
            return false;
        }
        const auto newLibPath = fs::canonical(candidatePath);

        // Replace the library with the new one
        INF("Updater: replacing current library: {} by: {}", libPath.string(), newLibPath.string());
        rv = replaceLibrary(libPath.string(), newLibPath.string());

    } catch (fs::filesystem_error &e) {
        m_feedback = fmt::format("Error occurred while updating library: {}", e.what());
        ERR("Updater: {}", m_feedback);
    }

    // cleanup extract dir
    boost::system::error_code ec;
    fs::remove_all(tempDir, ec);
    if (ec) {
        ERR("Updater: error removing temp update directory: {}", ec.message());
    }
    return rv;
}

bool Updater::replaceLibrary(const std::string &libPath, const std::string &newLibPath)
{
    try {
        std::string backupPath = libPath + ".old";

#if BOOST_OS_WINDOWS
        if (fs::exists(backupPath)) {
            boost::system::error_code ec;
            fs::remove(backupPath, ec); // Try removing .old
            if (ec) {
                // .old still locked from previous session
                fs::remove(libPath); // Try removing the current one
            } else {
                // We successfully removed .old — safe to backup current
                fs::rename(libPath, backupPath);
            }
        } else if (fs::exists(libPath)) {
            // No old file blocking, just rename current
            fs::rename(libPath, backupPath);
        }

        // Finally, rename new library into place
        fs::rename(newLibPath, libPath);
        return true;

#else // POSIX systems

        if (fs::exists(libPath)) {
            fs::rename(libPath, backupPath); // Backup current lib

            try {
                fs::rename(newLibPath, libPath); // Replace with new lib
                fs::remove(backupPath);          // Remove backup on success
                return true;
            } catch (const fs::filesystem_error &e) {
                m_feedback = fmt::format("Error occurred while replacing library: {}", e.what());
                ERR("Updater: {}", m_feedback);
                fs::rename(backupPath, libPath); // Restore original
                return false;
            }
        } else {
            // Current lib doesn't exist — simple case
            fs::rename(newLibPath, libPath);
            return true;
        }
#endif
    } catch (const fs::filesystem_error &e) {
        m_feedback = fmt::format("Error occurred while replacing library: {}", e.what());
        ERR("Error occurred while replacing library: {}", e.what());
        return false;
    }
}

} // namespace detail
} // namespace scorbit
