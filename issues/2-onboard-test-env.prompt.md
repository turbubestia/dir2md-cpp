# Implementation Plan: 2-onboard-test-env

[Analysis Reference](./2-onboard-test-env.plan.analysis.md)

---

## Overview

This plan implements a QtTest-based test infrastructure for the dir2md-cpp project, enabling:
- Test discovery via CMake/CTest with VS Code Test Explorer integration
- Per-backend-module test executables (one binary per module group)
- Source-line navigation from Test Explorer to individual test methods
- Coverage-ready test execution

---

## Phase 1 — CMake Module: `qtest_add_test()`

**References:** Analysis Section 2 (Component & File Impact Map), Section 3 (Boundary & Edge Case Analysis)

**Goal:** Create the reusable CMake function that registers QtTest tests with VS Code Test Explorer properties.

### Step 1.1 — Implement `cmake/qtest_add_test.cmake`

Create the file `cmake/qtest_add_test.cmake` with the following content:

```cmake
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

function(qtest_add_test)
  cmake_parse_arguments(
    QT
    ""
    "TARGET;PREFIX"
    "SOURCES"
    ${ARGN}
  )

  if(NOT QT_TARGET)
    message(FATAL_ERROR "qtest_add_test: TARGET is required")
  endif()

  if(NOT QT_SOURCES OR QT_SOURCES STREQUAL "")
    message(FATAL_ERROR "qtest_add_test: SOURCES is required and must not be empty")
  endif()

  if(NOT QT_PREFIX)
    set(QT_PREFIX "")
  endif()

  # Read each source file and extract test methods with line numbers.
  foreach(_src_file IN LISTS QT_SOURCES)
    if(NOT IS_ABSOLUTE "${_src_file}")
      get_filename_component(_src_file "${_src_file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${_src_file}")
      message(FATAL_ERROR "qtest_add_test: Source file does not exist: ${_src_file}")
    endif()

    file(READ "${_src_file}" _src_content)

    # Split content into lines for line-number tracking.
    string(REGEX REPLACE "\n" ";" _lines "${_src_content}")

    # State machine: track whether we are inside a test class and its private slots block.
    set(in_test_class OFF)
    set(in_private_slots OFF)
    set(class_name "")

    foreach(_line_index RANGE 0 ${#_lines} 1)
      list(GET _lines ${_line_index} _line)

      # Detect test class declaration: class XxxTest : public QObject
      if(_line MATCHES "class\\s+(\\w+Test)\\s*:\\s*public\\s+QObject")
        set(in_test_class ON)
        string(REGEX MATCH "class\\s+(\\w+Test)" _match "${_line}")
        string(REGEX REPLACE "class\\s+(\\w+Test)" "\\1" class_name "${_match}")
        set(in_private_slots OFF)
        continue()
      endif()

      if(in_test_class)
        # Detect private slots: block (with optional colon, possibly with leading whitespace)
        if(_line MATCHES "^\\s*private\\s+slots:\\s*$")
          set(in_private_slots ON)
          continue()
        endif()

        # If we hit another access specifier before finding slots, this class has no test methods.
        if(_line MATCHES "^\\s*(public|protected)\\s+(slots:|:\\s*public)" OR
           _line MATCHES "^\\s*(public|protected)\\s+[^:]")
          set(in_test_class OFF)
          set(class_name "")
          continue()
        endif()

        # Detect closing brace of class (standalone "};")
        if(_line MATCHES "^\\s*}\\s*;")
          set(in_test_class OFF)
          set(class_name "")
          set(in_private_slots OFF)
          continue()
        endif()

        # Inside private slots, look for test method signatures: void test_xxx(...) or void test_xxx()
        if(in_private_slots AND _line MATCHES "^\\s*void\\s+(test_\\w+)\\s*\\(")
          string(REGEX REPLACE "^\\s*void\\s+(test_\\w+)\\s*\\(.*" "\\1" method_name "${_line}")

          # Build CTest test name.
          set(_test_name "${method_name}")
          if(QT_PREFIX)
            set(_test_name "${QT_PREFIX}.${method_name}")
          endif()

          # Register the CTest test.
          add_test(
            NAME "${_test_name}"
            COMMAND ${QT_TARGET}
          )

          # Set DEF_SOURCE_LINE for VS Code Test Explorer navigation.
          # Use forward slashes in path for cross-platform compatibility.
          string(REPLACE "\\" "/" _src_path_normalized "${_src_file}")
          set(_line_number ${_line_index})
          set_tests_properties("${_test_name}" PROPERTIES
            DEF_SOURCE_LINE "${_src_path_normalized}:${_line_number}"
          )
        endif()
      endif()
    endforeach()

    if(NOT in_test_class AND class_name STREQUAL "")
      # Normal — all classes processed.
    endif()
  endforeach()

  unset(_src_file)
  unset(_src_content)
  unset(_lines)
  unset(_line_index)
  unset(_line)
  unset(in_test_class)
  unset(in_private_slots)
  unset(class_name)
  unset(method_name)
  unset(_test_name)
  unset(_src_path_normalized)
  unset(_line_number)
endfunction()
```

