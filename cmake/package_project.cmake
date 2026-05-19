# To silence warning in PackageProject
if(POLICY CMP0177)
    set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)
endif()
CPMAddPackage(
    NAME PackageProject
    URL https://github.com/TheLartians/PackageProject.cmake/archive/refs/tags/v1.13.0.tar.gz
    URL_HASH SHA256=2d124f7d2ff4991469a5ee3771459a8795a13c3d4fde9f7cecb767f174f7a545
)
