/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/log_c.h>
#include <scorbit_sdk/log.h>

using namespace scorbit;

void sb_add_logger_callback(sb_log_callback_t callback, void *userData)
{
    addLoggerCallback(
            [callback](const std::string &message, LogLevel level, const char *file, int line,
                       void *user) {
                callback(message.c_str(), static_cast<sb_log_level_t>(level), file, line, user);
            },
            userData);
}

void sb_reset_logger(void)
{
    resetLogger();
}
