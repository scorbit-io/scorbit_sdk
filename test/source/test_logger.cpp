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

TEST_CASE("logger using functor object")
{
    struct myLogger {
        void operator()(std::string_view msg, scorbit::LogLevel level, const char *file, int line)
        {
            l = level;
            f = file;
            ln = line;
            s = msg;
        }

        std::string s;
        scorbit::LogLevel l;
        std::string f;
        int ln;
    };

    myLogger l;

    SECTION("registerLogger")
    {
        scorbit::registerLogger(std::ref(l));
        INF("Hello");
        int line = __LINE__ - 1;
        const char *file = __FILE__;
        CHECK(l.s == "Hello");
        CHECK(l.l == scorbit::LogLevel::Info);
        CHECK(l.f == file + std::string_view(file).find_last_of("/\\") + 1); // extract filename
        CHECK(l.ln == line);

        // After unregister log it does no longer log (in the function object it's still old values)
        scorbit::unregisterLogger();
        INF("new logs");
        CHECK(l.s == "Hello");
        CHECK(l.ln == line);
    }

    SECTION("Log levels")
    {
        scorbit::registerLogger(std::ref(l));
        DBG("Debug");
        CHECK(l.l == scorbit::LogLevel::Debug);

        INF("Info");
        CHECK(l.l == scorbit::LogLevel::Info);

        WRN("Warn");
        CHECK(l.l == scorbit::LogLevel::Warn);

        ERR("Error");
        CHECK(l.l == scorbit::LogLevel::Error);
    }
}

