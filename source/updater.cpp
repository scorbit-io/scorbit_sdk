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
#include <logger/logger.h>
#include "utils/archiver.h"
#include <platform_id.h>
#include <utils/fs_read_write.h>
#include <scorbit_sdk/version.h>

#include <fmt/chrono.h>
#include <nlohmann/json.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/predef.h>
#include <regex>

#ifndef SCORBIT_SDK_PRODUCTION_KEY_HASH
#    define SCORBIT_SDK_PRODUCTION_KEY_HASH "unknown1"
#endif

#ifndef SCORBIT_SDK_THIS_KEY_HASH
#    define SCORBIT_SDK_THIS_KEY_HASH "unknown2"
#endif

namespace {

constexpr auto UPDATE_DIR = "scorbit_update";
constexpr auto SCORBITD_PATTERN = R"(^scorbitd(\.exe)?$)";
constexpr auto SDK_LIBRARY_PATTERN = R"(^(lib)?scorbit_sdk\.(so(\.\d+)*|(\d+\.)*dylib|dll)$)";
constexpr auto SDK_URL_PATTERN = R"(^.*scorbit_sdk-((\d+\.?)+)-(\w+)\.(tar\.gz|tgz)$)";
constexpr auto SCORBITD_NAME_PATTERN = R"(^scorbitd-((\d+\.?)+)-(\w+)\.(tar\.gz|tgz)$)";

namespace fs = boost::filesystem;
using namespace scorbit;
using namespace detail;

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

Updater::Updater(NetBase &net, bool useEncryptedKey, const std::string &scorbitdVersion,
                 const std::string &scorbitdPlatformId)
    : m_net {net}
    , m_useEncryptedKey {useEncryptedKey}
    , m_scorbitdVersion {scorbitdVersion}
    , m_scorbitdPlatformId {scorbitdPlatformId}
{
}

fs::path Updater::getSdkLibraryPath() const
{
    // Get the current library path, but if it's symlink, follow it to find the real path
    return fs::canonical(boost::dll::this_line_location());
}

fs::path Updater::getProcessExecutablePath() const
{
    // Get the current process binary path, but if it's symlink, follow it to find the real path
    return fs::canonical(boost::dll::program_location());
}

void Updater::checkNewVersionAndUpdate(const nlohmann::json &json,
                                       std::shared_ptr<EventManager> eventManager)
{
    if (m_updateInProgress)
        return;

    m_updateInProgress = true;
    m_feedback.clear();

    std::string logs;
    bool success = false;

    // Check for SDK update
    if (const auto it = json.find("sdk"); it != json.end() && it->is_object()) {
        const auto urlInfo = parseUrls(*it);
        const BinaryInfo binaryInfo {getSdkLibraryPath(), std::regex {SDK_LIBRARY_PATTERN}};

        if (canUpdateSdk(urlInfo, binaryInfo)) {
            INF("Updater: trying to update SDK to version: {}", urlInfo.version);
            success = tryToRemountAndUpdate(urlInfo, binaryInfo);
        }

        if (!m_feedback.empty() || success) {
            const auto timestamp = std::chrono::system_clock::now();
            logs.append(fmt::format("----- SDK ----- {:%Y-%m-%d %H:%M:%S}\n\n{}", timestamp,
                                    m_feedback));
        }
    }

    m_feedback.clear();

    // Check for Scorbitd update
    if (const auto it = json.find("scorbitd"); it != json.end() && it->is_object()) {
        const auto urlInfo = parseUrls(*it);
        const BinaryInfo binaryInfo {getProcessExecutablePath(), std::regex {SCORBITD_PATTERN}};

        if (canUpdateScorbitd(urlInfo, binaryInfo)) {
            INF("Updater: trying to update Scorbitd to version: {}", urlInfo.version);
            success = tryToRemountAndUpdate(urlInfo, binaryInfo);

            if (success && eventManager) {
                eventManager->push(std::make_shared<ScorbitdUpdatedEvent>(
                        urlInfo.version, binaryInfo.path.string()));
            }
        }

        if (!m_feedback.empty() || success) {
            const auto timestamp = std::chrono::system_clock::now();
            logs.append(fmt::format("----- scorbitd ----- {:%Y-%m-%d %H:%M:%S}\n\n{}", timestamp,
                                    m_feedback));
        }
    }

    if (!logs.empty()) {
        m_net.updateConfig("sdk", SCORBIT_SDK_VERSION, success, logs);
    }

    m_updateInProgress = false;
}

Updater::UrlInfo Updater::parseUrls(const nlohmann::json &obj) const
{
    UrlInfo info;

    try {
        info.version = obj["version"].get<std::string>();
        const auto assets = obj["assets_json"];

        // Find the first asset with the correct platform
        for (const auto &asset : assets) {
            // SDK is just a URL string
            if (asset.is_string()) {
                std::string url = asset.get<std::string>();
                static const std::regex re(SDK_URL_PATTERN);
                std::smatch match;
                if (!std::regex_match(url, match, re)) {
                    continue;
                }
                const auto url_version = match[1].str();
                if (url_version != info.version) {
                    continue;
                }
                const auto platformId = match[3].str();
                if (platformId == SCORBIT_SDK_PLATFORM_ID) {
                    info.url = url;
                    return info;
                }
            } else if (asset.is_object()) {
                // Scorbitd is an object with name, url, download_url, content_type, size
                const auto name = asset["name"].get<std::string>();
                static const std::regex re(SCORBITD_NAME_PATTERN);
                std::smatch match;
                if (!std::regex_match(name, match, re)) {
                    continue;
                }
                const auto url_version = match[1].str();
                if (url_version != info.version) {
                    continue;
                }

                const auto platformId = match[3].str();
                // To transition between u20 <=> u18
                const auto u18Equivalent =
                        m_scorbitdPlatformId.substr(0, m_scorbitdPlatformId.size() - 3) + "u18";
                const auto u20Equivalent =
                        m_scorbitdPlatformId.substr(0, m_scorbitdPlatformId.size() - 3) + "u20";
                if (platformId == m_scorbitdPlatformId || platformId == u18Equivalent
                    || platformId == u20Equivalent) {
                    asset["download_url"].get_to(info.url);
                    asset["content_type"].get_to(info.contentType);
                    asset["size"].get_to(info.size);
                    return info;
                }
            }
        }
    } catch (const std::exception &e) {
        const auto msg = fmt::format("Error parsing: {}, in json: {}", e.what(), obj.dump());
        feedback(msg);
        ERR("Updater: {}", msg);
    }

    return info;
}

bool Updater::update(const std::string &archive, const BinaryInfo &binaryInfo) const
{
    bool rv = false;
    const auto tempDir = binaryInfo.path.parent_path() / UPDATE_DIR;

    // RAII cleanup guard
    auto cleanup = [&tempDir]() {
        boost::system::error_code ec;
        fs::remove_all(tempDir, ec);
        if (ec) {
            ERR("Updater: error removing temp update directory: {}", ec.message());
        }
    };
    std::shared_ptr<void> guard(nullptr, [&](void *) { cleanup(); });

    try {
        // update from tgz archive
        const auto archivePath = fs::path {archive};

        // Extract the archive to a temporary directory
        if (!extract(archivePath.string(), tempDir.string())) {
            const auto msg = fmt::format("Failed to extract archive: {}", archivePath.string());
            feedback(msg);
            ERR("Updater: {}", msg);
            return false;
        }

        const auto candidatePath = findFile(tempDir, binaryInfo.re);
        if (candidatePath.empty()) {
            const auto msg = fmt::format("File is not found in the archive");
            feedback(msg);
            ERR("Updater: {}", msg);
            return false;
        }
        const auto newLibPath = fs::canonical(candidatePath);

        // Replace the binary with the new one
        INF("Updater: replacing current file: {} by: {}", binaryInfo.path.string(),
            newLibPath.string());
        rv = replaceBinary(binaryInfo.path.string(), newLibPath.string());

    } catch (fs::filesystem_error &e) {
        const auto msg = fmt::format("Error occurred while updating library: {}", e.what());
        feedback(msg);
        ERR("Updater: {}", msg);
    }

    return rv;
}

bool Updater::replaceBinary(const std::string &libPath, const std::string &newLibPath) const
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
                const auto msg =
                        fmt::format("Error occurred while replacing library: {}", e.what());
                feedback(msg);
                ERR("Updater: {}", msg);
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
        const auto msg = fmt::format("Error occurred while replacing library: {}", e.what());
        feedback(msg);
        ERR("Updater: {}", msg);
        return false;
    }
}

