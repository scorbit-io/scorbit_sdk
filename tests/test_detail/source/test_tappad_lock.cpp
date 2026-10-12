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

#include <catch2/catch_test_macros.hpp>

#include "tappad_lock_test_path.h"

#include <tpm/tappad_lock.h>

#include <atomic>
#include <chrono>
#include <thread>

#if defined(SCORBIT_TAPPAD_LOCK)
#    include <filesystem>
#    include <fstream>
#    include <initializer_list>
#    include <string>
#    include <system_error>
#    include <vector>

#    include <functional>

#    include <fcntl.h>
#    include <signal.h>
#    include <sys/types.h>
#    include <cerrno>
#    include <sys/file.h>
#    include <sys/stat.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

using namespace std::chrono_literals;

TEST_CASE("TappadLock: a hold succeeds and releases", "[tappad_lock]")
{
    {
        const TappadLock hold;
        CHECK(hold.ok());
        CHECK(hold.status() == ATCA_SUCCESS);
    }
    const TappadLock again;
    CHECK(again.ok());
}

#if defined(SCORBIT_TAPPAD_LOCK)

namespace {

namespace fs = std::filesystem;

// Whether some other open file description holds the flock on path.
bool flockHeldElsewhere(const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    const bool held = ::flock(fd, LOCK_EX | LOCK_NB) != 0;
    if (!held) {
        ::flock(fd, LOCK_UN);
    }
    ::close(fd);
    return held;
}

// Restores the per-test-process lock path when a test points it elsewhere.
struct PathOverride {
    explicit PathOverride(std::initializer_list<std::string> paths)
        : m_paths(paths)
    {
        std::vector<const char *> list;
        for (const auto &path : m_paths) {
            list.push_back(path.c_str());
        }
        REQUIRE(hal_tappad_lock_set_paths(list.data(), list.size()) == ATCA_SUCCESS);
    }
    ~PathOverride() { tappad_lock_test::usePrivateLockPath(); }

    std::vector<std::string> m_paths;
};

// A scratch directory next to the private lock file, removed (permissions restored first) however
// the test ends.
struct ScratchDir {
    explicit ScratchDir(const std::string &name)
        : path(fs::path(tappad_lock_test::privateLockPath()).parent_path()
               / (name + "-" + std::to_string(::getpid())))
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directory(path);
    }
    ~ScratchDir()
    {
        std::error_code ec;
        fs::permissions(path, fs::perms::owner_all, ec);
        fs::remove_all(path, ec);
    }
    std::string file(const std::string &name) const { return (path / name).string(); }

    fs::path path;
};

void touch(const std::string &path, mode_t mode)
{
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, mode);
    REQUIRE(fd >= 0);
    ::fchmod(fd, mode);
    ::close(fd);
}

ino_t inodeOf(const std::string &path)
{
    struct stat st;
    REQUIRE(::stat(path.c_str(), &st) == 0);
    return st.st_ino;
}

mode_t modeOf(const std::string &path)
{
    struct stat st;
    REQUIRE(::stat(path.c_str(), &st) == 0);
    return st.st_mode & 07777;
}

// A forked child running @p body(readFd, writeFd), with a pipe each way. The child closes the ends
// it does not use and, by its body, exits when the parent's end closes. The parent's guard kills
// and reaps it on every exit path, so a failing test can never leave ctest waiting.
class ForkedChild
{
public:
    explicit ForkedChild(const std::function<int(int readFd, int writeFd)> &body)
    {
        int toChild[2] {-1, -1};
        int toParent[2] {-1, -1};
        const bool piped = ::pipe(toChild) == 0 && ::pipe(toParent) == 0;
        if (piped) {
            m_pid = ::fork();
        }
        if (!piped || m_pid < 0) {
            // Close what was opened before failing, so a failed REQUIRE leaks no fds.
            for (const int fd : {toChild[0], toChild[1], toParent[0], toParent[1]}) {
                if (fd >= 0) {
                    ::close(fd);
                }
            }
            m_pid = -1;
            FAIL("pipe() or fork() failed");
        }
        if (m_pid == 0) {
            ::close(toChild[1]);
            ::close(toParent[0]);
            ::_exit(body(toChild[0], toParent[1]));
        }
        ::close(toChild[0]);
        ::close(toParent[1]);
        m_toChild = toChild[1];
        m_fromChild = toParent[0];
    }
    ~ForkedChild()
    {
        closeFds();
        if (m_pid > 0) {
            ::kill(m_pid, SIGKILL);
            ::waitpid(m_pid, nullptr, 0);
        }
    }
    ForkedChild(const ForkedChild &) = delete;
    ForkedChild &operator=(const ForkedChild &) = delete;

