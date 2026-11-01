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

#include <logger/logger.h>
#include <scorbit_sdk/log.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/trompeloeil.hpp>
#include <thread>
#include <random>
#include <chrono>
#include <atomic>
#include <future>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace std::chrono_literals;

struct CallbackHelper {
    std::string msg;
    scorbit::LogLevel level;
    std::string fileName;
    int line;
};

inline void sleepForLogger()
{
    std::this_thread::sleep_for(1ms); // Give some time to the logger thread
}

TEST_CASE("logger using functor object callback")
{
    struct loggerFunctor {
        void operator()(std::string_view msg, scorbit::LogLevel level, const char *file, int line,
                        int64_t timestamp)
        {
            data.msg = msg;
            data.level = level;
            data.fileName = file;
            data.line = line;
        }

        CallbackHelper data;
    };

    loggerFunctor l;

    SECTION("add logger")
    {
        // We have to use std::ref to pass the reference to the object, otherwise we can't reach
        // the object's data as the object will be copied
        scorbit::addLoggerCallback(std::ref(l), 512);
        INF("Hello");
        sleepForLogger();
        int line = __LINE__ - 2;
        const char *file = __FILE__;
        CHECK(l.data.msg == "Hello");
        CHECK(l.data.level == scorbit::LogLevel::Info);
        CHECK(l.data.fileName == file + std::string_view(file).find_last_of("/\\") + 1);
        CHECK(l.data.line == line);
    }

    SECTION("Log levels")
    {
        scorbit::addLoggerCallback(std::ref(l), 512);
        DBG("Debug");
        sleepForLogger();
        CHECK(l.data.level == scorbit::LogLevel::Debug);

        INF("Info");
        sleepForLogger();
        CHECK(l.data.level == scorbit::LogLevel::Info);

        WRN("Warn");
        sleepForLogger();
        CHECK(l.data.level == scorbit::LogLevel::Warn);

        ERR("Error");
        sleepForLogger();
        CHECK(l.data.level == scorbit::LogLevel::Error);
    }

    resetLogger();
}

struct MockCallback {
    MAKE_MOCK4(callbackFunc, void(std::string_view, scorbit::LogLevel, const char *, int));
} mockCallback;

void callbackFunc(std::string_view msg, scorbit::LogLevel level, const char *file, int line,
                  int64_t timestamp)
{
    mockCallback.callbackFunc(msg, level, file, line);
}

TEST_CASE("Logger with function callback using trompeloeil")
{
    using namespace trompeloeil;
    using namespace scorbit;

    SECTION("SectionName")
    {
        REQUIRE_CALL(mockCallback, callbackFunc(eq<std::string_view>("Hello"), eq(LogLevel::Info),
                                                _, _));

        scorbit::addLoggerCallback(callbackFunc, 512);
        INF("Hello");
        sleepForLogger();
    }

    resetLogger();
}

static void callback(std::string_view msg, scorbit::LogLevel level, const char *file, int line,
                     int64_t timestamp, void *user)
{
    (void)timestamp;
    auto &userData = *static_cast<CallbackHelper *>(user);
    userData.level = level;
    userData.fileName = file;
    userData.line = line;
    userData.msg = msg;
}

TEST_CASE("logger using lambda callback")
{
    CallbackHelper userData;

    scorbit::addLoggerCallback(
            [&userData](std::string_view msg, scorbit::LogLevel level, const char *file, int line,
                        int64_t timestamp) {
                userData.level = level;
                userData.fileName = file;
                userData.line = line;
                userData.msg = msg;
            }, 512);
    INF("Hello");
    sleepForLogger();
    int line = __LINE__ - 2;
    const char *file = __FILE__;
    CHECK(userData.msg == "Hello");
    CHECK(userData.level == scorbit::LogLevel::Info);
    CHECK(userData.fileName == file + std::string_view(file).find_last_of("/\\") + 1);
    CHECK(userData.line == line);

    resetLogger();
}

