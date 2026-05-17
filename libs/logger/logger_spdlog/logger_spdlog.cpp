/*
 * Logger spdlog Implementation
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 */

#include "detail/cut_long_string.h"
#include <logger/logger_spdlog.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <vector>
#include <iostream>

namespace {

constexpr auto GZIP_EXT = "gz";

#if defined(__GNUC__) || defined(__clang__)
#    define LIKELY(x) __builtin_expect(!!(x), 1)
#    define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#    define LIKELY(x) (x)
#    define UNLIKELY(x) (x)
#endif

constexpr spdlog::level::level_enum toSpdlogLevel(logger::LogLevel level)
{
    switch (level) {
    case logger::LogLevel::Debug:
        return spdlog::level::debug;
    case logger::LogLevel::Info:
        return spdlog::level::info;
    case logger::LogLevel::Warn:
        return spdlog::level::warn;
    case logger::LogLevel::Error:
        return spdlog::level::err;
    default:
        return spdlog::level::info;
    }
}

void logCompressorCallback(spdlog::filename_t filename)
{
}

} // namespace

namespace logger {

Logger *Logger::instance()
{
    static Logger logger;
    return &logger;
}

void Logger::initLogger(LogLevel level, Flags flags, const std::string &fileName, size_t maxLogSize,
                        size_t maxLogFiles, size_t maxLogMessageLength)
{
    m_maxLogMessageLength = maxLogMessageLength;
    const auto fileLine = (flags & SHOW_FILE) ? "\t[file://%s:%#]" : std::string {};
    const auto timestamp = (flags & SHOW_TIME) ? "[%Y-%m-%d %T.%e] [%^%4!l%$] " : std::string {};
    const auto pattern = fmt::format("{}%v{}", timestamp, fileLine);

    std::vector<spdlog::sink_ptr> sinks;

    if (fileName.empty()) {
        // create a console output logger
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern(pattern);
        sinks.push_back(consoleSink);
    } else {
        try {
            // create a file rotating logger with 5mb size max and 3 rotated files (multi threaded)
            auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    fileName, maxLogSize, maxLogFiles - 1, spdlog::filename_t {GZIP_EXT},
                    logCompressorCallback);
            fileSink->set_pattern(pattern);
            sinks.push_back(fileSink);
        } catch (const spdlog::spdlog_ex &ex) {
            std::cerr << "Logger initialization failed: " << ex.what();
        }
    }

    spdlog::drop("logger"); // Remove previous logger from registry if it exists
    m_logger = std::make_shared<spdlog::logger>("logger", begin(sinks), end(sinks));
    m_logger->set_level(toSpdlogLevel(level));
    m_logger->flush_on(spdlog::level::warn);

    spdlog::register_logger(m_logger); // Add to spdlog registry, so flush_every() can access it
    spdlog::flush_every(std::chrono::seconds(1));
}

void Logger::flush()
{
    if (m_logger) {
        m_logger->flush();
    }
}

void Logger::logImpl(const std::string &message, LogLevel level, const char *file, int line)
{
    if (LIKELY(m_logger)) {
        // Already formatted by logMessage(); string_view hits spdlog's non-template log() so the
        // message is not routed through spdlog's log(loc, lvl, "{}", msg) fmt indirection.
        const spdlog::source_loc loc {file, line, nullptr};
        const auto lvl = toSpdlogLevel(level);
        if (LIKELY(message.length() <= m_maxLogMessageLength)) {
            m_logger->log(loc, lvl, spdlog::string_view_t {message.data(), message.size()});
        } else {
            const auto truncated = cutLongString(message, m_maxLogMessageLength);
            m_logger->log(loc, lvl, spdlog::string_view_t {truncated.data(), truncated.size()});
        }
    }
}

// Implementation of the interface log() function
void log(const std::string &message, LogLevel level, const char *file, int line)
{
    Logger::instance()->logImpl(message, level, file, line);
}

} // namespace logger