bool Updater::canUpdateSdk(const UrlInfo &urlInfo, const BinaryInfo &binaryInfo) const
{
    try {
        if (binaryInfo.path == getProcessExecutablePath()) {
            // SDK is statically linked into the executable, skip update
            return false;
        }

        if (!isPlatformIdValid(SCORBIT_SDK_PLATFORM_ID)) {
            return false;
        }

        if (urlInfo.url.empty()) {
            const auto msg = fmt::format("Couldn't find update file in assets for the platform: {}",
                                         SCORBIT_SDK_PLATFORM_ID);
            feedback(msg);
            WRN("Updater: {}", msg);
            return false;
        }

        if (!isBinaryObjectValid(binaryInfo)) {
            return false;
        }

        if (!isEncryptedKeyValid()) {
            return false;
        }

        if (!isSdkVersionCompatible(urlInfo.version)) {
            return false;
        }
    } catch (const std::exception &e) {
        const auto msg = fmt::format("Error in 'sdk': {}", e.what());
        feedback(msg);
        ERR("Updater: {}", msg);
        return false;
    }

    return true;
}

bool Updater::canUpdateScorbitd(const UrlInfo &urlInfo, const BinaryInfo &binaryInfo) const
{
    if (!isScorbitdVersionCompatible(urlInfo.version)) {
        return false;
    }

    if (!isBinaryObjectValid(binaryInfo)) {
        return false;
    }

    if (!isPlatformIdValid(m_scorbitdPlatformId)) {
        return false;
    }

    if (urlInfo.url.empty()) {
        const auto msg = fmt::format("Couldn't find update file in assets for the platform: {}",
                                     m_scorbitdPlatformId);
        feedback(msg);
        WRN("Updater: {}", msg);
        return false;
    }

    return true;
}

