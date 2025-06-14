# this file contains a list of tools that can be activated and downloaded on-demand each tool is
# enabled during configuration by passing an additional `-DUSE_<TOOL>=<VALUE>` argument to CMake

# only activate tools for top level project
if(NOT PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    return()
endif()

# Build with /MD in MSVC
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

# enables sanitizers support using the the `USE_SANITIZER` flag available values are: Address,
# Memory, MemoryWithOrigins, Undefined, Thread, Leak, 'Address;Undefined'
if(USE_SANITIZER OR USE_STATIC_ANALYZER)
    CPMAddPackage("gh:StableCoder/cmake-scripts#24.08.1")

    if(USE_SANITIZER)
        include(${cmake-scripts_SOURCE_DIR}/sanitizers.cmake)
    endif()

    if(USE_STATIC_ANALYZER)
        if("clang-tidy" IN_LIST USE_STATIC_ANALYZER)
            set(CLANG_TIDY ON CACHE INTERNAL "")
        else()
            set(CLANG_TIDY OFF CACHE INTERNAL "")
        endif()
        if("iwyu" IN_LIST USE_STATIC_ANALYZER)
            set(IWYU ON CACHE INTERNAL "")
        else()
            set(IWYU OFF CACHE INTERNAL "")
        endif()
        if("cppcheck" IN_LIST USE_STATIC_ANALYZER)
            set(CPPCHECK ON CACHE INTERNAL "")
        else()
            set(CPPCHECK OFF CACHE INTERNAL "")
        endif()

        include(${cmake-scripts_SOURCE_DIR}/tools.cmake)

        if(${CLANG_TIDY})
            clang_tidy(${CLANG_TIDY_ARGS})
        endif()

        if(${IWYU})
            include_what_you_use(${IWYU_ARGS})
        endif()

        if(${CPPCHECK})
            cppcheck(${CPPCHECK_ARGS})
        endif()
    endif()
endif()

# enables CCACHE support through the USE_CCACHE flag possible values are: YES, NO or equivalent
if(USE_CCACHE)
    CPMAddPackage("gh:TheLartians/Ccache.cmake@1.2.5")
endif()
