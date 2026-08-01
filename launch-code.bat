set "VCPKG_ROOT=C:\Development\tools\vcpkg"
set "CMAKE_PATH=C:\Qt\Tools\CMake_64\bin"
set "NINJA_PATH=C:\Qt\Tools\Ninja"

set "PATH=%VCPKG_ROOT%;%CMAKE_PATH%;%NINJA_PATH%;%PATH%"

pushd .
call "C:\Qt\6.11.1\llvm-mingw_64\bin\qtenv2.bat"
popd

code .