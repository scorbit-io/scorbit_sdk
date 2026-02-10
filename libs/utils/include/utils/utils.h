/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace utils {

extern void pingWatchdog();

extern nlohmann::json readJsonFile(const std::string &jsonFilename);
extern bool writeJsonFile(const std::string &jsonFilename, const nlohmann::json &jsonObj);

} // namespace utils
