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

#include <logger.h>
#include <scorbit_sdk/log.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/trompeloeil.hpp>
#include <thread>
#include <random>
#include <chrono>

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
        scorbit::addLoggerCallback(std::ref(l));
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
        scorbit::addLoggerCallback(std::ref(l));
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

        scorbit::addLoggerCallback(callbackFunc);
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
            });
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
                                         std::placeholders::_5, &userData));
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
                                       int64_t) { logs.emplace_back(msg); });

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
