set "VCPKG_ROOT=C:\Development\tools\vcpkg"
set "CMAKE_PATH=C:\Qt\Tools\CMake_64\bin"
set "NINJA_PATH=C:\Qt\Tools\Ninja"
set "QT6_PATH=C:\Qt\6.11.1\msvc2022_64\bin"

pushd .
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
popd

set "PATH=%VCPKG_ROOT%;%CMAKE_PATH%;%NINJA_PATH%;%QT6_PATH%;%PATH%"

code .