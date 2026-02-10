/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */


#include "utils/commandrunner.h"
#include <logger/logger.h>
#include <cpp-subprocess/subprocess.hpp>

#include <sys/resource.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <future>

using namespace std;
using namespace subprocess;

constexpr int PROCESS_PRIORITY = 9; // Least priority

namespace utils {

// Runs command and return future which will be set -1 in case of error
// or exit status of the command
std::future<int> runCommand(const std::string &command, const std::vector<std::string> &parameters)
{
    return std::async(std::launch::async, [=]() -> int {
        pid_t pid = fork();
        if (pid < 0) {
            ERR("Couldn't fork to run command {}, errno = {}", command, errno);
            return -1;
        }
        else if (pid == 0) {
            // This is child process

            vector<char *> arguments;
            arguments.reserve(parameters.size() + 2);

            arguments.push_back(const_cast<char *>(command.data()));
            for(const auto &s: parameters) {
                arguments.push_back(const_cast<char *>(s.data()));
            }
            arguments.push_back(nullptr);

            execvp(command.data(), arguments.data());

            ERR("Couldn't start {}, errno = {}", command, errno);
            _exit(1);
        }

        // This is parent process
        // set low priority to child process
        setpriority(PRIO_PROCESS, static_cast<id_t>(pid), PROCESS_PRIORITY);

        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }

        return -1;
    });
}

int runCommandAndGetResult(const std::string &command, const std::vector<std::string> &parameters)
{
    int result = 1;
    auto runResult = runCommand(command, parameters);
    if (runResult.valid()) {
        result = runResult.get();
    }

    return result;
}

CommandResult runCommandAndGetOutput(const string &command, const vector<string> &parameters)
{
    vector<string> args = parameters;
    args.insert(args.begin(), command);

    int retcode = -1;
    vector<char> charVec;
    try {
        auto popen = Popen(args, output {PIPE}, error {STDOUT}, shell {false});
        charVec = popen.communicate().first.buf;
        retcode = popen.retcode();
    } catch (const exception &error) {
        ERR("runCommandAndGetOutput: {} got exception: {}", command, error.what());
    }

    return CommandResult {retcode, std::string{charVec.data(), charVec.size()}};
}

} // namespace utils