**Key design decisions:**
- The function uses a line-by-line state machine (not regex on full file content) to correctly track `private slots:` blocks within test classes. This avoids false positives from methods outside slots.
- Test class names are detected by the pattern `class XxxTest : public QObject` — the `Test` suffix is the convention.
- Test methods are detected as `void test_*(` inside `private slots:` blocks only.
- Path normalization converts Windows backslashes to forward slashes for VS Code compatibility.
- If a source file has no test methods found, CMake emits no warning (silent skip) — this avoids noise during early development. The FATAL_ERROR on missing files handles the error case per Analysis Section 3.

**Exit Criterion:** File exists and contains a valid `qtest_add_test()` function definition.

---

## Phase 2 — Root CMakeLists.txt: Module Path Configuration

**References:** Analysis Section 2 (Component & File Impact Map — CMakeLists.txt root)

**Goal:** Make the `cmake/` directory discoverable so custom `.cmake` files can be included.

### Step 2.1 — Add CMAKE_MODULE_PATH to root `CMakeLists.txt`

In `CMakeLists.txt`, **before** the `add_subdirectory(test)` call, add:

```cmake
# Before: find_package(Qt6 REQUIRED ...)
# After find_package but before add_subdirectory calls:
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
```

The modified root `CMakeLists.txt` should look like:

```cmake
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
find_package(Qt6 REQUIRED COMPONENTS Core Network Quick QuickControls2 Test)

# Add custom CMake module path for qtest_add_test and other helpers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

add_subdirectory(src)
add_subdirectory(test)
```

**Exit Criterion:** `cmake --preset debug` configures without "module not found" errors.

---

## Phase 3 — Test Executable: `backend_core_test`

**References:** Analysis Section 2 (Component & File Impact Map — test/backend/core/), Section 3 (QtTest-Specific Edge Cases)

**Goal:** Create the test executable target that links QtTest and the backend library.

### Step 3.1 — Create `test/backend/core/CMakeLists.txt`

Create the file with:

```cmake
qt6_add_executable(backend_core_test
    main.cpp
    setting_manager_test.cpp
)

target_include_directories(backend_core_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/backend
)

target_link_libraries(backend_core_test PRIVATE
    Qt6::Test
    dir2md_backend
)

set_target_properties(backend_core_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Register tests with CTest + VS Code Test Explorer integration.
include(${CMAKE_SOURCE_DIR}/cmake/qtest_add_test.cmake)
qtest_add_test(
    TARGET backend_core_test
    SOURCES setting_manager_test.cpp
    PREFIX backend.core
)
```

**Key decisions:**
- `qt6_add_executable()` is used (Qt 6.11 is confirmed in CMakePresets.json).
- The test executable links `dir2md_backend` so it can exercise the real backend API during actual tests. For the POC, this link is harmless and validates the full integration path.
- `target_include_directories` points to `src/backend` so test files can `#include <backend/core/...>` headers (matching the existing include style in `core_schema.hpp`).

### Step 3.2 — Verify `test/backend/CMakeLists.txt` exists

The file already exists with `add_subdirectory(core)`. No changes needed. **Verify** it contains exactly this line.

**Exit Criterion:** `cmake --preset debug` configures successfully and CTest lists tests named `backend.core.*`.

---

## Phase 4 — Test Source Files: POC Implementation

**References:** Analysis Section 2 (Component & File Impact Map — main.cpp, setting_manager_test.cpp), Section 4 (Verification Checklist)

**Goal:** Create minimal but valid test files that exercise the QtTest infrastructure end-to-end.

### Step 4.1 — Implement `test/backend/core/main.cpp`

Create the file with:

```cpp
#include <QTest>

#include "setting_manager_test.cpp"

QTEST_MAIN(setting_manager_test)
```

