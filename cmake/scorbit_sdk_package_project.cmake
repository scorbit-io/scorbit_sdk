# Fork of TheLartians/PackageProject.cmake install rules for scorbit_sdk only.
# Diff vs upstream: shared-library namelink (lib<name>.so) is installed with Runtime so the
# runtime .deb matches loaders that open "libscorbit_sdk.so".

function(scorbit_sdk_packageProject)
    if(POLICY CMP0177)
        cmake_policy(SET CMP0177 NEW)
    endif()
    include(CMakePackageConfigHelpers)
    include(GNUInstallDirs)

    cmake_parse_arguments(
        PROJECT
        ""
        "NAME;VERSION;INCLUDE_DIR;INCLUDE_DESTINATION;BINARY_DIR;COMPATIBILITY;EXPORT_HEADER;VERSION_HEADER;NAMESPACE;DISABLE_VERSION_SUFFIX;ARCH_INDEPENDENT;INCLUDE_HEADER_PATTERN;CPACK;RUNTIME_DESTINATION"
        "DEPENDENCIES"
        ${ARGN}
    )

    set(_SCORBIT_PP_VENDOR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package_project_vendor")

    if(PROJECT_DISABLE_VERSION_SUFFIX)
        unset(PROJECT_VERSION_SUFFIX)
    else()
        set(PROJECT_VERSION_SUFFIX -${PROJECT_VERSION})
    endif()

    if(NOT DEFINED PROJECT_COMPATIBILITY)
        set(PROJECT_COMPATIBILITY AnyNewerVersion)
    endif()

    if(DEFINED PROJECT_NAMESPACE)
        if(PROJECT_CPACK)
            set(CPACK_PACKAGE_NAMESPACE ${PROJECT_NAMESPACE})
        endif()
        set(PROJECT_NAMESPACE ${PROJECT_NAMESPACE}::)
        add_library(${PROJECT_NAMESPACE}${PROJECT_NAME} ALIAS ${PROJECT_NAME})
    endif()

    if(DEFINED PROJECT_VERSION_HEADER OR DEFINED PROJECT_EXPORT_HEADER)
        set(PROJECT_VERSION_INCLUDE_DIR ${PROJECT_BINARY_DIR}/PackageProjectInclude)

        if(DEFINED PROJECT_EXPORT_HEADER)
            include(GenerateExportHeader)
            generate_export_header(
                ${PROJECT_NAME}
                EXPORT_FILE_NAME ${PROJECT_VERSION_INCLUDE_DIR}/${PROJECT_EXPORT_HEADER}
            )
        endif()

        if(DEFINED PROJECT_VERSION_HEADER)
            unset(CMAKE_MATCH_1)
            unset(CMAKE_MATCH_3)
            unset(CMAKE_MATCH_5)
            unset(CMAKE_MATCH_7)

            string(REGEX MATCH "^([0-9]+)(\\.([0-9]+))?(\\.([0-9]+))?(\\.([0-9]+))?$" _ "${PROJECT_VERSION}")

            set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
            set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_3})
            set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_5})
            set(PROJECT_VERSION_TWEAK ${CMAKE_MATCH_7})

            if(NOT DEFINED PROJECT_VERSION_MAJOR)
                set(PROJECT_VERSION_MAJOR "0")
            endif()
            if(NOT DEFINED PROJECT_VERSION_MINOR)
                set(PROJECT_VERSION_MINOR "0")
            endif()
            if(NOT DEFINED PROJECT_VERSION_PATCH)
                set(PROJECT_VERSION_PATCH "0")
            endif()
            if(NOT DEFINED PROJECT_VERSION_TWEAK)
                set(PROJECT_VERSION_TWEAK "0")
            endif()

            string(TOUPPER ${PROJECT_NAME} UPPERCASE_PROJECT_NAME)
            string(REGEX REPLACE [^a-zA-Z0-9] _ UPPERCASE_PROJECT_NAME ${UPPERCASE_PROJECT_NAME})
            configure_file(
                ${_SCORBIT_PP_VENDOR}/version.h.in
                ${PROJECT_VERSION_INCLUDE_DIR}/${PROJECT_VERSION_HEADER}
                @ONLY
            )
        endif()

        get_target_property(target_type ${PROJECT_NAME} TYPE)
        if(target_type STREQUAL "INTERFACE_LIBRARY")
            set(VISIBILITY INTERFACE)
        else()
            set(VISIBILITY PUBLIC)
        endif()
        target_include_directories(
            ${PROJECT_NAME}
            ${VISIBILITY}
            "$<BUILD_INTERFACE:${PROJECT_VERSION_INCLUDE_DIR}>"
        )
        install(
            DIRECTORY ${PROJECT_VERSION_INCLUDE_DIR}/
            DESTINATION ${PROJECT_INCLUDE_DESTINATION}
            COMPONENT "${PROJECT_NAME}_Development"
        )
    endif()

    set(wbpvf_extra_args "")
    if(NOT DEFINED PROJECT_ARCH_INDEPENDENT)
        get_target_property(target_type "${PROJECT_NAME}" TYPE)
        if(target_type STREQUAL "INTERFACE_LIBRARY")
            set(PROJECT_ARCH_INDEPENDENT YES)
        endif()
    endif()

    if(PROJECT_ARCH_INDEPENDENT)
        set(wbpvf_extra_args ARCH_INDEPENDENT)
        set(INSTALL_DIR_FOR_CMAKE_CONFIGS ${CMAKE_INSTALL_DATADIR})
    else()
        set(INSTALL_DIR_FOR_CMAKE_CONFIGS ${CMAKE_INSTALL_LIBDIR})
    endif()

    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY ${PROJECT_COMPATIBILITY} ${wbpvf_extra_args}
    )

    if(NOT DEFINED PROJECT_RUNTIME_DESTINATION)
        set(PROJECT_RUNTIME_DESTINATION ${PROJECT_NAME}${PROJECT_VERSION_SUFFIX})
    endif()

    # NAMELINK_COMPONENT omitted: versionless lib<name>.so stays with LIBRARY (Runtime).
    install(
        TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        LIBRARY
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_RUNTIME_DESTINATION}
            COMPONENT "${PROJECT_NAME}_Runtime"
        ARCHIVE
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_RUNTIME_DESTINATION}
            COMPONENT "${PROJECT_NAME}_Development"
        RUNTIME
            DESTINATION ${CMAKE_INSTALL_BINDIR}/${PROJECT_RUNTIME_DESTINATION}
            COMPONENT "${PROJECT_NAME}_Runtime"
        BUNDLE
            DESTINATION ${CMAKE_INSTALL_BINDIR}/${PROJECT_RUNTIME_DESTINATION}
            COMPONENT "${PROJECT_NAME}_Runtime"
        PUBLIC_HEADER
            DESTINATION ${PROJECT_INCLUDE_DESTINATION}
            COMPONENT "${PROJECT_NAME}_Development"
        INCLUDES
        DESTINATION "${PROJECT_INCLUDE_DESTINATION}"
    )

    set(
        "${PROJECT_NAME}_INSTALL_CMAKEDIR"
        "${INSTALL_DIR_FOR_CMAKE_CONFIGS}/cmake/${PROJECT_NAME}${PROJECT_VERSION_SUFFIX}"
        CACHE PATH "CMake package config location relative to the install prefix"
    )

    mark_as_advanced("${PROJECT_NAME}_INSTALL_CMAKEDIR")

    if(NOT DEFINED PROJECT_DEPENDENCIES)
        set(PROJECT_DEPENDENCIES "")
    endif()
    configure_file(
        ${_SCORBIT_PP_VENDOR}/Config.cmake.in
        "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
        @ONLY
    )

    install(
        EXPORT ${PROJECT_NAME}Targets
        DESTINATION "${${PROJECT_NAME}_INSTALL_CMAKEDIR}"
        NAMESPACE ${PROJECT_NAMESPACE}
        COMPONENT "${PROJECT_NAME}_Development"
    )

    install(
        FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
              "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
        DESTINATION "${${PROJECT_NAME}_INSTALL_CMAKEDIR}"
        COMPONENT "${PROJECT_NAME}_Development"
    )

    if(NOT DEFINED PROJECT_INCLUDE_HEADER_PATTERN)
        set(PROJECT_INCLUDE_HEADER_PATTERN "*")
    endif()

    if(PROJECT_INCLUDE_DESTINATION AND PROJECT_INCLUDE_DIR)
        install(
            DIRECTORY ${PROJECT_INCLUDE_DIR}/
            DESTINATION ${PROJECT_INCLUDE_DESTINATION}
            COMPONENT "${PROJECT_NAME}_Development"
            FILES_MATCHING
            PATTERN "${PROJECT_INCLUDE_HEADER_PATTERN}"
        )
    endif()

    set(${PROJECT_NAME}_VERSION ${PROJECT_VERSION} CACHE INTERNAL "")
endfunction()