    void send(char byte) { (void)!::write(m_toChild, &byte, 1); }
    /// The next byte from the child, or '\0' if it closed its end.
    char receive()
    {
        char byte = 0;
        return ::read(m_fromChild, &byte, 1) == 1 ? byte : '\0';
    }
    /// The child's exit status, or -1 if it has not exited within @p limit (it is then killed).
    int wait(std::chrono::milliseconds limit = 10s)
    {
        closeFds();
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            int status = 0;
            if (::waitpid(m_pid, &status, WNOHANG) == m_pid) {
                m_pid = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
            std::this_thread::sleep_for(5ms);
        }
        return -1;
    }

private:
    void closeFds()
    {
        if (m_toChild >= 0) {
            ::close(m_toChild);
            m_toChild = -1;
        }
        if (m_fromChild >= 0) {
            ::close(m_fromChild);
            m_fromChild = -1;
        }
    }

    pid_t m_pid {-1};
    int m_toChild {-1};
    int m_fromChild {-1};
};

// In a child: the next byte from the parent, or '\0' once the parent closed its end.
char childReceive(int fd)
{
    char byte = 0;
    return ::read(fd, &byte, 1) == 1 ? byte : '\0';
}

void childSend(int fd, char byte)
{
    (void)!::write(fd, &byte, 1);
}

} // namespace

TEST_CASE("TappadLock: tests never use the real lock path", "[tappad_lock]")
{
    const TappadLock hold;
    REQUIRE(hold.ok());
    REQUIRE(hal_tappad_lock_path() != nullptr);
    CHECK(std::string(hal_tappad_lock_path()) == tappad_lock_test::privateLockPath());
}

TEST_CASE("TappadLock: nests on one thread and keeps the flock until the outermost release",
          "[tappad_lock]")
{
    const std::string path = tappad_lock_test::privateLockPath();
    CHECK(hal_tappad_lock_depth() == 0);
    {
        const TappadLock outer;
        REQUIRE(outer.ok());
        CHECK(hal_tappad_lock_depth() == 1);
        {
            const TappadLock inner {0};
            REQUIRE(inner.ok());
            CHECK(hal_tappad_lock_depth() == 2);
            CHECK(flockHeldElsewhere(path));
        }
        CHECK(hal_tappad_lock_depth() == 1);
        CHECK(flockHeldElsewhere(path));
    }
    CHECK(hal_tappad_lock_depth() == 0);
    CHECK_FALSE(flockHeldElsewhere(path));
}

TEST_CASE("TappadLock: another thread is excluded, times out busy, then gets in", "[tappad_lock]")
{
    std::atomic<bool> held {false};
    std::atomic<bool> release {false};
    std::thread holder([&] {
        const TappadLock hold;
        held = hold.ok();
        while (!release) {
            std::this_thread::sleep_for(1ms);
        }
    });
    while (!held) {
        std::this_thread::sleep_for(1ms);
    }

    const auto start = std::chrono::steady_clock::now();
    {
        const TappadLock attempt {50};
        CHECK_FALSE(attempt.ok());
        CHECK(attempt.status() == TappadLock::BUSY);
    }
    // The lock measures its budget in whole milliseconds.
    CHECK(std::chrono::steady_clock::now() - start >= 48ms);
    CHECK(hal_tappad_lock_depth() == 0);

    release = true;
    {
        const TappadLock attempt {2000};
        CHECK(attempt.ok());
    }
    holder.join();
}

