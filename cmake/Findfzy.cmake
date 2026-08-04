# Findfzy.cmake — Repository-local find module for the vendored fzy match component.
#
# Purpose:
#   Discovers, validates, builds, and exposes the vendored fzy match component
#   as a static library with strict isolation boundaries.
#
# Public include contract:
#   Consumers include <fzy/match.h> to access the public API.
#
# Isolation boundary:
#   Only thirdparty/fzy/src/match.c is compiled — no executable entry points,
#   terminal UI, choices, options, or test sources are included.
#
# To add a new backend consumer:
#   1. Call find_package(fzy REQUIRED) in your CMakeLists.txt.
#   2. Link against the `fzy` target via target_link_libraries(... fzy).

if(TARGET fzy)
    return()
endif()

# --- Validate required vendored inputs ---------------------------------------

set(FZY_MATCH_C "${CMAKE_SOURCE_DIR}/thirdparty/fzy/src/match.c")
set(FZY_MATCH_H "${CMAKE_SOURCE_DIR}/thirdparty/fzy/src/match.h")
set(FZY_BONUS_H "${CMAKE_SOURCE_DIR}/thirdparty/fzy/src/bonus.h")
set(FZY_CONFIG_DEF_H "${CMAKE_SOURCE_DIR}/thirdparty/fzy/src/config.def.h")

foreach(_fzy_file IN ITEMS FZY_MATCH_C FZY_MATCH_H FZY_BONUS_H FZY_CONFIG_DEF_H)
    if(NOT EXISTS "${${_fzy_file}}")
        message(FATAL_ERROR "fzy vendored source incomplete: ${${_fzy_file}}")
    endif()
endforeach()

# --- Prepare build-tree directories -------------------------------------------

set(FZY_BUILD_DIR "${CMAKE_BINARY_DIR}/thirdparty/fzy")
set(FZY_INCLUDE_DIR "${FZY_BUILD_DIR}/includes")
file(MAKE_DIRECTORY "${FZY_BUILD_DIR}")
file(MAKE_DIRECTORY "${FZY_INCLUDE_DIR}")

# --- Generate private build-tree config.h from config.def.h -------------------

set(FZY_CONFIG_H "${FZY_BUILD_DIR}/config.h")
configure_file("${FZY_CONFIG_DEF_H}" "${FZY_CONFIG_H}")

# --- Copy the public header to the build-tree include root --------------------

file(MAKE_DIRECTORY "${FZY_INCLUDE_DIR}/fzy")
file(READ "${FZY_MATCH_H}" _fzy_match_h_content)
file(WRITE "${FZY_INCLUDE_DIR}/fzy/match.h" "${_fzy_match_h_content}")

# --- Define the fzy static library target -------------------------------------

add_library(fzy STATIC "${CMAKE_SOURCE_DIR}/thirdparty/fzy/src/match.c")

# --- Set target-scoped compilation requirements -------------------------------

# C99 language standard
target_compile_features(fzy PUBLIC c_std_99)

# Upstream version macro and GNU source extension
target_compile_definitions(fzy PRIVATE MATCH_VERSION=1.1 _GNU_SOURCE)

# Warning policy
target_compile_options(fzy PRIVATE -Wall -Wextra -pedantic -Werror=vla)

# --- Configure include directories with proper visibility ---------------------

target_include_directories(fzy PRIVATE
    # Generated config.h (private to match.c compilation)
    "${FZY_BUILD_DIR}"
    # bonus.h includes "../config.h" via relative path
    "${CMAKE_SOURCE_DIR}/thirdparty/fzy/src"
)

target_include_directories(fzy PUBLIC
    # Public consumer header root — consumers include <fzy/match.h>
    "${FZY_INCLUDE_DIR}"
)

# --- No executable-only link dependencies -------------------------------------
# fzy is a pure static library; no pthread or other executable-oriented flags.
