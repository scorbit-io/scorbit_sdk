/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "scorbit_sdk/log.h"
#include "logger.h"

namespace scorbit {

void addLoggerCallback(LoggerCallback &&callback, void *userData)
{
    detail::Logger::instance()->addCallback(std::move(callback), userData);
}

void resetLogger()
{
    detail::Logger::instance()->clear();
}

} // namespace scorbit
