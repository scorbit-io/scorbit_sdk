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

// Two writers in one process sharing a read-only mount: it goes read-only again only when the last
// of them is done, so one finishing can't pull the mount from under the other.

#include "../../../libs/utils/source/fs_mount_holders.h"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

using utils::detail::MountHolders;
using utils::detail::MountOps;
using utils::detail::optionsReadOnly;

namespace {

/// One mount point, "/", with counted remounts that a test can make fail.
struct FakeMount {
    std::atomic<bool> readOnly {true}; // read by the threads test outside the holders' lock
    bool failRW = false;
    bool failRO = false;
    int rw = 0;
    int ro = 0;

    MountOps ops()
    {
        return MountOps {
                [](const std::filesystem::path &) { return std::string {"/"}; },
                [this](const std::string &) { return readOnly.load(); },
                [this](const std::string &) {
                    ++rw;
                    if (failRW) {
                        return false;
                    }
                    readOnly = false;
                    return true;
                },
                [this](const std::string &) {
                    ++ro;
                    if (failRO) {
                        return false;
                    }
                    readOnly = true;
                    return true;
                },
        };
    }
};

} // namespace

TEST_CASE("Two overlapping writers remount read-only once, after the second is done")
{
    FakeMount mount;
    MountHolders holders(mount.ops());

    const auto first = holders.makeWritable("/usr/local/scorbit/scorbitd");
    CHECK(first.ok);
    CHECK(first.mountPoint == "/");
    const auto second = holders.makeWritable("/usr/local/scorbit/payload");
    CHECK(second.ok);
    CHECK(second.mountPoint == "/"); // the second writer holds it too
    CHECK(mount.rw == 1);
    CHECK(holders.holders("/") == 2);

    CHECK(holders.remountReadOnly(first.mountPoint));
    CHECK_FALSE(mount.readOnly); // still writable for the second writer
    CHECK(mount.ro == 0);

    CHECK(holders.remountReadOnly(second.mountPoint));
    CHECK(mount.readOnly);
    CHECK(mount.ro == 1);
    CHECK(holders.holders("/") == 0);
}

TEST_CASE("Release order doesn't matter")
{
    FakeMount mount;
    MountHolders holders(mount.ops());
    const auto first = holders.makeWritable("/a");
    const auto second = holders.makeWritable("/b");

    CHECK(holders.remountReadOnly(second.mountPoint)); // the later writer finishes first
    CHECK_FALSE(mount.readOnly);
    CHECK(holders.remountReadOnly(first.mountPoint));
    CHECK(mount.readOnly);
    CHECK(mount.ro == 1);
}

TEST_CASE("An empty release is a no-op")
{
    FakeMount mount;
    MountHolders holders(mount.ops());
    const auto held = holders.makeWritable("/a");

    CHECK(holders.remountReadOnly(""));
    CHECK(mount.ro == 0);
    CHECK(holders.holders("/") == 1);
    CHECK(holders.remountReadOnly(held.mountPoint));
    CHECK(mount.ro == 1);
}

TEST_CASE("A mount made writable outside this process is left alone")
{
    FakeMount mount;
    mount.readOnly = false;
    MountHolders holders(mount.ops());

    const auto result = holders.makeWritable("/a");
    CHECK(result.ok);
    CHECK(result.mountPoint.empty()); // nothing held, nothing to give back
    CHECK(mount.rw == 0);
    CHECK(holders.holders("/") == 0);
    CHECK(holders.remountReadOnly(result.mountPoint));
    CHECK(mount.ro == 0);
    CHECK_FALSE(mount.readOnly);
}

TEST_CASE("A failed remount read-write holds nothing")
{
    FakeMount mount;
    mount.failRW = true;
    MountHolders holders(mount.ops());

    const auto result = holders.makeWritable("/a");
    CHECK_FALSE(result.ok);
    CHECK(result.mountPoint == "/");
    CHECK(holders.holders("/") == 0);

    mount.failRW = false;
    CHECK(holders.makeWritable("/a").ok);
    CHECK(holders.holders("/") == 1);
}

TEST_CASE("A failed remount read-only can be retried, and the count never underflows")
{
    FakeMount mount;
    MountHolders holders(mount.ops());
    const auto held = holders.makeWritable("/a");

    mount.failRO = true;
    CHECK_FALSE(holders.remountReadOnly(held.mountPoint));
    CHECK(holders.holders("/") == 0);
    CHECK_FALSE(mount.readOnly);

    mount.failRO = false;
    CHECK(holders.remountReadOnly(held.mountPoint)); // the caller's retry
    CHECK(mount.readOnly);
    CHECK(mount.ro == 2);
    CHECK(holders.holders("/") == 0);

    // A new writer after all that starts from scratch
    CHECK(holders.makeWritable("/a").mountPoint == "/");
    CHECK(holders.holders("/") == 1);
}