TEST_CASE("TappadLock: excludes another process, then gets in once it releases", "[tappad_lock]")
{
    ForkedChild child([](int in, int out) {
        // Only the lock under test and raw pipe I/O before _exit.
        const bool locked = hal_tappad_lock(2000) == ATCA_SUCCESS;
        childSend(out, locked ? '1' : '0');
        (void)childReceive(in); // hold until the parent has seen it excluded (or went away)
        hal_tappad_unlock();
        return locked ? 0 : 1;
    });

    REQUIRE(child.receive() == '1');

    const auto busy = hal_tappad_lock(50);
    CHECK(busy == TappadLock::BUSY);
    if (busy == ATCA_SUCCESS) {
        hal_tappad_unlock();
    }
    CHECK(hal_tappad_lock_depth() == 0);

    child.send('x');
    REQUIRE(hal_tappad_lock(2000) == ATCA_SUCCESS);
    hal_tappad_unlock();

    CHECK(child.wait() == 0);
}

TEST_CASE("TappadLock: a child does not inherit its parent's hold", "[tappad_lock]")
{
    REQUIRE(hal_tappad_lock(2000) == ATCA_SUCCESS);

    ForkedChild child([](int, int out) {
        // The parent holds through the inherited open file description; the child must reopen
        // and be excluded, not nest into a hold it does not have.
        if (hal_tappad_lock_file_count() != 0) {
            childSend(out, 'F'); // inherited the parent's files
            return 0;
        }
        const ATCA_STATUS status = hal_tappad_lock(50);
        childSend(out, status == TappadLock::BUSY ? 'B' : 'L');
        if (status == ATCA_SUCCESS) {
            hal_tappad_unlock();
        }
        return 0;
    });

    const char result = child.receive();
    hal_tappad_unlock();
    CHECK(result == 'B');
    CHECK(child.wait() == 0);
}

TEST_CASE("TappadLock: a process taking it back to back does not starve another", "[tappad_lock]")
{
    ForkedChild child([](int in, int out) {
        // Sequence holds, retaken as soon as they are released, until told to stop.
        ::fcntl(in, F_SETFL, O_NONBLOCK);
        childSend(out, '1');
        for (;;) {
            if (hal_tappad_lock_sequence(2000) == ATCA_SUCCESS) {
                ::usleep(20 * 1000);
                hal_tappad_unlock();
            }
            char byte = 0;
            const auto got = ::read(in, &byte, 1);
            if (got == 1 || got == 0) {
                return 0;
            }
        }
    });

    REQUIRE(child.receive() == '1');
    std::this_thread::sleep_for(50ms);

    int acquired = 0;
    for (int i = 0; i < 5; ++i) {
        if (hal_tappad_lock(500) == ATCA_SUCCESS) {
            ++acquired;
            hal_tappad_unlock();
        }
        std::this_thread::sleep_for(5ms);
    }
    child.send('x');
    // Each try may wait 500 ms through holds of 20 ms; allow one loss to scheduling under load.
    CHECK(acquired >= 4);
    CHECK(child.wait() == 0);
}

TEST_CASE("TappadLock: falls back to the next lock path when one is not writable", "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-ro"};
    fs::permissions(dir.path, fs::perms::owner_read | fs::perms::owner_exec);
    // Root ignores directory permissions, so there a path through a regular file stands in.
    const ScratchDir rootDir {"scorbit-tappad-notdir"};
    const std::string blocker = rootDir.file("file");
    touch(blocker, 0600);
    const std::string unwritable =
            (::geteuid() == 0) ? blocker + "/scorbit-tappad.lock" : dir.file("scorbit-tappad.lock");
    const std::string fallback = tappad_lock_test::privateLockPath();

    const PathOverride paths {unwritable, fallback};
    const TappadLock hold;
    REQUIRE(hold.ok());
    REQUIRE(hal_tappad_lock_path() != nullptr);
    CHECK(std::string(hal_tappad_lock_path()) == fallback);
    CHECK(TappadLock::isCrossProcess());
}

