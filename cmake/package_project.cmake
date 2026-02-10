# To silence warning in PackageProject
if(POLICY CMP0177)
    set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)
endif()
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.13.0")
