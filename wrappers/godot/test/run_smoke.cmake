# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# Runs smoke_test.gd in a fresh Godot project holding the staged add-on.
#   -DGODOT=<godot binary> -DPROJECT_DIR=<scratch dir> -DADDON_DIR=<staged addons/scorbit>
#   -DSOURCE_DIR=<this dir> [-DWITHOUT_NATIVE=ON: drop the native libraries first]
#   [-DSDK_COPY=fresh|corrupt: how smoke_test.gd prepares the SDK copy in the user folder]
#   -DCASE=<name>: the project's name, so each case has a user folder of its own

file(REMOVE_RECURSE ${PROJECT_DIR})
file(MAKE_DIRECTORY ${PROJECT_DIR}/addons)
file(COPY ${ADDON_DIR} DESTINATION ${PROJECT_DIR}/addons)
if(WITHOUT_NATIVE)
    file(REMOVE_RECURSE ${PROJECT_DIR}/addons/scorbit/bin)
    set(_expect_native false)
else()
    set(_expect_native true)
endif()
file(COPY ${SOURCE_DIR}/smoke_test.gd DESTINATION ${PROJECT_DIR})
file(
    WRITE ${PROJECT_DIR}/project.godot
    "config_version=5\n\n[application]\n\nconfig/name=\"scorbit_smoke_${CASE}\"\n"
)

# A project loads the extensions listed in .godot/extension_list.cfg, which the editor writes on
# the first import. Written here instead: Godot 4.7.2 crashes at the end of a first *headless*
# import whenever a GDExtension registers a class (even a trivial one), and this avoids it.
if(NOT WITHOUT_NATIVE)
    file(
        WRITE ${PROJECT_DIR}/.godot/extension_list.cfg
        "res://addons/scorbit/scorbit.gdextension\n"
    )
endif()
execute_process(
    COMMAND ${GODOT} --headless --path ${PROJECT_DIR} --import
    RESULT_VARIABLE _import_result
)
if(NOT _import_result EQUAL 0)
    message(FATAL_ERROR "Godot import failed: ${_import_result}")
endif()

execute_process(
    COMMAND ${GODOT} --headless --path ${PROJECT_DIR} -s res://smoke_test.gd -- ${_expect_native}
            ${SDK_COPY}
    RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "smoke_test.gd failed: ${_result}")
endif()
