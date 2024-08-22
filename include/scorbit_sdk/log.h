/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include "log_types.h"

namespace scorbit {

extern void registerLogger(logger_function_t &&loggerFunction);
extern void unregisterLogger();

} // namespace scorbit