TEST_CASE("TappadLock: refuses a symlink at the lock path and leaves its target alone",
          "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-symlink"};
    const std::string target = dir.file("victim");
    touch(target, 0600);
    const std::string link = dir.file("scorbit-tappad.lock");
    fs::create_symlink(target, link);
    const std::string fallback = tappad_lock_test::privateLockPath();

    {
        const PathOverride paths {link, fallback};
        const TappadLock hold;
        REQUIRE(hold.ok());
        REQUIRE(hal_tappad_lock_path() != nullptr);
        CHECK(std::string(hal_tappad_lock_path()) == fallback);
    }
    CHECK(modeOf(target) == 0600);
}

TEST_CASE("TappadLock: refuses a hardlinked lock file", "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-hardlink"};
    const std::string original = dir.file("victim");
    touch(original, 0600);
    const std::string link = dir.file("scorbit-tappad.lock");
    fs::create_hard_link(original, link);
    const std::string fallback = tappad_lock_test::privateLockPath();

    {
        const PathOverride paths {link, fallback};
        const TappadLock hold;
        REQUIRE(hold.ok());
        REQUIRE(hal_tappad_lock_path() != nullptr);
        CHECK(std::string(hal_tappad_lock_path()) == fallback);
    }
    CHECK(modeOf(original) == 0600);
}

TEST_CASE("TappadLock: refuses an existing lock file that is not mode 0666 and leaves it",
          "[tappad_lock]")
{
    // Root could open it and others could not, which would split the lock: everyone refuses it.
    const ScratchDir dir {"scorbit-tappad-existing"};
    const std::string path = dir.file("scorbit-tappad.lock");
    touch(path, 0600);
    const std::string fallback = tappad_lock_test::privateLockPath();
    // Not empty, so it cannot pass for another process's mid-creation file, which is waited for.
    std::ofstream(path, std::ios::app) << 'x';

    {
        const PathOverride paths {path, fallback};
        const TappadLock hold;
        REQUIRE(hold.ok());
        REQUIRE(hal_tappad_lock_path() != nullptr);
        CHECK(std::string(hal_tappad_lock_path()) == fallback);
    }
    CHECK(modeOf(path) == 0600);
}

TEST_CASE("TappadLock: refuses a FIFO at the lock path promptly", "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-fifo"};
    const std::string fifo = dir.file("scorbit-tappad.lock");
    REQUIRE(::mkfifo(fifo.c_str(), 0666) == 0);
    const std::string fallback = tappad_lock_test::privateLockPath();

    const PathOverride paths {fifo, fallback};
    const auto start = std::chrono::steady_clock::now();
    const TappadLock hold {1000};
    REQUIRE(hold.ok());
    CHECK(std::chrono::steady_clock::now() - start < 500ms);
    REQUIRE(hal_tappad_lock_path() != nullptr);
    CHECK(std::string(hal_tappad_lock_path()) == fallback);
}

TEST_CASE("TappadLock: refuses a directory at the lock path", "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-dir"};
    const std::string planted = dir.file("scorbit-tappad.lock");
    fs::create_directory(planted);
    const std::string fallback = tappad_lock_test::privateLockPath();

    const PathOverride paths {planted, fallback};
    const TappadLock hold;
    REQUIRE(hold.ok());
    REQUIRE(hal_tappad_lock_path() != nullptr);
    CHECK(std::string(hal_tappad_lock_path()) == fallback);
}

