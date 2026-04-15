# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# Define BOOST_COMPILED_COMPONENTS and BOOST_HEADER_ONLY_COMPONENTS lists
# then include this module and in for the target add ${BOOST_LIBS}:
# target_link_libraries(${PROJECT_NAME} PRIVATE ${BOOST_LIBS})

# Author: Dilshod Mukhtarov, 2025

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

set(TRY_BOOST_VERSION 1.90.0) # Set the desired Boost version here for fetching by CPM

# Don't use find_package if boost was added by CPM, otherwise can't find include files
if(NOT BOOST_ADDED_BY_CPM)
    find_package(Boost COMPONENTS ${BOOST_COMPILED_COMPONENTS} QUIET)
endif()

if(Boost_FOUND)
    set(BOOST_ADDED_BY_CPM OFF)
    message(STATUS "Using system Boost version ${Boost_VERSION}")
    set(BOOST_LIBS ${Boost_LIBRARIES})
else()
    set(BOOST_ADDED_BY_CPM ON)
    message(STATUS "System Boost not found, fetching Boost ${TRY_BOOST_VERSION} using CPM.")

    set(Boost_USE_STATIC_LIBS ON) # only find static libs
    set(Boost_USE_DEBUG_LIBS OFF) # ignore debug libs and
    set(Boost_USE_RELEASE_LIBS ON) # only find release libs
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME OFF)

    # url for 1.85.0 + only
    set(BOOST_URL
        "https://github.com/boostorg/boost/releases/download/boost-${TRY_BOOST_VERSION}/boost-${TRY_BOOST_VERSION}-cmake.tar.xz"
    )

    # Apple thin slices: host CMAKE_SYSTEM_PROCESSOR can disagree with CMAKE_OSX_ARCHITECTURES.
    # Set Boost.Context cache before Boost configures (see boost/libs/context/CMakeLists.txt).
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        set(_boost_osx_archs "${CMAKE_OSX_ARCHITECTURES}")
        list(LENGTH _boost_osx_archs _boost_osx_arch_count)
        if(_boost_osx_arch_count EQUAL 1)
            list(GET _boost_osx_archs 0 _boost_osx_single)
            if(_boost_osx_single STREQUAL "x86_64")
                set(BOOST_CONTEXT_ARCHITECTURE "x86_64" CACHE STRING "Boost.Context architecture" FORCE)
                set(BOOST_CONTEXT_ABI "sysv" CACHE STRING "Boost.Context ABI" FORCE)
            elseif(_boost_osx_single STREQUAL "arm64")
                set(BOOST_CONTEXT_ARCHITECTURE "arm64" CACHE STRING "Boost.Context architecture" FORCE)
                set(BOOST_CONTEXT_ABI "aapcs" CACHE STRING "Boost.Context ABI" FORCE)
            endif()
        elseif(_boost_osx_arch_count GREATER 1)
            # Multi-arch (universal) build: Boost.Context assembly is arch-specific and can't be
            # compiled for multiple architectures in one pass. Use the portable ucontext fallback.
            set(BOOST_CONTEXT_IMPLEMENTATION "ucontext"
                CACHE STRING "Boost.Context implementation" FORCE)
        endif()
        unset(_boost_osx_archs)
        unset(_boost_osx_arch_count)
        unset(_boost_osx_single)
    endif()

    CPMAddPackage(
        NAME Boost
        URL ${BOOST_URL}
        EXCLUDE_FROM_ALL YES
        SYSTEM YES
        OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES OFF"
    )

    set(BOOST_LIBS "") # Initialize an empty list to collect libraries

    # Collect header-only Boost libraries
    foreach(a_lib ${BOOST_HEADER_ONLY_COMPONENTS})
        list(APPEND BOOST_LIBS boost_${a_lib})
    endforeach()

    # Collect compiled Boost libraries
    foreach(a_lib ${BOOST_COMPILED_COMPONENTS})
        list(APPEND BOOST_LIBS Boost::${a_lib})
    endforeach()
endif()
