/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "scorbit_sdk/log.h"
#include "logger.h"

namespace scorbit {

void registerLogger(logger_function_t &&loggerFunction)
{
    logger()->registerLogger(std::move(loggerFunction));
}

void unregisterLogger()
{
    logger()->unregisterLogger();
}

} // namespace scorbit
