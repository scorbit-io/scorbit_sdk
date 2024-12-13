set(BOOS_INCLUDE_LIBRARIES "")
list(APPEND BOOST_INCLUDE_LIBRARIES "${BOOST_NOT_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED}")
list(APPEND BOOST_INCLUDE_LIBRARIES "${BOOST_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED}")

# url for 1.85.0 + only
set(BOOST_URL
    "https://github.com/boostorg/boost/releases/download/boost-${TRY_BOOST_VERSION}/boost-${TRY_BOOST_VERSION}-cmake.tar.xz"
)

set(Boost_USE_STATIC_LIBS ON) # only find static libs
set(Boost_USE_DEBUG_LIBS OFF) # ignore debug libs and
set(Boost_USE_RELEASE_LIBS ON) # only find release libs
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.30")
    cmake_policy(SET CMP0167 NEW)
endif()

CPMAddPackage(
    NAME Boost
    URL ${BOOST_URL}
    EXCLUDE_FROM_ALL YES
    OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES OFF"
)

set(BOOST_LIBS "") # Initialize an empty list to collect libraries

# Collect header-only Boost libraries
foreach(a_lib ${BOOST_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED})
    list(APPEND BOOST_LIBS boost_${a_lib})
endforeach()

# Collect non-header-only Boost libraries
foreach(a_lib ${BOOST_NOT_HEADER_ONLY_COMPONENTS_THAT_YOU_NEED})
    list(APPEND BOOST_LIBS Boost::${a_lib})
endforeach()
