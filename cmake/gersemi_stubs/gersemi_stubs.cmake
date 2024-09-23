
function(packageProject)
    cmake_parse_arguments(
      PROJECT
      ""
      "NAME;VERSION;INCLUDE_DIR;INCLUDE_DESTINATION;BINARY_DIR;COMPATIBILITY;EXPORT_HEADER;VERSION_HEADER;NAMESPACE;DISABLE_VERSION_SUFFIX;ARCH_INDEPENDENT;INCLUDE_HEADER_PATTERN;CPACK;RUNTIME_DESTINATION"
      "DEPENDENCIES"
      ${ARGN}
    )
endfunction()

function(CPMAddPackage)
    set(oneValueArgs
        NAME
        FORCE
        VERSION
        GIT_TAG
        DOWNLOAD_ONLY
        GITHUB_REPOSITORY
        GITLAB_REPOSITORY
        BITBUCKET_REPOSITORY
        GIT_REPOSITORY
        SOURCE_DIR
        FIND_PACKAGE_ARGUMENTS
        NO_CACHE
        SYSTEM
        GIT_SHALLOW
        EXCLUDE_FROM_ALL
        SOURCE_SUBDIR
        CUSTOM_CACHE_KEY
    )
    set(multiValueArgs URL OPTIONS DOWNLOAD_COMMAND PATCHES)
    cmake_parse_arguments(CPM_ARGS "" "${oneValueArgs}" "${multiValueArgs}" "${ARGN}")
endfunction()

function(catch_discover_tests TARGET)
    cmake_parse_arguments(
      ""
      ""
      "TEST_PREFIX;TEST_SUFFIX;WORKING_DIRECTORY;TEST_LIST;REPORTER;OUTPUT_DIR;OUTPUT_PREFIX;OUTPUT_SUFFIX;DISCOVERY_MODE"
      "TEST_SPEC;EXTRA_ARGS;PROPERTIES;DL_PATHS;DL_FRAMEWORK_PATHS"
      ${ARGN}
    )
endfunction()