**Explanation:** `QTEST_MAIN()` creates a `main()` function that runs ALL test classes found in the binary's linked translation units. By including `setting_manager_test.cpp` (not just the header), we ensure the test class definition is compiled into this translation unit and picked up by QtTest's internal registration. Only one `QTEST_MAIN()` call is needed per executable.

**Note on include style:** Including `.cpp` files directly is a standard QtTest pattern for small test binaries. It avoids the need for separate `.hpp` headers for each test class, keeping the test directory flat and simple.

### Step 4.2 — Implement `test/backend/core/setting_manager_test.cpp`

Create the file with:

```cpp
#include <QObject>
#include <QTest>
#include <QString>
#include <QVariant>

class setting_manager_test : public QObject {
    Q_OBJECT

private slots:
    // POC: verify test infrastructure works (no dependency on SettingsManager).
    void test_basic_assertion()
    {
        QVERIFY(true);
        QCOMPARE(1 + 1, 2);
    }

    // Test QString functionality to validate QtTest integration.
    void test_string_operations()
    {
        QString str = "dir2md";
        QCOMPARE(str.length(), 6);
        QVERIFY(str.startsWith("dir"));
        QVERIFY(!str.endsWith("xyz"));
    }

    // Test QVariant basic operations (relevant to SettingsManager API).
    void test_variant_types()
    {
        QVariant intVal(42);
        QCOMPARE(intVal.toInt(), 42);

        QVariant boolVal(true);
        QVERIFY(boolVal.toBool());

        QVariant strVal("hello");
        QCOMPARE(strVal.toString(), QString("hello"));
    }

    // Test that SettingsManager can be instantiated (integration POC).
    void test_settings_manager_instantiation()
    {
        // SettingsManager inherits QObject, so it requires a parent or will leak.
        // Use a local QObject as parent to validate construction.
        QObject parent;
        dir2md::backend::SettingsManager manager(&parent);

        QVERIFY(&manager != nullptr);
        QCOMPARE(manager.parent(), &parent);
    }
};

#include "setting_manager_test.moc"
```

**Key decisions:**
- Class name is `setting_manager_test` (snake_case per repo convention, not `SettingManagerTest`).
- Four test methods cover: basic assertions, QString operations, QVariant types, and actual SettingsManager instantiation.
- The last method (`test_settings_manager_instantiation`) validates the real backend API without depending on schema registration or file I/O.
- `#include "setting_manager_test.moc"` at the bottom is required for Qt's MOC (Meta-Object Compiler) when the class is included from another translation unit (main.cpp). This is a standard Qt pattern.

**Exit Criterion:** All four test methods compile and pass when the test binary is run.

---

## Phase 5 — Build, Test & Verify

**References:** Analysis Section 4 (Verification Checklist)

**Goal:** Validate the entire infrastructure end-to-end.

### Step 5.1 — Configure

Run:
```powershell
cmake --preset debug
```

**Expected result:** Configuration succeeds with no errors or warnings about test targets, missing modules, or undefined references.

### Step 5.2 — Build

Run:
```powershell
cmake --build --preset debug
```

**Expected result:** The executable `backend_core_test.exe` is built in the build output directory (e.g., `build/cmake-debug/test/backend/core/backend_core_test.exe`). No compilation or linking errors.

### Step 5.3 — Run CTest

Run:
```powershell
cd build\cmake-debug
ctest --output-on-failure -V
```

**Expected result:**
- Four tests listed with names matching `backend.core.test_*`:
  - `backend.core.test_basic_assertion`
  - `backend.core.test_string_operations`
  - `backend.core.test_variant_types`
  - `backend.core.test_settings_manager_instantiation`
- All four pass (exit code 0).
- No failures or errors in output.

### Step 5.4 — Run Test Executable Directly

Run:
```powershell
.\test\backend\core\backend_core_test.exe
```

**Expected result:** Same four tests pass with output like:
```
********* Start testing of setting_manager_test *********
Config: Using QtTest library 6.11.1, version known (not final)...
PASS   setting_manager_test::test_basic_assertion()
PASS   setting_manager_test::test_string_operations()
PASS   setting_manager_test::test_variant_types()
PASS   setting_manager_test::test_settings_manager_instantiation()
Totals: 4 passed, 0 failed, 0 skipped, 0 botched, ...
********* Finished testing of setting_manager_test *********
```

### Step 5.5 — VS Code Test Explorer Verification

