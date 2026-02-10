/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <future>

namespace utils {

struct CommandResult {
    int retCode {-1};
    std::string output;
};

extern std::future<int> runCommand(const std::string &command,
                                   const std::vector<std::string> &parameters);
extern int runCommandAndGetResult(const std::string &command,
                                  const std::vector<std::string> &parameters = {});

CommandResult runCommandAndGetOutput(const std::string &command,
                                     const std::vector<std::string> &parameters);

} // namespace utils
