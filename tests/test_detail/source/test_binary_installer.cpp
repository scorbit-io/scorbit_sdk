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

#include <utils/binary_installer.h>
#include "executable_bytes.h"

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem.hpp>
#include <boost/predef.h>
#include <fstream>
#include <iterator>

using namespace scorbit::detail;
namespace fs = boost::filesystem;

namespace {

class TempDir
{
public:
    TempDir()
        : m_path(fs::temp_directory_path() / fs::unique_path("test_installer_%%%%-%%%%"))
    {
        fs::create_directories(m_path);
    }

    ~TempDir() { fs::remove_all(m_path); }

    const fs::path &path() const { return m_path; }

private:
    fs::path m_path;
};

void writeFile(const fs::path &path, const std::string &content)
{
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path.string(), std::ios::binary | std::ios::trunc);
    ofs << content;
}

std::string readFile(const fs::path &path)
{
    std::ifstream ifs(path.string(), std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("isExecutableImage")
{
    TempDir tmp;
    const auto file = tmp.path() / "libscorbit_sdk.so";
    std::string error;

    SECTION("Executable of this platform's format")
    {
        writeFile(file, executableBytes("code"));
        CHECK(isExecutableImage(file, error));
    }

    SECTION("Empty file")
    {
        writeFile(file, "");
        CHECK_FALSE(isExecutableImage(file, error));
        CHECK(error.find("Empty file") != std::string::npos);
    }

    SECTION("Shorter than the magic")
    {
        writeFile(file, "ab");
        CHECK_FALSE(isExecutableImage(file, error));
        CHECK(error.find("Too short") != std::string::npos);
    }

    SECTION("Not an executable")
    {
        writeFile(file, "<html>404 Not Found</html>");
        CHECK_FALSE(isExecutableImage(file, error));
        CHECK(error.find("Not an executable") != std::string::npos);
    }

    SECTION("Missing file")
    {
        CHECK_FALSE(isExecutableImage(file, error));
        CHECK(error.find("Not a regular file") != std::string::npos);
    }
}

TEST_CASE("installBinary")
{
    TempDir tmp;
    const auto target = tmp.path() / "libscorbit_sdk.so";
    const auto candidate = tmp.path() / "scorbit_update" / "lib" / "libscorbit_sdk.so";
    const fs::path staged {target.string() + ".new"};
    const fs::path backup {target.string() + ".old"};

    const auto original = executableBytes("original version");
    const auto updated = executableBytes("updated version, a bit longer");
    writeFile(target, original);
    std::string error;

    SECTION("Replaces the target and leaves nothing behind")
    {
        writeFile(candidate, updated);

        REQUIRE(installBinary(target, candidate, error));
        CHECK(error.empty());
        CHECK(readFile(target) == updated);
        CHECK_FALSE(fs::exists(candidate));
        CHECK_FALSE(fs::exists(staged));
        CHECK_FALSE(fs::exists(backup));
    }

    SECTION("Leftovers of an interrupted update are cleaned up")
    {
        writeFile(staged, "half written");
        writeFile(backup, original);
        writeFile(candidate, updated);

        REQUIRE(installBinary(target, candidate, error));
        CHECK(readFile(target) == updated);
        CHECK_FALSE(fs::exists(staged));
        CHECK_FALSE(fs::exists(backup));
    }

    SECTION("Empty candidate is refused, the target stays")
    {
        writeFile(candidate, "");

        CHECK_FALSE(installBinary(target, candidate, error));
        CHECK(error.find("Empty file") != std::string::npos);
        CHECK(readFile(target) == original);
        CHECK_FALSE(fs::exists(staged));
        CHECK_FALSE(fs::exists(backup));
    }

    SECTION("Candidate that's not an executable is refused, the target stays")
    {
        writeFile(candidate, "garbage that is not a binary");

        CHECK_FALSE(installBinary(target, candidate, error));
        CHECK(error.find("Not an executable") != std::string::npos);
        CHECK(readFile(target) == original);
        CHECK_FALSE(fs::exists(staged));
        CHECK_FALSE(fs::exists(backup));
    }

    SECTION("Missing candidate is refused, the target stays")
    {
        CHECK_FALSE(installBinary(target, candidate, error));
        CHECK(readFile(target) == original);
    }

    SECTION("Failed check of the installed binary puts the original back")
    {
        writeFile(candidate, updated);
        fs::path checked;

        CHECK_FALSE(installBinary(target, candidate, error, [&](const fs::path &installed) {
            checked = installed;
            return readFile(installed) != updated; // sees the new binary, and rejects it
        }));
        CHECK(checked == target);
        CHECK(error.find("original restored") != std::string::npos);
        CHECK(readFile(target) == original);
        CHECK_FALSE(fs::exists(staged));
        CHECK_FALSE(fs::exists(backup));
    }

#if !BOOST_OS_WINDOWS
    SECTION("The target keeps its permissions")
    {
        const auto perms = fs::owner_all | fs::group_read | fs::group_exe;
        fs::permissions(target, perms);
        writeFile(candidate, updated);
        fs::permissions(candidate, fs::owner_read | fs::owner_write);

        REQUIRE(installBinary(target, candidate, error));
        CHECK(fs::status(target).permissions() == perms);
    }
#endif
}
