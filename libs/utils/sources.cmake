set(headers
    include/utils/toupper.h
    include/utils/utils.h
    include/utils/bytearray.h
    include/utils/flags.h
    include/utils/fs_read_write.h
)

set(sources
    source/toupper.cpp
    source/utils.cpp
    source/bytearray.cpp
    source/fs_read_write.cpp
)

# commandrunner is POSIX-only and excluded on Windows rather than ported.
#
# It is built on the POSIX process model -- fork(), waitpid(), WIFEXITED(),
# setpriority() and <sys/resource.h> -- which Windows has no equivalent of.
# Guarding the includes is not enough; pid_t and fork go with them.
#
# Excluding it costs this build nothing. Nothing in the SDK calls runCommand,
# runCommandAndGetResult or runCommandAndGetOutput; libs/utils is compiled on
# Windows at all only because libs/tpm/CMakeLists.txt adds it. The sole consumer
# anywhere is scorbitd's install_package.cpp, which runs "/bin/sh" -- the Linux
# self-update path, and not a Windows story either. There are no tests for it.
#
# The header goes with the source deliberately, so a Windows caller fails at
# compile with an undeclared function rather than at link with an unresolved
# symbol. That is the error that says what is actually wrong.
#
# If Windows ever needs this, the rewrite is already half-present: the file
# includes cpp-subprocess, which supports Windows. That is a port, not a guard,
# and it belongs to whoever has a caller that needs it. See SB-4861.
if(NOT WIN32)
    list(APPEND headers include/utils/commandrunner.h)
    list(APPEND sources source/commandrunner.cpp)
endif()
