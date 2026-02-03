/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#include <utils/utils.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>

#if defined(HAVE_LIBSYSTEMD)
#    include <systemd/sd-daemon.h>
#endif

#include <logger/logger.h>

using namespace std;
using namespace nlohmann;

namespace utils {

void pingWatchdog()
{
#if defined(HAVE_LIBSYSTEMD)
    // Notify systemd watchdog
    sd_notify(0, "WATCHDOG=1");
#endif
}

json readJsonFile(const string &jsonFilename)
{
    json jsonObj;
    ifstream ifs(jsonFilename);

    if (ifs.is_open()) {
        try {
            jsonObj = json::parse(ifs, nullptr,
                                  true, // allow exceptions
                                  true  // allow comments (JWCC mode)
            );
        } catch (nlohmann::detail::parse_error &e) {
            ERR("Can't parse {} {}", jsonFilename, e.what());
            std::cerr << "Can't parse " << jsonFilename << " " << e.what() << std::endl;
        }
    }
    return jsonObj;
}

bool writeJsonFile(const string &jsonFilename, const json &jsonObj)
{
    ofstream ofs(jsonFilename, std::ios::trunc);
    if (!ofs.is_open()) {
        ERR("Can't write to {}", jsonFilename);
        return false;
    }

    ofs << std::setw(4) << jsonObj << std::endl;
    return true;
}

} // utils
