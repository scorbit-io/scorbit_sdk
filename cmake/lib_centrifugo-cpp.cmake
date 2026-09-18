CPMAddPackage(
    NAME centrifugo-cpp
    URL https://github.com/scorbit-io/centrifugo-cpp/archive/refs/tags/v0.8.1.tar.gz
    URL_HASH SHA256=b755d2401adc5043025e67b92d74d9319d461d640315a4cb97b527ce5f13dfb6
)

# centrifugo-cpp's own CMakeLists.txt applies `-Wall -Wextra` unconditionally.
# cl accepts -Wall (it maps to /Wall) but reads -Wextra as /W followed by a
# number, and stops before compiling anything:
#
#   cl : command line error D8021: invalid numeric argument '/Wextra'
#
# Stripped here rather than patched, deliberately. A CPM PATCHES entry would have
# to apply ON MSVC, and that is precisely the case this repo already learned not
# to rely on: the patch.exe a Windows host provides is commonly GNU patch 2.5.9
# from Strawberry Perl, which aborts on a diff instead of failing cleanly and
# takes the configure step with it (see lib_cpr.cmake and SB-4852).
#
# The filter is on the -W prefix rather than on "-Wextra" by name, so a future
# upstream warning flag does not reintroduce the same failure silently.
#
# centrifugo-cpp is our repository, so the real fix is to guard those flags
# upstream and bump the pin here; this keeps them on the compilers that
# understand them until that releases. See SB-4877.
if(MSVC AND TARGET centrifugo-cpp)
    get_target_property(_centrifugo_cpp_opts centrifugo-cpp COMPILE_OPTIONS)
    if(_centrifugo_cpp_opts)
        list(FILTER _centrifugo_cpp_opts EXCLUDE REGEX "^-W")
        set_target_properties(
            centrifugo-cpp
            PROPERTIES COMPILE_OPTIONS "${_centrifugo_cpp_opts}"
        )
    endif()
endif()