TEST_CASE("logger using bind callback")
{
    CallbackHelper userData;

    scorbit::addLoggerCallback(std::bind(callback, std::placeholders::_1, std::placeholders::_2,
                                         std::placeholders::_3, std::placeholders::_4,
                                         std::placeholders::_5, &userData), 512);
    INF("Hello");
    sleepForLogger();
    int line = __LINE__ - 2;
    const char *file = __FILE__;
    CHECK(userData.msg == "Hello");
    CHECK(userData.level == scorbit::LogLevel::Info);
    CHECK(userData.fileName == file + std::string_view(file).find_last_of("/\\") + 1);
    CHECK(userData.line == line);

    resetLogger();
}

TEST_CASE("logger multithread")
{
    std::vector<std::string> logs;
    scorbit::addLoggerCallback([&logs](std::string_view msg, scorbit::LogLevel, const char *, int,
                                       int64_t) { logs.emplace_back(msg); }, 512);

    // Set up the random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 10); // Random duration between 1 and 10 milliseconds

    constexpr auto numLogs = 20;

    std::vector<std::thread> threads;
    for (int i = 0; i < numLogs; ++i) {
        threads.emplace_back([i, &dist, &gen]() {
            INF("Hello world {}", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    // Generate the test logs
    std::vector<std::string> testLogs;
    for (int i = 0; i < numLogs; ++i) {
        testLogs.emplace_back(fmt::format("Hello world {}", i));
    }

    using namespace Catch::Matchers;
    CHECK_THAT(logs, UnorderedEquals(testLogs));

    resetLogger();
}

namespace {

// Unregisters on any exit, so a failed REQUIRE cannot leave a callback holding dangling locals.
struct ResetLoggerOnExit {
    ~ResetLoggerOnExit() { resetLogger(); }
};

} // namespace

TEST_CASE("resetLogger waits for a callback that is already running")
{
    ResetLoggerOnExit guard;
    std::promise<void> entered;
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<bool> blockedOnce {false};
    std::atomic<bool> running {false};

    scorbit::addLoggerCallback(
            [&](std::string_view, scorbit::LogLevel, const char *, int, int64_t) {
                running = true;
                if (!blockedOnce.exchange(true)) {
                    entered.set_value();
                    released.wait();
                }
                running = false;
            },
            512);
    INF("block the dispatcher");
    REQUIRE(entered.get_future().wait_for(5s) == std::future_status::ready);

    auto reset = std::async(std::launch::async, [] { resetLogger(); });
    CHECK(reset.wait_for(100ms) == std::future_status::timeout);

    release.set_value();
    REQUIRE(reset.wait_for(5s) == std::future_status::ready);
    CHECK_FALSE(running);
}

TEST_CASE("a logger callback may reset the logger itself")
{
    ResetLoggerOnExit guard;
    std::promise<void> done;
    std::atomic<bool> once {false};

    scorbit::addLoggerCallback(
            [&](std::string_view, scorbit::LogLevel, const char *, int, int64_t) {
                if (!once.exchange(true)) {
                    sb_reset_logger(); // the C call: resetLogger() would free this very callback
                    done.set_value();
                }
            },
            512);
    INF("reset from inside");
    CHECK(done.get_future().wait_for(5s) == std::future_status::ready);
}

TEST_CASE("callbacks added during a dispatch do not disturb it")
{
    ResetLoggerOnExit guard;
    std::promise<void> entered;
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<int> calls {0};

    scorbit::addLoggerCallback(
            [&](std::string_view, scorbit::LogLevel, const char *, int, int64_t) {
                if (calls.fetch_add(1) == 0) {
                    entered.set_value();
                    released.wait();
                }
            },
            512);
    INF("first");
    REQUIRE(entered.get_future().wait_for(5s) == std::future_status::ready);

    for (int i = 0; i < 64; ++i) { // enough to reallocate the callback list mid-dispatch
        scorbit::addLoggerCallback(
                [](std::string_view, scorbit::LogLevel, const char *, int, int64_t) {}, 512);
    }
    release.set_value();

    INF("second");
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (calls < 2 && std::chrono::steady_clock::now() < deadline) {
        sleepForLogger();
    }
    CHECK(calls >= 2);
}
