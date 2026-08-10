# qtest_add_test.cmake
# Registers QtTest test classes as CTest tests with DEF_SOURCE_LINE for VS Code Test Explorer.
#
# Usage:
#   qtest_add_test(
#     TARGET <executable_target_name>
#     SOURCES <source_file1.cpp> [<source_file2.cpp> ...]
#     PREFIX <prefix_string>       # optional, e.g. "backend.core"
#   )
#
# The function:
#   1. Reads each source file to find test class declarations (class XxxTest : public QObject).
#   2. Within each class, finds private slots methods matching void test_*( ).
#   3. Registers one CTest test per test method with DEF_SOURCE_LINE set to <absolute_path>:<line>.

function(find_cpp_method_definitions _file_path _out_results_var)
    if(NOT EXISTS "${_file_path}")
        message(FATAL_ERROR "File not found: ${_file_path}")
    endif()

    file(READ "${_file_path}" _file_content)
    # message(STATUS "find_cpp_method_definitions: Processing ${_file_path}")

    # 1. Normalize line endings (remove CR)
    string(REPLACE "\r" "" _file_content "${_file_content}")

    # 2. Mask actual semicolons with ASCII 0x01 (\x01) so CMake list mechanics ignore them
    string(REPLACE ";" "#" _safe_content "${_file_content}")

    # 3. Convert newlines into CMake list delimiters (;)
    string(REPLACE "\n" ";" _lines "${_safe_content}")

    set(_current_line_num 0)
    set(_matches "")

    # C++ Method Definition Regex Pattern
    set(_method_pattern "[ \t]*([a-zA-Z_0-9<>:*&]+[ \t]+)+([a-zA-Z_0-9:]+)::([a-zA-Z_0-9]+)[ \t]*\\(")

    foreach(_line IN LISTS _lines)
        math(EXPR _current_line_num "${_current_line_num} + 1")

        # 4. Restore original semicolons
        string(REPLACE "#" ";" _real_line "${_line}")
        # message(STATUS "find_cpp_method_definitions: Line ${_current_line_num}: ${_real_line}")

        if(_line MATCHES "${_method_pattern}")
            # Extract signature (everything before the opening brace '{')
            string(REGEX REPLACE "[ \t]*\\{[ \t]*$" "" _signature "${_real_line}")
            string(STRIP "${_signature}" _signature)

            list(APPEND _matches "${_current_line_num}:${_signature}")
            # message(STATUS "find_cpp_method_definitions: Found method at line ${_current_line_num}: ${_signature}")
        endif()
    endforeach()

    set(${_out_results_var} "${_matches}" PARENT_SCOPE)
endfunction()

function(qtest_add_test)
  cmake_parse_arguments(
    QT               # prefix
    ""               # options
    "SOURCE;PREFIX"  # one_value_keywords
    ""               # multi_value_keywords
    ${ARGN}
  )

  if(NOT QT_SOURCE OR QT_SOURCE STREQUAL "")
    message(FATAL_ERROR "qtest_add_test: SOURCE is required and must not be empty")
  endif()

  if(NOT QT_PREFIX)
    set(QT_PREFIX "")
  endif()

  # we want to construct the test target name as QT_PREFIX_SOURCE_FILENAME without dots 
  # and without extension, so we can use it as a CTest test name
  get_filename_component(file_stem "${QT_SOURCE}" NAME_WE)
  string(CONCAT QT_PREFIX "${QT_PREFIX}" "." "${file_stem}")
  string(REPLACE "." "_" test_target "${QT_PREFIX}")
  message(STATUS "qtest_add_test: Registering tests from ${QT_SOURCE} in target ${test_target} with prefix '${QT_PREFIX}'")

  qt6_add_executable(${test_target} ${QT_SOURCE})
  # target_include_directories(backend_core_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(${test_target} PRIVATE Qt6::Test dir2md_backend)
  set_target_properties(${test_target} PROPERTIES
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )

  # we need the source files to be absolute paths for the DEF_SOURCE_LINE property to work correctly
  if(NOT IS_ABSOLUTE "${QT_SOURCE}")
    get_filename_component(QT_SOURCE "${QT_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  if(NOT EXISTS "${QT_SOURCE}")
    message(FATAL_ERROR "qtest_add_test: Source file does not exist: ${QT_SOURCE}")
  endif()

  find_cpp_method_definitions("${QT_SOURCE}" my_methods)
  foreach(_entry IN LISTS my_methods)
    if(_entry MATCHES "^([0-9]+):.*[ \t:]+(test_[a-zA-Z_0-9]+)[ \t]*\\(")
      set(_line_num "${CMAKE_MATCH_1}")
      set(_func_name "${CMAKE_MATCH_2}")

      # Construct test name with optional prefix
      if(QT_PREFIX AND NOT QT_PREFIX STREQUAL "")
        set(_test_name "${QT_PREFIX}.${_func_name}")
      else()
        set(_test_name "${_func_name}")
      endif()

      # Construct DEF_SOURCE_LINE property value: "<absolute_path>:<line_number>"
      set(_source_line "${QT_SOURCE}:${_line_num}")

      # Add CTest test with DEF_SOURCE_LINE for VS Code Test Explorer integration
      add_test(
        NAME ${_test_name}
        COMMAND ${test_target} "${_func_name}"
      )

      set_property(TEST ${_test_name} PROPERTY DEF_SOURCE_LINE "${_source_line}")
    endif()
  endforeach()

  unset(_src_file)
  unset(_src_content)
  unset(_lines)
  unset(_line_count)
  unset(in_test_class)
  unset(in_private_slots)
  unset(class_name)
  unset(method_name)
  unset(_test_name)
  unset(_src_path_normalized)
  unset(_idx)
  unset(_line)
  unset(_match)
endfunction()
