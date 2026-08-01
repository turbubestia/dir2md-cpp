cmake_minimum_required(VERSION 3.21)
project(MyQtApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Coverage support (requires gcovr + LLVM coverage tools)
option(BUILD_COVERAGE "Enable code coverage instrumentation" OFF)
if(BUILD_COVERAGE)
    message(STATUS "Code coverage enabled")
    set(COVERAGE_FLAGS "-fprofile-instr-generate -fcoverage-mapping")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
endif()

# Find Qt6 modules installed via Qt Maintenance Tool
find_package(Qt6 REQUIRED COMPONENTS Core Network Quick QuickControls2)

add_subdirectory(src)