1. Open the project in VS Code.
2. Open the Testing view (sidebar icon with beaker/flask).
3. Expand the test hierarchy: `backend.core` > individual test methods.
4. Click on one test method (e.g., `test_basic_assertion`).
5. **Expected result:** The editor opens `test/backend/core/setting_manager_test.cpp` at the exact line of that test method (not the class declaration).

### Step 5.6 — Coverage Build Verification

Run:
```powershell
cmake --preset debug-coverage
cmake --build --preset debug-coverage
cd build\cmake-debug-coverage
ctest --output-on-failure
```

**Expected result:** Tests compile and run successfully with coverage instrumentation flags (`-fprofile-instr-generate -fcoverage-mapping`). `.profraw` files are produced in the test directory.

---

## Phase 6 — Validation Against Analysis Checklist

**References:** Analysis Section 4 (Verification Checklist)

Go through each checklist item:

| # | Checklist Item | Status |
|---|---|---|
| 1 | `cmake/qtest_add_test.cmake` defines `qtest_add_test()` and is includable via `include()` | [ ] |
| 2 | `cmake --preset debug` succeeds with no test target errors | [ ] |
| 3 | `backend_core_test.exe` is built in the output directory | [ ] |
| 4 | `ctest --output-on-failure` lists tests with `backend.core.` prefix | [ ] |
| 5 | POC test `setting_manager_test` passes (exit code 0) | [ ] |
| 6 | VS Code Test Explorer shows tests under `backend.core` hierarchy | [ ] |
| 7 | Clicking a test opens the editor at the correct method line | [ ] |
| 8 | No build errors from missing `test/cli/` or `test/frontend/` subdirectories | [ ] |
| 9 | No CamelCase in test class, method, or file names | [ ] |
| 10 | `setting_manager_test.cpp` does not depend on actual SettingsManager implementation beyond instantiation | [ ] |

---

## Risk Mitigation & Troubleshooting

### Potential Issue 1: MOC compilation failure
**Symptom:** Linker error about `meta-object` or `qt_meta_stringdata`.
**Fix:** Ensure `#include "setting_manager_test.moc"` is at the **bottom** of `setting_manager_test.cpp`, after all method definitions. This is required when a Q_OBJECT class is included from another translation unit.

### Potential Issue 2: CMake regex fails to find test methods
**Symptom:** CTest lists zero tests despite valid source files.
**Fix:** Verify that test methods use the pattern `void test_*(` (with opening parenthesis, not just `void test_*()`). The regex in `qtest_add_test.cmake` matches `void\s+(test_\w+)\s*\(` which requires the opening paren.

### Potential Issue 3: Windows path issues in DEF_SOURCE_LINE
**Symptom:** VS Code Test Explorer shows tests but clicking them does not open the editor.
**Fix:** The path normalization in `qtest_add_test.cmake` converts backslashes to forward slashes. If this fails, verify that the path is absolute (not relative) and uses valid characters.

### Potential Issue 4: QtTest not found
**Symptom:** CMake error "Could not find a package configuration file provided by Qt6Test".
**Fix:** Verify Qt6 was installed with the Test module. The root `CMakeLists.txt` already requests `Qt6::Test` in `find_package()`. If missing, reinstall Qt6 with the Test module component.

### Potential Issue 5: Multiple QTEST_MAIN conflicts
**Symptom:** Linker error about multiple definitions of `main`.
**Fix:** Ensure only ONE `QTEST_MAIN()` call exists per executable target. The `main.cpp` file should include test `.cpp` files but never define a second `QTEST_MAIN()`.

---

## File Change Summary

| File | Action | Description |
|---|---|---|
| `cmake/qtest_add_test.cmake` | **Create** | CMake function for QtTest registration with DEF_SOURCE_LINE |
| `CMakeLists.txt` (root) | **Modify** | Add `list(APPEND CMAKE_MODULE_PATH ...)` before subdirectories |
| `test/backend/core/CMakeLists.txt` | **Create** | Test executable target + qtest_add_test() call |
| `test/backend/core/main.cpp` | **Create** | QTEST_MAIN entry point with test class include |
| `test/backend/core/setting_manager_test.cpp` | **Create** | POC test class with 4 test methods |

**No changes required:**
- `test/CMakeLists.txt` — already includes CTest and backend subdirectory.
- `test/backend/CMakeLists.txt` — already contains `add_subdirectory(core)`.
- `src/backend/core/` source files — tests consume existing API, no modifications needed.
