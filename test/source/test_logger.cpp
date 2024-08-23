/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <logger.h>
#include <scorbit_sdk/log.h>

#include <catch2/catch_test_macros.hpp>

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
