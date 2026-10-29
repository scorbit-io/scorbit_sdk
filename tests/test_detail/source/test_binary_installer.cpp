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

TEST_CASE("readElfImage")
{
    TempDir tmp;
    const auto file = tmp.path() / "libscorbit_sdk.so";
    const auto image = elfBytes(std::string(1000, 'x'));
    std::string error;

    SECTION("Complete 64-bit image")
    {
        writeFile(file, image);
        const auto info = readElfImage(file, error);
        REQUIRE(info);
        CHECK(*info == ElfInfo {2, 1, TEST_ELF_MACHINE});
    }

    SECTION("Complete 32-bit image")
    {
        writeFile(file, elfBytes("code", 40, false));
        const auto info = readElfImage(file, error);
        REQUIRE(info);
        CHECK(*info == ElfInfo {1, 1, 40});
    }

    SECTION("Cut off inside a loadable segment")
    {
        writeFile(file, image.substr(0, image.size() - 10));
        CHECK_FALSE(readElfImage(file, error));
        CHECK(error.find("loadable segment") != std::string::npos);
    }

    SECTION("Cut off inside the header")
    {
        writeFile(file, image.substr(0, 30));
        CHECK_FALSE(readElfImage(file, error));
        CHECK(error.find("header cut short") != std::string::npos);
    }

    SECTION("Only the magic")
    {
        writeFile(file, image.substr(0, 4));
        CHECK_FALSE(readElfImage(file, error));
    }

    SECTION("Section headers past the end of the file")
    {
        auto cut = image;
        putLittleEndian(cut, 40, cut.size(), 8); // e_shoff
        putLittleEndian(cut, 60, 1, 2);          // e_shnum
        writeFile(file, cut);
        CHECK_FALSE(readElfImage(file, error));
        CHECK(error.find("section headers") != std::string::npos);
    }

    SECTION("Relocatable object, not loadable")
    {
        auto object = image;
        putLittleEndian(object, 16, 1, 2); // e_type: ET_REL
        writeFile(file, object);
        CHECK_FALSE(readElfImage(file, error));
    }

    SECTION("Not an ELF image")
    {
        writeFile(file, std::string(200, 'x'));
        CHECK_FALSE(readElfImage(file, error));
        CHECK(error.find("bad magic") != std::string::npos);
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

    SECTION("The target itself as the candidate is refused, the target stays")
    {
        CHECK_FALSE(installBinary(target, target, error));
        CHECK(error.find("is the current one") != std::string::npos);
        CHECK(readFile(target) == original);
        CHECK_FALSE(fs::exists(staged));
    }

    SECTION("A backup that can't be removed refuses the update, the target stays")
    {
        writeFile(backup / "in_use", "can't remove a directory with files in it");
        writeFile(candidate, updated);

        CHECK_FALSE(installBinary(target, candidate, error));
        CHECK(error.find("previous backup") != std::string::npos);
        CHECK(readFile(target) == original);
        CHECK(readFile(candidate) == updated);
    }

#if !BOOST_OS_WINDOWS
    SECTION("A candidate linked to the target is refused, the target stays")
    {
        fs::create_directories(candidate.parent_path());
        fs::create_symlink(target, candidate);

        CHECK_FALSE(installBinary(target, candidate, error));
        CHECK(readFile(target) == original);
        CHECK_FALSE(fs::exists(staged));
        CHECK_FALSE(fs::exists(backup));
    }
#endif

#if !BOOST_OS_WINDOWS && !BOOST_OS_MACOS
    SECTION("A binary for another architecture is refused, the target stays")
    {
        writeFile(candidate, elfBytes("other", 40)); // EM_ARM

        CHECK_FALSE(installBinary(target, candidate, error));
        CHECK(error.find("another architecture") != std::string::npos);
        CHECK(readFile(target) == original);
    }

    SECTION("A cut off binary is refused, the target stays")
    {
        writeFile(candidate, updated.substr(0, updated.size() - 1));

        CHECK_FALSE(installBinary(target, candidate, error));
        CHECK(error.find("Not a complete ELF image") != std::string::npos);
        CHECK(readFile(target) == original);
    }
#endif

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
