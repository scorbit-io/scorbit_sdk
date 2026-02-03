/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2025
 *
 ****************************************************************************/

#include <utils/remount.h>
#include <utils/commandrunner.h>
#include <logger/logger.h>
#include <string>

namespace {

void runMount(const char *rw_option)
{
    constexpr auto command = "mount";
    const auto res = utils::runCommandAndGetOutput(
            command, {"-o", std::string {"remount,"} + rw_option, "/"});
    if (res.retCode != 0) {
        ERR("Error executing command mount -o remount,{} / : exit code {}, output:\n{}", rw_option,
            res.retCode, res.output);
    }
}

} // namespace

namespace utils {

void remountRootRO()
{
    runMount("ro");
}

void remountRootRW()
{
    runMount("rw");
}

} // namespace utils