bool Updater::isPlatformIdValid(std::string_view platformId) const
{
    const auto rv = !platformId.empty() && platformId.find("unknown") == std::string_view::npos;

    if (!rv) {
        const auto msg = fmt::format("Invalid platform ID: {}", platformId);
        feedback(msg);
        WRN("Updater: {}", msg);
    }
    return rv;
}

bool Updater::isBinaryObjectValid(const BinaryInfo &binaryInfo) const
{
    const auto rv = std::regex_match(binaryInfo.path.filename().string(), binaryInfo.re);

    if (!rv) {
        const auto msg = fmt::format("Binary path doesn't match the pattern, path: {}",
                                     binaryInfo.path.string());
        feedback(msg);
        WRN("Updater: {}", msg);
    }
    return rv;
}

bool Updater::isEncryptedKeyValid() const
{
    if (m_useEncryptedKey
        && std::string_view {SCORBIT_SDK_THIS_KEY_HASH}
                   != std::string_view {SCORBIT_SDK_PRODUCTION_KEY_HASH}) {
        const auto msg = fmt::format(
                "Using encrypted key, production key hash mismatch: expected {}, found {}",
                SCORBIT_SDK_PRODUCTION_KEY_HASH, SCORBIT_SDK_THIS_KEY_HASH);
        feedback(msg);
        WRN("Updater: {}", msg);
        return false;
    }
    return true;
}

bool Updater::isSdkVersionCompatible(const std::string &newVersion) const
{
    if (newVersion == SCORBIT_SDK_VERSION) {
        return false;
    }

    // Check if current version is x.y.z then it updates only by x.y.*
    const auto majorMinor =
            fmt::format("{}.{}.", SCORBIT_SDK_VERSION_MAJOR, SCORBIT_SDK_VERSION_MINOR);
    if (newVersion.substr(0, majorMinor.size()) != majorMinor) {
        const auto msg = fmt::format("Version mismatch: can only update by {}x, found: {}",
                                     majorMinor, newVersion);
        feedback(msg);
        WRN("Updater: {}", msg);
        return false;
    }

    return true;
}

bool Updater::isScorbitdVersionCompatible(const std::string &newVersion) const
{
    return !m_scorbitdVersion.empty() && newVersion != m_scorbitdVersion;
}

bool Updater::tryToRemountAndUpdate(const UrlInfo &urlInfo, const BinaryInfo &binaryInfo)
{
    const auto mountResult = utils::fsMakeWritable(binaryInfo.path.parent_path().native());
    if (!mountResult.ok) {
        const auto msg =
                fmt::format("Failed to make filesystem writable: {}", mountResult.mountPoint);
        feedback(msg);
        WRN("Updater: {}", msg);
        return false;
    }

    const auto ok = downloadAndupdateTgz(urlInfo, binaryInfo);
    utils::fsRemountReadOnly(mountResult.mountPoint);
    return ok;
}

bool Updater::downloadAndupdateTgz(const UrlInfo &urlInfo, const BinaryInfo &binaryInfo) const
{
    auto tempFile = fs::temp_directory_path() / fs::unique_path();
    tempFile.replace_extension(".tar.gz");

    INF("Updater: downloading to temp file: {}", tempFile.string());
    bool success = false;

    m_net.download(
            false, // Must be synced download, block until download finished
            [this, &binaryInfo, &urlInfo, &success,
             filename = tempFile.string()](Error error, const std::string &message) {
                success = false;
                if (error == Error::Success) {
                    INF("Updater: downloaded successfully: {}", filename);
                    success = update(filename, binaryInfo);
                    if (success) {
                        const auto msg =
                                fmt::format("Updated successfully, ver: {}", urlInfo.version);
                        feedback(msg);
                        INF("Updater: {}", msg);
                    }

                    // Cleanup downloaded archive
                    boost::system::error_code ec;
                    fs::remove(filename, ec);
                    if (ec) {
                        WRN("Updater: failed to remove temp file: {}, {}", filename, ec.message());
                    }
                } else {
                    const auto msg = fmt::format("Updater: download failed: {}, {}",
                                                 static_cast<int>(error), message);
                    feedback(msg);
                    ERR("Updater: download failed: {}", msg);
                }
            },
            urlInfo.url, tempFile.string(), {{HDR_KEY_ACCEPT_CONTENT, HDR_VAL_CONTENT_OCTET}});

    return success;
}

void Updater::feedback(std::string_view out) const
{
    if (m_feedback.empty()) {
        m_feedback = out;
    } else {
        m_feedback = fmt::format("{}\n\n{}", m_feedback, out);
    }
}

} // namespace detail
} // namespace scorbit