TEST_CASE("TappadLock: moves up to the preferred path once it becomes usable", "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-upgrade"};
    const fs::path lateDir = dir.path / "late";
    const std::string preferred = (lateDir / "scorbit-tappad.lock").string();
    const std::string fallback = tappad_lock_test::privateLockPath();

    const PathOverride paths {preferred, fallback};
    {
        const TappadLock hold;
        REQUIRE(hold.ok());
        REQUIRE(hal_tappad_lock_path() != nullptr);
        CHECK(std::string(hal_tappad_lock_path()) == fallback);
    }

    fs::create_directory(lateDir);
    {
        const TappadLock hold;
        REQUIRE(hold.ok());
        REQUIRE(hal_tappad_lock_path() != nullptr);
        CHECK(std::string(hal_tappad_lock_path()) == preferred);
        CHECK(flockHeldElsewhere(preferred));
        // Both usable paths are held, not just the preferred one.
        CHECK(hal_tappad_lock_file_count() == 2);
        CHECK(flockHeldElsewhere(fallback));
    }
}

TEST_CASE("TappadLock: reopens the lock file after it is unlinked", "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-unlink"};
    const std::string path = dir.file("scorbit-tappad.lock");
    const PathOverride paths {path};

    {
        const TappadLock hold;
        REQUIRE(hold.ok());
    }
    fs::remove(path);
    {
        // A lock on the orphaned inode would exclude nobody who opens the path now.
        const TappadLock hold;
        REQUIRE(hold.ok());
        REQUIRE(fs::exists(path));
        CHECK(flockHeldElsewhere(path));
    }
}

TEST_CASE("TappadLock: with no usable lock file it still excludes other threads", "[tappad_lock]")
{
    const PathOverride paths {"/nonexistent-scorbit-dir/scorbit-tappad.lock"};

    std::atomic<bool> held {false};
    std::atomic<bool> release {false};
    std::thread holder([&] {
        const TappadLock hold;
        held = hold.ok();
        while (!release) {
            std::this_thread::sleep_for(1ms);
        }
    });
    while (!held) {
        std::this_thread::sleep_for(1ms);
    }

    CHECK_FALSE(TappadLock::isCrossProcess());
    CHECK(TappadLock {50}.status() == TappadLock::BUSY);

    release = true;
    holder.join();
    CHECK(TappadLock {50}.ok());
}

TEST_CASE("TappadLock: lock paths cannot change while held", "[tappad_lock]")
{
    const TappadLock hold;
    REQUIRE(hold.ok());
    const char *path = "/tmp/x.lock";
    CHECK(hal_tappad_lock_set_paths(&path, 1) == ATCA_FUNC_FAIL);
}

TEST_CASE(
        "TappadLock: processes with different usable paths exclude each other through a common one",
        "[tappad_lock]")
{
    // The parent can use A and B; the child only B. Each holds every path it can use, so they
    // meet on B whichever path either would have preferred.
    const ScratchDir dir {"scorbit-tappad-sets"};
    const std::string a = dir.file("a.lock");
    const std::string b = dir.file("b.lock");

    const PathOverride paths {a, b};
    const TappadLock hold;
    REQUIRE(hold.ok());
    CHECK(hal_tappad_lock_file_count() == 2);

    ForkedChild child([b](int, int out) {
        const char *onlyB[] = {b.c_str()};
        hal_tappad_lock_set_paths(onlyB, 1);
        const ATCA_STATUS status = hal_tappad_lock(50);
        childSend(out, status == TappadLock::BUSY ? 'B' : 'L');
        if (status == ATCA_SUCCESS) {
            hal_tappad_unlock();
        }
        return 0;
    });
    CHECK(child.receive() == 'B');
    CHECK(child.wait() == 0);
}

