/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <logger.h>
#include <scorbit_sdk/log.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/trompeloeil.hpp>
#include <thread>
#include <random>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;

struct CallbackHelper {
    std::string msg;
    scorbit::LogLevel level;
    std::string file;
    int line;
};

TEST_CASE("logger using functor object callback")
{
    struct loggerFunctor {
        void operator()(std::string_view msg, scorbit::LogLevel level, const char *file, int line,
                        void *user)
        {
            (void)user;
            data.level = level;
            data.file = file;
            data.line = line;
            data.msg = msg;
        }

        CallbackHelper data;
    };

    loggerFunctor l;

    SECTION("registerLogger")
    {
        // We have to use std::ref to pass the reference to the object, otherwise we can't reach
        // the object's data as the object will be copied
        scorbit::registerLogger(std::ref(l));
        INF("Hello");
        int line = __LINE__ - 1;
        const char *file = __FILE__;
        CHECK(l.data.msg == "Hello");
        CHECK(l.data.level == scorbit::LogLevel::Info);
        CHECK(l.data.file == file + std::string_view(file).find_last_of("/\\") + 1);
        CHECK(l.data.line == line);

        // After unregister log it does no longer log (in the function object it's still old values)
        scorbit::unregisterLogger();
        INF("new logs");
        CHECK(l.data.msg == "Hello");
        CHECK(l.data.line == line);
    }

    SECTION("Log levels")
    {
        scorbit::registerLogger(std::ref(l));
        DBG("Debug");
        CHECK(l.data.level == scorbit::LogLevel::Debug);

        INF("Info");
        CHECK(l.data.level == scorbit::LogLevel::Info);

        WRN("Warn");
        CHECK(l.data.level == scorbit::LogLevel::Warn);

        ERR("Error");
        CHECK(l.data.level == scorbit::LogLevel::Error);
    }
}

struct MockCallback {
    MAKE_MOCK5(callbackFunc, void(std::string_view, scorbit::LogLevel, const char *, int, void *));
} mockCallback;

void callbackFunc(std::string_view msg, scorbit::LogLevel level, const char *file, int line,
                  void *user)
{
    mockCallback.callbackFunc(msg, level, file, line, user);
}

TEST_CASE("Logger with function callback using trompeloeil")
{
    using namespace trompeloeil;
    using namespace scorbit;

    SECTION("SectionName")
    {
        REQUIRE_CALL(mockCallback, callbackFunc(eq<std::string_view>("Hello"), eq(LogLevel::Info),
                                                _, _, eq(nullptr)));

        scorbit::registerLogger(callbackFunc);
        INF("Hello");

        // After unregister log it does no longer log (in the function object it's still old values)
        scorbit::unregisterLogger();
        INF("Hello");
    }
}

static void callback(std::string_view msg, scorbit::LogLevel level, const char *file, int line,
                     void *user)
{
    auto &userData = *static_cast<CallbackHelper *>(user);
    userData.level = level;
    userData.file = file;
    userData.line = line;
    userData.msg = msg;
}

TEST_CASE("logger using function callback")
{
    static CallbackHelper userData;

    scorbit::registerLogger(callback, &userData);
    INF("Hello");
    int line = __LINE__ - 1;
    const char *file = __FILE__;
    CHECK(userData.msg == "Hello");
    CHECK(userData.level == scorbit::LogLevel::Info);
    CHECK(userData.file == file + std::string_view(file).find_last_of("/\\") + 1);
    CHECK(userData.line == line);

    // After unregister log it does no longer log (in the function object it's still old values)
    scorbit::unregisterLogger();
    INF("new logs");
    CHECK(userData.msg == "Hello");
    CHECK(userData.line == line);
}

TEST_CASE("logger using lambda callback")
{
    CallbackHelper userData;

    scorbit::registerLogger([&userData](std::string_view msg, scorbit::LogLevel level,
                                        const char *file, int line, void *user) {
        (void)user;
        userData.level = level;
        userData.file = file;
        userData.line = line;
        userData.msg = msg;
    });
    INF("Hello");
    int line = __LINE__ - 1;
    const char *file = __FILE__;
    CHECK(userData.msg == "Hello");
    CHECK(userData.level == scorbit::LogLevel::Info);
    CHECK(userData.file == file + std::string_view(file).find_last_of("/\\") + 1);
    CHECK(userData.line == line);

    // After unregister log it does no longer log (in the function object it's still old values)
    scorbit::unregisterLogger();
    INF("new logs");
    CHECK(userData.msg == "Hello");
    CHECK(userData.line == line);
}

TEST_CASE("logger using bind callback")
{
    CallbackHelper userData;

    scorbit::registerLogger(std::bind(callback, std::placeholders::_1, std::placeholders::_2,
                                      std::placeholders::_3, std::placeholders::_4, &userData));
    INF("Hello");
    int line = __LINE__ - 1;
    const char *file = __FILE__;
    CHECK(userData.msg == "Hello");
    CHECK(userData.level == scorbit::LogLevel::Info);
    CHECK(userData.file == file + std::string_view(file).find_last_of("/\\") + 1);
    CHECK(userData.line == line);

    // After unregister log it does no longer log (in the function object it's still old values)
    scorbit::unregisterLogger();
    INF("new logs");
    CHECK(userData.msg == "Hello");
    CHECK(userData.line == line);
}

TEST_CASE("logger multithread")
{
    std::vector<std::string> logs;
    scorbit::registerLogger([&logs](std::string_view msg, scorbit::LogLevel, const char *, int,
                                    void *) { logs.emplace_back(msg); });

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
}
