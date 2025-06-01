# Define BOOST_COMPILED_COMPONENTS and BOOST_HEADER_ONLY_COMPONENTS lists
# then include this module and in for the target add ${BOOST_LIBS}:
# target_link_libraries(${PROJECT_NAME} PRIVATE ${BOOST_LIBS})

# Author: Dilshod Mukhtarov, 2025

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(TRY_BOOST_VERSION 1.88.0) # Set the desired Boost version here for fetching by CPM

set(Boost_USE_STATIC_LIBS ON) # only find static libs
set(Boost_USE_DEBUG_LIBS OFF) # ignore debug libs and
set(Boost_USE_RELEASE_LIBS ON) # only find release libs
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

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

    # url for 1.85.0 + only
    set(BOOST_URL
        "https://github.com/boostorg/boost/releases/download/boost-${TRY_BOOST_VERSION}/boost-${TRY_BOOST_VERSION}-cmake.tar.xz"
    )

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