TEST_CASE("TappadLock: two paths naming one file are locked once", "[tappad_lock]")
{
    // As with /var/lock symlinked to /run/lock. Locking the file through two descriptors would
    // deadlock this process against itself.
    const ScratchDir dir {"scorbit-tappad-dedupe"};
    const fs::path real = dir.path / "real";
    const fs::path link = dir.path / "link";
    fs::create_directory(real);
    fs::create_directory_symlink(real, link);
    const std::string viaLink = (link / "scorbit-tappad.lock").string();
    const std::string viaReal = (real / "scorbit-tappad.lock").string();

    const PathOverride paths {viaLink, viaReal};
    for (int i = 0; i < 2; ++i) {
        const TappadLock hold {500};
        REQUIRE(hold.ok());
        CHECK(hal_tappad_lock_file_count() == 1);
        REQUIRE(hal_tappad_lock_path() != nullptr);
        CHECK(std::string(hal_tappad_lock_path()) == viaLink);
        CHECK(flockHeldElsewhere(viaReal));
    }
}

TEST_CASE("TappadLock: a partial acquire that times out releases what it took", "[tappad_lock]")
{
    // Files are locked in (device, inode) order: name them so that `first` is taken first.
    const ScratchDir dir {"scorbit-tappad-partial"};
    std::string first = dir.file("1.lock");
    std::string second = dir.file("2.lock");
    touch(first, 0666);
    touch(second, 0666);
    if (inodeOf(second) < inodeOf(first)) {
        std::swap(first, second);
    }

    ForkedChild child([second](int in, int out) {
        const char *onlySecond[] = {second.c_str()};
        hal_tappad_lock_set_paths(onlySecond, 1);
        const bool locked = hal_tappad_lock(2000) == ATCA_SUCCESS;
        childSend(out, locked ? '1' : '0');
        (void)childReceive(in);
        if (locked) {
            hal_tappad_unlock();
        }
        return 0;
    });
    REQUIRE(child.receive() == '1');

    {
        const PathOverride paths {first, second};
        {
            const TappadLock attempt {100};
            CHECK(attempt.status() == TappadLock::BUSY);
        }
        CHECK(hal_tappad_lock_depth() == 0);
        // The first file was taken and must have been let go again at once.
        CHECK_FALSE(flockHeldElsewhere(first));
    }

    child.send('x');
    CHECK(child.wait() == 0);
}

TEST_CASE("TappadLock: refuses lock files with extra mode bits", "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-modebits"};
    const std::string wide = dir.file("0777.lock");
    const std::string setuid = dir.file("04666.lock");
    touch(wide, 0777);
    touch(setuid, 04666);
    const std::string fallback = tappad_lock_test::privateLockPath();

    {
        const PathOverride paths {wide, setuid, fallback};
        const TappadLock hold;
        REQUIRE(hold.ok());
        CHECK(hal_tappad_lock_file_count() == 1);
        REQUIRE(hal_tappad_lock_path() != nullptr);
        CHECK(std::string(hal_tappad_lock_path()) == fallback);
    }
    CHECK(modeOf(wide) == 0777);
    CHECK(modeOf(setuid) == 04666);
}

TEST_CASE("TappadLock: waits for a lock file another process has just created", "[tappad_lock]")
{
    // The creator's O_CREAT leaves it at the umask's mode until its fchmod(). Refusing it then
    // would let this process leave the path out while the creator locks it.
    const ScratchDir dir {"scorbit-tappad-creating"};
    const std::string path = dir.file("scorbit-tappad.lock");
    const std::string fallback = tappad_lock_test::privateLockPath();
    const PathOverride paths {path, fallback};
    touch(path, 0644);
    CHECK(TappadLock {50}.status() == TappadLock::BUSY);

    std::thread creator([&] {
        std::this_thread::sleep_for(100ms);
        ::chmod(path.c_str(), 0666);
    });
    {
        const TappadLock hold {2000};
        REQUIRE(hold.ok());
        CHECK(hal_tappad_lock_file_count() == 2);
        CHECK(flockHeldElsewhere(path));
    }
    creator.join();
}

