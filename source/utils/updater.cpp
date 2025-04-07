/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Mar 2025
 *
 ****************************************************************************/

#include "updater.h"
#include "archiver.h"
#include "../logger.h"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/predef.h>
#include <boost/filesystem.hpp>
#include <regex>

namespace {

constexpr auto UPDATE_DIR = "scorbit_sdk_update";
constexpr auto LIBRARY_PATTERN = R"(^(lib)?scorbit_sdk\.(so(\.\d+)*|(\d+\.)*dylib|dll)$)";

namespace fs = boost::filesystem;
using namespace scorbit;
using namespace detail;

bool replaceLibrary(const std::string &libPath, const std::string &newLibPath)
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
                ERR("Error occurred while replacing library: {}", e.what());
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
        ERR("Error occurred while replacing library: {}", e.what());
        return false;
    }
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

bool update(const std::string &archive)
{
    bool rv = false;
    const auto tempDir = fs::temp_directory_path() / UPDATE_DIR;

    try {
        const static std::regex pattern(LIBRARY_PATTERN);
        const auto libPath = getLibraryPath();
        if (libPath.empty()) {
            ERR("Update: library path is empty");
            return false;
        }

        // Make sure that this is shared library and matches the pattern
        if (!std::regex_search(libPath.filename().string(), pattern)) {
            return false;
        }

        // update from tgz archive
        const auto archivePath = fs::path {archive};

        // Extract the archive to a temporary directory
        if (!extract(archivePath.string(), tempDir.string())) {
            ERR("Update: failed to extract archive");
            return false;
        }

        const auto candidatePath = findFile(tempDir, pattern);
        if (candidatePath.empty()) {
            ERR("Update: library not found in the archive");
            return false;
        }
        const auto newLibPath = fs::canonical(candidatePath);

        // Replace the library with the new one
        INF("Update: replacing current library: {} by: {}", libPath.string(), newLibPath.string());
        rv = replaceLibrary(libPath.string(), newLibPath.string());

    } catch (fs::filesystem_error &e) {
        ERR("Couldn't self-update: {}", e.what());
    }

    // cleanup extract dir
    boost::system::error_code ec;
    fs::remove_all(tempDir, ec);
    if (ec) {
        ERR("Update: error removing temp update directory: {}", ec.message());
    }
    return rv;
}

} // namespace detail
} // namespace scorbit
