cmake_minimum_required(VERSION 3.21)
project(MyQtApp LANGUAGES C CXX)

set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/dist/${CMAKE_BUILD_TYPE}" CACHE PATH "Install prefix" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add custom CMake module path for qtest_add_test and other helpers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Coverage support (requires gcovr + LLVM coverage tools)
option(BUILD_COVERAGE "Enable code coverage instrumentation" OFF)
if(BUILD_COVERAGE)
    message(STATUS "Code coverage enabled")
    set(COVERAGE_FLAGS "-fprofile-instr-generate -fcoverage-mapping")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
endif()

# Find Qt6 modules installed via Qt Maintenance Tool
find_package(Qt6 REQUIRED COMPONENTS Core Network Quick QuickControls2 Test)
find_package(fzy REQUIRED)

# Source folders
add_subdirectory(src)

include(CTest)
enable_testing()

add_subdirectory(test)

# Post-install: deploy Qt runtime once for all executables
include(deploy-qt)
deploy_qt_targets("${CMAKE_INSTALL_PREFIX}/bin" dir2md_frontend dir2md_cli)