TEST_CASE("TappadLock: waits for a just-created lock file it cannot open yet", "[tappad_lock]")
{
    // A creator under umask 077 leaves it 0600 until its fchmod(), so another user gets EACCES.
    // Mode 0000 denies this user too; root opens it anyway, which the case above covers.
    const ScratchDir dir {"scorbit-tappad-unreadable"};
    const std::string path = dir.file("scorbit-tappad.lock");
    const std::string fallback = tappad_lock_test::privateLockPath();

    const PathOverride paths {path, fallback};
    touch(path, 0000);
    CHECK(TappadLock {50}.status() == TappadLock::BUSY);

    std::thread creator([&] {
        std::this_thread::sleep_for(100ms);
        ::chmod(path.c_str(), 0666);
    });
    {
        const TappadLock hold {2000};
        REQUIRE(hold.ok());
        CHECK(hal_tappad_lock_file_count() == 2);
    }
    creator.join();
}

TEST_CASE("TappadLock: drops a kept lock file that was chmod'ed or hard-linked since",
          "[tappad_lock]")
{
    // Newly opening processes refuse it, so a holder that kept locking it would split the lock.
    const ScratchDir dir {"scorbit-tappad-revalidate"};
    const std::string path = dir.file("scorbit-tappad.lock");
    const std::string fallback = tappad_lock_test::privateLockPath();

    const PathOverride paths {path, fallback};
    const auto heldCount = [] {
        const TappadLock hold;
        REQUIRE(hold.ok());
        return hal_tappad_lock_file_count();
    };
    CHECK(heldCount() == 2);

    SECTION("chmod'ed")
    {
        REQUIRE(::chmod(path.c_str(), 0777) == 0);
    }
    SECTION("hard-linked")
    {
        fs::create_hard_link(path, dir.file("extra-link"));
    }
    const TappadLock hold;
    REQUIRE(hold.ok());
    CHECK(hal_tappad_lock_file_count() == 1);
    REQUIRE(hal_tappad_lock_path() != nullptr);
    CHECK(std::string(hal_tappad_lock_path()) == fallback);
    CHECK_FALSE(flockHeldElsewhere(path));
}

#    if defined(ATCA_TAPPAD_LOCK_ENV_OVERRIDE)
TEST_CASE("TappadLock: transient flock errors wait, unsupported ones drop the path",
          "[tappad_lock]")
{
    struct HookGuard {
        ~HookGuard() { hal_tappad_lock_test_set_flock(nullptr); }
    } guard;

    // ENOLCK can pass; leaving the path out would let another process lock it alone.
    hal_tappad_lock_test_set_flock([](int fd, int operation) {
        if (operation & LOCK_EX) {
            errno = ENOLCK;
            return -1;
        }
        return ::flock(fd, operation);
    });
    CHECK(TappadLock {50}.status() == TappadLock::BUSY);

    // Not supported on the file system: every process sees the same, so it is left out.
    hal_tappad_lock_test_set_flock([](int fd, int operation) {
        if (operation & LOCK_EX) {
            errno = EOPNOTSUPP;
            return -1;
        }
        return ::flock(fd, operation);
    });
    {
        const TappadLock hold {500};
        REQUIRE(hold.ok());
        CHECK(hal_tappad_lock_file_count() == 0);
        CHECK_FALSE(TappadLock::isCrossProcess());
    }
}
#    endif

