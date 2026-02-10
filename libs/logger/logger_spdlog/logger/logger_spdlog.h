/*
 * Logger spdlog Implementation
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 */

#pragma once

#include <logger/logger.h>
#include <memory>
#include <string>

namespace spdlog {
class logger;
}

namespace logger {

using Flags = uint32_t;
enum Flag : Flags {
    NONE = 0x0000,
    SHOW_TIME = 0x0001,
    SHOW_FILE = 0x0002,
};

class Logger
{
public:
    static Logger *instance();

    void initLogger(LogLevel level, Flags flags, const std::string &fileName, size_t maxLogSize,
                    size_t maxLogFiles, size_t maxLogMessageLength = 512);

    void flush();

    void logImpl(const std::string &message, LogLevel level, const char *file, int line);

private:
    Logger() = default;

private:
    std::shared_ptr<spdlog::logger> m_logger;
    size_t m_maxLogMessageLength {512};
};

} // namespace logger
