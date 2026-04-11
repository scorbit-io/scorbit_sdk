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

#include <utils/archiver.h>

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

using namespace scorbit::detail;
namespace fs = boost::filesystem;

namespace {

class TempDir
{
public:
    TempDir()
        : m_path(fs::temp_directory_path() / fs::unique_path("test_archiver_%%%%-%%%%"))
    {
        fs::create_directories(m_path);
    }

    ~TempDir() { fs::remove_all(m_path); }

    const fs::path &path() const { return m_path; }

    std::string createFile(const std::string &name, const std::string &content)
    {
        auto filePath = m_path / name;
        std::ofstream ofs(filePath.string(), std::ios::binary);
        ofs << content;
        return filePath.string();
    }

private:
    fs::path m_path;
};

} // namespace

TEST_CASE("createTarGz with file entries")
{
    TempDir tmpDir;

    SECTION("Single file")
    {
        auto srcPath = tmpDir.createFile("test.log", "hello world");
        auto archivePath = (tmpDir.path() / "output.tar.gz").string();

        std::vector<ArchiveFileEntry> files = {{"logs/test.log", srcPath}};
        REQUIRE(createTarGz(archivePath, files));
        REQUIRE(fs::exists(archivePath));
        CHECK(fs::file_size(archivePath) > 0);
    }

    SECTION("Multiple files")
    {
        auto src1 = tmpDir.createFile("a.log", "file a content");
        auto src2 = tmpDir.createFile("b.log", "file b content with more data");
        auto archivePath = (tmpDir.path() / "multi.tar.gz").string();

        std::vector<ArchiveFileEntry> files = {
                {"logs/a.log", src1},
                {"logs/b.log", src2},
        };
        REQUIRE(createTarGz(archivePath, files));
        REQUIRE(fs::exists(archivePath));
        CHECK(fs::file_size(archivePath) > 0);
    }

    SECTION("Non-existent source file is skipped gracefully")
    {
        auto srcValid = tmpDir.createFile("valid.log", "valid content");
        auto archivePath = (tmpDir.path() / "partial.tar.gz").string();

        std::vector<ArchiveFileEntry> files = {
                {"logs/valid.log", srcValid},
                {"logs/missing.log", "/nonexistent/path/missing.log"},
        };
        REQUIRE(createTarGz(archivePath, files));
        REQUIRE(fs::exists(archivePath));
    }
}

TEST_CASE("createTarGz with memory entries")
{
    TempDir tmpDir;

    SECTION("Single memory entry")
    {
        auto archivePath = (tmpDir.path() / "mem.tar.gz").string();

        std::vector<ArchiveMemoryEntry> memEntries = {
                {"logs/extra.log", "arbitrary log data here"},
        };
        REQUIRE(createTarGz(archivePath, {}, memEntries));
        REQUIRE(fs::exists(archivePath));
        CHECK(fs::file_size(archivePath) > 0);
    }

    SECTION("Empty data memory entry")
    {
        auto archivePath = (tmpDir.path() / "empty_mem.tar.gz").string();

        std::vector<ArchiveMemoryEntry> memEntries = {
                {"logs/empty.log", ""},
        };
        REQUIRE(createTarGz(archivePath, {}, memEntries));
        REQUIRE(fs::exists(archivePath));
    }
}

TEST_CASE("createTarGz with mixed entries")
{
    TempDir tmpDir;

    auto srcPath = tmpDir.createFile("real.log", "real file content");
    auto archivePath = (tmpDir.path() / "mixed.tar.gz").string();

    std::vector<ArchiveFileEntry> files = {{"logs/real.log", srcPath}};
    std::vector<ArchiveMemoryEntry> memEntries = {{"logs/extra.log", "in-memory log data"}};

    REQUIRE(createTarGz(archivePath, files, memEntries));
    REQUIRE(fs::exists(archivePath));
    CHECK(fs::file_size(archivePath) > 0);
}

TEST_CASE("createTarGz with extract round-trip")
{
    TempDir tmpDir;

    auto srcPath = tmpDir.createFile("roundtrip.log", "round trip content");
    auto archivePath = (tmpDir.path() / "roundtrip.tar.gz").string();
    auto extractDir = (tmpDir.path() / "extracted").string();

    std::vector<ArchiveFileEntry> files = {{"logs/roundtrip.log", srcPath}};
    std::vector<ArchiveMemoryEntry> memEntries = {{"logs/extra.log", "extra content"}};

    REQUIRE(createTarGz(archivePath, files, memEntries));
    REQUIRE(fs::exists(archivePath));

    fs::create_directories(extractDir);
    REQUIRE(extract(archivePath, extractDir));

    auto extractedLog = fs::path(extractDir) / "logs" / "roundtrip.log";
    auto extractedExtra = fs::path(extractDir) / "logs" / "extra.log";

    REQUIRE(fs::exists(extractedLog));
    REQUIRE(fs::exists(extractedExtra));

    std::ifstream logIfs(extractedLog.string());
    std::string logContent((std::istreambuf_iterator<char>(logIfs)),
                           std::istreambuf_iterator<char>());
    CHECK(logContent == "round trip content");

    std::ifstream extraIfs(extractedExtra.string());
    std::string extraContent((std::istreambuf_iterator<char>(extraIfs)),
                             std::istreambuf_iterator<char>());
    CHECK(extraContent == "extra content");
}

TEST_CASE("createTarGz edge cases")
{
    TempDir tmpDir;

    SECTION("No entries returns false")
    {
        auto archivePath = (tmpDir.path() / "empty.tar.gz").string();
        CHECK_FALSE(createTarGz(archivePath, {}));
    }

    SECTION("All source files missing still produces archive with no file entries")
    {
        auto archivePath = (tmpDir.path() / "all_missing.tar.gz").string();

        std::vector<ArchiveFileEntry> files = {
                {"logs/a.log", "/nonexistent/a.log"},
                {"logs/b.log", "/nonexistent/b.log"},
        };
        // All files are missing, but we still attempt to create the archive.
        // The result depends on implementation: it creates a valid empty gzip stream.
        // This should still succeed (the archive just has no entries).
        CHECK(createTarGz(archivePath, files));
    }
}