TEST_CASE("A failed read-only remount is retried by the next writer's release")
{
    FakeMount mount;
    MountHolders holders(mount.ops());
    const auto updater = holders.makeWritable("/usr/local/bin/x");
    mount.failRO = true; // EBUSY: a replaced binary still runs
    CHECK_FALSE(holders.remountReadOnly(updater.mountPoint));
    CHECK(holders.owed("/"));
    CHECK_FALSE(mount.readOnly);

    // The mount is still writable, but this process left it so: the next writer takes it over
    const auto next = holders.makeWritable("/etc/settings");
    CHECK(next.ok);
    CHECK(next.mountPoint == "/");
    CHECK(mount.rw == 1); // no second remount read-write: it is writable already
    CHECK(holders.holders("/") == 1);

    mount.failRO = false;
    CHECK(holders.remountReadOnly(next.mountPoint)); // its release retries, and succeeds
    CHECK(mount.readOnly);
    CHECK_FALSE(holders.owed("/"));

    // Once paid, a mount made writable outside the process is left alone again
    mount.readOnly = false;
    CHECK(holders.makeWritable("/etc/settings").mountPoint.empty());
}

TEST_CASE("A held mount remounted read-only from outside is taken back for the next writer")
{
    FakeMount mount;
    MountHolders holders(mount.ops());
    const auto first = holders.makeWritable("/a");
    mount.readOnly = true; // another process remounted it

    const auto second = holders.makeWritable("/b");
    CHECK(second.ok);
    CHECK(mount.rw == 2);
    CHECK_FALSE(mount.readOnly);
    CHECK(holders.holders("/") == 2);

    mount.readOnly = true;
    mount.failRW = true;
    const auto third = holders.makeWritable("/c");
    CHECK_FALSE(third.ok);
    CHECK(holders.holders("/") == 2); // a failed writer holds nothing
    CHECK(holders.remountReadOnly(first.mountPoint));
    CHECK(holders.remountReadOnly(second.mountPoint));
    CHECK(holders.holders("/") == 0);
}

TEST_CASE("Writers on several threads remount read-only once, after the last is done")
{
    FakeMount mount;
    MountHolders holders(mount.ops());
    constexpr int WRITERS = 16;
    std::atomic<int> sawReadOnly {0};
    std::vector<std::thread> threads;
    for (int i = 0; i < WRITERS; ++i) {
        threads.emplace_back([&] {
            for (int round = 0; round < 50; ++round) {
                const auto held = holders.makeWritable("/a");
                if (!held.ok || mount.readOnly) {
                    ++sawReadOnly;
                }
                holders.remountReadOnly(held.mountPoint);
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    CHECK(sawReadOnly == 0);
    CHECK(holders.holders("/") == 0);
    CHECK(mount.readOnly);
    CHECK(mount.rw == mount.ro); // every remount read-write was paired with one back
}

TEST_CASE("Only the ro option itself means read-only")
{
    CHECK(optionsReadOnly("ro"));
    CHECK(optionsReadOnly("ro,relatime"));
    CHECK(optionsReadOnly("relatime,ro"));
    CHECK_FALSE(optionsReadOnly("rw,errors=remount-ro"));
    CHECK_FALSE(optionsReadOnly("rw,relatime"));
    CHECK_FALSE(optionsReadOnly(""));
    CHECK_FALSE(optionsReadOnly("rootcontext=x"));
}

TEST_CASE("A mount is read-only when either its own or its superblock options say ro")
{
    using utils::detail::parseMountInfoLine;

    const auto writable = parseMountInfoLine(
            "25 1 179:2 / / rw,noatime shared:1 - ext4 /dev/root rw,errors=continue");
    CHECK(writable.mountPoint == "/");
    CHECK_FALSE(writable.readOnly);

    // A Spike's read-only root
    CHECK(parseMountInfoLine(
                  "25 1 179:2 / / ro,noatime shared:1 - ext4 /dev/root ro,errors=continue")
                  .readOnly);
    // A read-only bind over a writable superblock: writes still fail
    const auto bind = parseMountInfoLine(
            "40 25 179:2 /data /mnt/data ro,relatime - ext4 /dev/root rw,errors=remount-ro");
    CHECK(bind.mountPoint == "/mnt/data");
    CHECK(bind.readOnly);
    // No optional fields before the separator, and a superblock read-only under a rw mount
    CHECK(parseMountInfoLine("41 25 0:30 / /boot rw,relatime - vfat /dev/mmcblk0p1 ro").readOnly);
    CHECK_FALSE(
            parseMountInfoLine("42 25 0:31 / /tmp rw shared:5 master:2 - tmpfs tmpfs rw,size=1k")
                    .readOnly);
}
