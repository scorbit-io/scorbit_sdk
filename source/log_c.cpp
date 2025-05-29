/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/log_c.h>
#include "logger.h"

using namespace scorbit;

void sb_add_logger_callback(sb_log_callback_t callback, void *userData)
{

    detail::Logger::instance()->addCallback(
            [callback, userData](const std::string &message, LogLevel level, const char *file,
                                 int line, int64_t timestamp) {
                callback(message.c_str(), static_cast<sb_log_level_t>(level), file, line, timestamp,
                         userData);
            },
            userData);
}

void sb_reset_logger(void)
{
    detail::Logger::instance()->clear();
}