TEST_CASE("TappadLock: processes reaching the files in different orders do not deadlock",
          "[tappad_lock]")
{
    // dir0 is a symlink to dir2 and dir1 is real. The parent lists [dir0, dir1, dir2] and so
    // meets file 2 before file 1; the child lists [dir1, dir2]. Locking in path order would let
    // each hold one file while waiting for the other; locking by inode never does.
    const ScratchDir dir {"scorbit-tappad-order"};
    fs::create_directory(dir.path / "dir1");
    fs::create_directory(dir.path / "dir2");
    fs::create_directory_symlink(dir.path / "dir2", dir.path / "dir0");
    const std::string p0 = (dir.path / "dir0" / "x.lock").string();
    const std::string p1 = (dir.path / "dir1" / "x.lock").string();
    const std::string p2 = (dir.path / "dir2" / "x.lock").string();
    // Created in the order that makes path order and inode order disagree for the parent.
    touch(p1, 0666);
    touch(p2, 0666);

    constexpr int ROUNDS = 40;
    ForkedChild child([p1, p2](int, int out) {
        const char *paths[] = {p1.c_str(), p2.c_str()};
        hal_tappad_lock_set_paths(paths, 2);
        int failures = 0;
        for (int i = 0; i < ROUNDS; ++i) {
            if (hal_tappad_lock(2000) == ATCA_SUCCESS) {
                ::usleep(1000);
                hal_tappad_unlock();
            } else {
                ++failures;
            }
        }
        childSend(out, failures == 0 ? 'k' : 'f');
        return 0;
    });

    const PathOverride paths {p0, p1, p2};
    int failures = 0;
    for (int i = 0; i < ROUNDS; ++i) {
        if (hal_tappad_lock(2000) == ATCA_SUCCESS) {
            CHECK(hal_tappad_lock_file_count() == 2);
            ::usleep(1000);
            hal_tappad_unlock();
        } else {
            ++failures;
        }
    }
    CHECK(failures == 0);
    CHECK(child.receive() == 'k');
    CHECK(child.wait() == 0);
}

#    if defined(__APPLE__)
TEST_CASE("TappadLock: locks a file it may only open read-only", "[tappad_lock]")
{
    // An immutable file refuses O_RDWR with EPERM, as a read-only mount refuses it with EROFS
    // (that case is covered by the Linux container smoke test); flock() works on O_RDONLY.
    const ScratchDir dir {"scorbit-tappad-readonly"};
    const std::string path = dir.file("scorbit-tappad.lock");
    touch(path, 0666);
    REQUIRE(::chflags(path.c_str(), UF_IMMUTABLE) == 0);
    struct Unflag {
        std::string path;
        ~Unflag() { ::chflags(path.c_str(), 0); }
    } unflag {path};

    const PathOverride paths {path};
    const TappadLock hold;
    REQUIRE(hold.ok());
    CHECK(hal_tappad_lock_file_count() == 1);
    CHECK(flockHeldElsewhere(path));
}
#    endif

TEST_CASE("TappadLock: retries when one of several held files is replaced while waiting",
          "[tappad_lock]")
{
    const ScratchDir dir {"scorbit-tappad-replace"};
    const std::string a = dir.file("a.lock");
    const std::string b = dir.file("b.lock");

    ForkedChild child([b](int in, int out) {
        const char *onlyB[] = {b.c_str()};
        hal_tappad_lock_set_paths(onlyB, 1);
        const bool locked = hal_tappad_lock(2000) == ATCA_SUCCESS;
        childSend(out, locked ? '1' : '0');
        (void)childReceive(in);
        if (locked) {
            hal_tappad_unlock();
        }
        return 0;
    });
    REQUIRE(child.receive() == '1');

    const PathOverride paths {a, b};
    std::atomic<bool> held {false};
    std::atomic<bool> release {false};
    std::atomic<int> count {-1};
    std::atomic<bool> newFileLocked {false};
    std::thread waiter([&] {
        const TappadLock hold {3000};
        held = hold.ok();
        count = static_cast<int>(hal_tappad_lock_file_count());
        newFileLocked = flockHeldElsewhere(a);
        while (!release) {
            std::this_thread::sleep_for(1ms);
        }
    });

    // The waiter has opened both files and is stuck on b. Replace a under it, then let b go:
    // the post-acquire check must see the replacement and lock the new a.
    std::this_thread::sleep_for(100ms);
    fs::remove(a);
    touch(a, 0666);
    child.send('x');

    for (int i = 0; i < 3000 && count < 0; ++i) {
        std::this_thread::sleep_for(1ms);
    }
    release = true;
    waiter.join();
    CHECK(held);
    CHECK(count == 2);
    CHECK(newFileLocked);
    CHECK(child.wait() == 0);
}

#endif
