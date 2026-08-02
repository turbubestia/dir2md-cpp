We need to scafold the test environment. For this we will use only the QtTest framework already configured in the main Makefile. The test setup is simple. Each subproject in `src/`, which are `src/backend/`, `src/cli/`, and `/src/frontend/` will have a matching folder in `test/`. For now we will only focus in the backend (so leave the cli and frontend alone). In turn every `src/path/to/file.hpp/.cpp` witl have a matching `test/path/to/file_test.cpp`. Also we will configure each module in a subproject as a standalone test executable. For now only `test/backend/core` module exist. The test will therfore be configured in `test/backend/core/CMakeList.cpp` with `test\backend\core\main.cpp` the test entry point. Since the add test will be repeated, we want to implement a function in `cmake\qtest_add_test.cmake` to simplify the qtest configuration to be analogoes to GoogleTest gtest_add_test().

The property expected by the **VS Code CMake Tools extension** for its "Go to Test" / Test Explorer location feature is indeed **`DEF_SOURCE_LINE`**.

---

## Property Format

`DEF_SOURCE_LINE` expects a string in the format of an **absolute path** to the source file followed by a **colon and line number**:

```text
<absolute_path_to_source_file>:<line_number>

```

---

## How to Set It in CMake

You can set this property right after your `add_test()` call using either `set_tests_properties` or `set_property`:

### Option 1: Using `set_tests_properties` (Recommended)

```cmake
add_test(NAME my_test COMMAND my_test_executable)

set_tests_properties(my_test PROPERTIES
    DEF_SOURCE_LINE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp:42"
)

```

### Option 2: Using `set_property`

```cmake
add_test(NAME my_test COMMAND my_test_executable)

set_property(TEST my_test PROPERTY
    DEF_SOURCE_LINE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp:42"
)

```

---

## Useful Context & Built-in Support

As reference (not to infer we want to use gtest)
* **`gtest_add_tests()`:** In CMake 3.30+, `gtest_add_tests()` automatically sets `DEF_SOURCE_LINE` for tests it parses during configuration.
* **`gtest_discover_tests()`:** In CMake 4.x, `gtest_discover_tests()` supports populating `DEF_SOURCE_LINE` dynamically using `--gtest_output=json` output from GoogleTest binaries.

## Final check

To know this is working properly lest create a simple test that pass in `test\backend\core\setting_manager_test.cpp` not tied yet to the actual implementation of `setting_manager`.

## Follow this links to get an upto date reference on QtTest framework
- https://doc.qt.io/qt-6/qtest-overview.html
- https://doc.qt.io/qt-6/qttest-best-practices.html
- https://doc.qt.io/qt-6/qttest-module.html

---

# Refinement Iteration 1
**Status:** PENDING USER FEEDBACK

## 1. Executive Summary
Scaffold a QtTest-based test environment for the dir2md-cpp project, starting with the `backend/core` module. This includes creating a reusable CMake helper function (`qtest_add_test()`) analogous to GoogleTest's `gtest_add_test()`, configuring VS Code CMake Tools Test Explorer integration via `DEF_SOURCE_LINE`, and delivering a minimal passing test as proof of concept.

## 2. Refined Requirements & Acceptance Criteria

### Requirement [REQ-01]: Test Directory Structure
- **Description:** The `test/` directory mirrors the structure of `src/backend/`. Each source file `src/backend/core/{file}.cpp/.hpp` has a corresponding test file `test/backend/core/{file}_test.cpp`.
- **Acceptance Criteria:**
  - [ ] Given the project root, when listing `test/backend/core/`, then files exist for each backend core source file with `_test.cpp` suffix convention.

### Requirement [REQ-02]: QtTest Test Executable Configuration
- **Description:** Each test module is configured as a standalone QtTest executable using `add_executable()` + `qt6_add_tests()` (or equivalent Qt6 test macros). The backend core tests are built as a single executable containing all test classes for that module.
- **Acceptance Criteria:**
  - [ ] Given the CMake configuration in `test/backend/core/CMakeLists.txt`, when configuring the project, then a test executable named `dir2md_core_tests` (or similar) is created.
  - [ ] Given the executable, when built, then it links against `Qt6::Test` and `dir2md_backend`.
  - [ ] Given the executable, when run directly, then it executes all registered test classes and reports results.

### Requirement [REQ-03]: Reusable CMake Helper Function
- **Description:** A helper function `qtest_add_test()` is implemented in `cmake/qtest_add_test.cmake` to simplify QtTest registration. It should be analogous to `gtest_add_test()` in ergonomics, accepting a test executable target and automatically discovering/adding test classes.
- **Acceptance Criteria:**
  - [ ] Given the file `cmake/qtest_add_test.cmake`, when sourced via `include()`, then the function `qtest_add_test(TARGET <target>)` is available.
  - [ ] Given a QtTest executable with test classes inheriting `QObject` and using `QTEST_MAIN()` or individual `QTEST_APPLESS_MAIN()` / manual `main()`, when `qtest_add_test()` is called, then each test class is registered as a CTest test.
  - [ ] Given the function sets `DEF_SOURCE_LINE` on each registered test, then VS Code CMake Tools Test Explorer shows the correct source location for each test.

### Requirement [REQ-04]: VS Code CMake Tools Integration
- **Description:** Each QtTest executable is configured so that the VS Code CMake Tools extension's Test Explorer can discover and locate tests. The `DEF_SOURCE_LINE` property must be set per test in the format `<absolute_path>:<line_number>`.
- **Acceptance Criteria:**
  - [ ] Given a registered QtTest, when the project is configured in VS Code, then the test appears in the CMake Tools Test Explorer panel.
  - [ ] Given a test in the explorer, when double-clicked or "Go to Test" is invoked, then the editor opens the source file at the correct line (the test method definition).

### Requirement [REQ-05]: Minimal Passing Proof-of-Concept Test
- **Description:** A minimal, standalone test file `test/backend/core/setting_manager_test.cpp` is created that does NOT depend on the actual `SettingsManager` implementation. It verifies the QtTest infrastructure works end-to-end.
- **Acceptance Criteria:**
  - [ ] Given the test file exists with a test class containing at least one test method, when the project is built and tests are run via CTest, then the test passes (exit code 0).
  - [ ] The test uses QtTest best practices: inherits `QObject`, uses `private slots:` for test methods, and follows the QTEST naming convention.

### Requirement [REQ-06]: CLI and Frontend Test Scaffolding Excluded
- **Description:** Only the backend module is in scope for this iteration. The `test/cli/` and `test/frontend/` directories are left empty or unconfigured.
- **Acceptance Criteria:**
  - [ ] Given the current CMake configuration, when building, then no errors occur related to missing CLI or frontend test subdirectories.

## 3. Scope & Constraints
- **In-Scope:**
  - Test directory structure for `src/backend/core/` → `test/backend/core/`
  - CMakeLists.txt configuration for `test/backend/core/`
  - Reusable `qtest_add_test()` function in `cmake/qtest_add_test.cmake`
  - VS Code CMake Tools `DEF_SOURCE_LINE` integration
  - Minimal passing proof-of-concept test (`setting_manager_test.cpp`)
  - Test entry point (`test/backend/core/main.cpp`)
- **Out-of-Scope:**
  - CLI module tests (`test/cli/`)
  - Frontend module tests (`test/frontend/`)
  - Integration or end-to-end tests
  - Mocking frameworks or external dependencies beyond Qt6
  - CI/CD pipeline integration (future iteration)
- **Technical Constraints / Edge Cases:**
  - Qt6 Test module is already found in the root `CMakeLists.txt` (`find_package(Qt6 REQUIRED COMPONENTS ... Test)`), so no additional Qt dependency discovery is needed.
  - CMake version is 3.21 (minimum); must ensure compatibility with features used in `qtest_add_test()`. Note: `qt6_add_tests()` was introduced in Qt 6.5 — need to verify project's Qt version or use the older `add_test()` + manual registration approach as fallback.
  - Test executables should link against the backend library (`dir2md_backend`) to access internal headers, but the POC test intentionally avoids this dependency.
  - Windows environment: ensure test executables can be discovered and run by CTest on Windows (`.exe` suffix handled automatically by CMake).

## 4. Open Design Choices (Questions for User)

### [Technical]: Test Discovery Approach
QtTest does not have a built-in test discovery macro like `gtest_add_tests()`. Two approaches are available:

1. **Manual registration with helper:** Each test class is manually listed in `qtest_add_test(TARGET <exe> TESTS TestClass1 TestClass2 ...)`. The helper wraps `add_test()` calls and sets `DEF_SOURCE_LINE` per class. Simpler, more explicit, works with all Qt6 versions.
**User: manual requistration.**

2. **Binary introspection (advanced):** Run the test executable with a custom flag (e.g., `--list-tests`) to parse output and auto-discover test classes at configure time. More complex, requires parsing stdout, but closer to `gtest_discover_tests()` behavior.
**User: we DON'T want binary inspection beacuse mapping to source is not possible.**

**Recommendation:** Approach 1 for this iteration — explicit, simple, and fully compatible with CMake 3.21 + Qt6.

### [Technical]: Test Executable Structure
Should each module have a **single test executable** containing all its test classes (with a shared `main.cpp` using `QTEST_MAIN()`), or **one executable per test class**?

- **Single executable:** Simpler CMake, faster builds (one binary), aligns with the current `test/backend/core/main.cpp` structure.
- **Per-class executables:** Easier isolation, but more CMake boilerplate.

**Recommendation:** Single executable per module (`dir2md_core_tests`) with shared `main.cpp`.

**User: single executable per module. So every file under `src/backend/core` will have a single executable `backend_core_test.exe`**

### [Technical]: DEF_SOURCE_LINE Granularity
Should `DEF_SOURCE_LINE` point to the **test method** (e.g., `void tst_SettingsManager::initTestCase()`) or the **test class definition**?
**User: to the test method.**

- **Test method:** More precise — clicking a test in Test Explorer opens exactly where the test logic begins. Requires parsing the source file to find the method's line number.
- **Test class:** Simpler to implement — points to the class declaration line. Less precise but sufficient for navigation.

**Recommendation:** Test method level for best developer experience, but requires a mechanism to extract method line numbers (can be done via regex parsing of the test source file in CMake).

## User Notes
Also add a prefix to qtest_add_test to set it in add_test PREFIX so we can organize them in the test discovery. For this task the prefix would be `backend.core` and the test example would shows as `backend.core.setting_manager_test`.

## Test Naming convention

The file pair `setting_manager.hpp/.cpp` will have the test file `setting_manager_test.cpp` and the test class will have the same name as the file `class setting_manager_test` and the test must be register also with the same class name `setting_manager_test`. Do not use CamelCase for test names. follow the repo conventions.

# Refinement Iteration 2
**Status:** LOCKED

## 1. Executive Summary
Finalize the QtTest-based test environment scaffold for the dir2md-cpp project by incorporating all user decisions from Iteration 1: manual test registration via a reusable `qtest_add_test()` CMake helper, single executable per backend module with snake_case naming, DEF_SOURCE_LINE pointing to individual test methods for VS Code Test Explorer navigation, and a minimal passing proof-of-concept test in `test/backend/core/setting_manager_test.cpp`.

## 2. Refined Requirements & Acceptance Criteria

### Requirement [REQ-01]: Test Directory Structure
- **Description:** The `test/` directory mirrors the structure of `src/backend/`. Each source file `src/backend/core/{file}.cpp/.hpp` has a corresponding test file `test/backend/core/{file}_test.cpp`.
- **Acceptance Criteria:**
  - [ ] Given the project root, when listing `test/backend/core/`, then files exist for each backend core source file with `_test.cpp` suffix convention.

### Requirement [REQ-02]: QtTest Test Executable Configuration — Single Executable Per Module
- **Description:** Each backend module has a single test executable containing all its test classes, using a shared `main.cpp` with `QTEST_MAIN()`. The executable links against `Qt6::Test` and the corresponding backend library. Naming follows snake_case: `backend_core_test` for the core module.
- **Acceptance Criteria:**
  - [ ] Given the CMake configuration in `test/backend/core/CMakeLists.txt`, when configuring the project, then a test executable named `backend_core_test` is created.
  - [ ] Given the executable, when built, then it links against `Qt6::Test` and `dir2md_backend`.
  - [ ] Given the executable, when run directly, then it executes all registered test classes and reports results.

### Requirement [REQ-03]: Reusable CMake Helper Function — Manual Registration with PREFIX
- **Description:** A helper function `qtest_add_test()` is implemented in `cmake/qtest_add_test.cmake` to simplify QtTest registration via manual class listing. The function accepts a test executable target and an explicit list of test class names, wraps `add_test()` calls, sets `DEF_SOURCE_LINE` per test pointing to the test method line, and supports a `PREFIX` argument for hierarchical test naming in CTest/Test Explorer.
- **Acceptance Criteria:**
  - [ ] Given the file `cmake/qtest_add_test.cmake`, when sourced via `include()`, then the function `qtest_add_test(TARGET <target> TESTS <TestClass1> <TestClass2> ... [PREFIX <prefix>])` is available.
  - [ ] Given a QtTest executable with test classes inheriting `QObject` and using `QTEST_MAIN()`, when `qtest_add_test()` is called, then each test class is registered as a separate CTest test.
  - [ ] Given the function sets `DEF_SOURCE_LINE` on each registered test pointing to the test method line number, then VS Code CMake Tools Test Explorer shows the correct source location for each individual test method.
  - [ ] Given a PREFIX argument (e.g., `backend.core`), when tests are listed via `ctest --output-on-failure`, then test names are prefixed hierarchically (e.g., `backend.core.setting_manager_test`).

### Requirement [REQ-04]: VS Code CMake Tools Integration
- **Description:** Each QtTest executable is configured so that the VS Code CMake Tools extension's Test Explorer can discover and locate tests. The `DEF_SOURCE_LINE` property must be set per test in the format `<absolute_path_to_source_file>:<line_number_of_test_method>`.
- **Acceptance Criteria:**
  - [ ] Given a registered QtTest, when the project is configured in VS Code, then the test appears in the CMake Tools Test Explorer panel under the correct prefix hierarchy.
  - [ ] Given a test in the explorer, when "Go to Test" is invoked, then the editor opens the source file at the exact line of the test method definition (not just the class declaration).

### Requirement [REQ-05]: Minimal Passing Proof-of-Concept Test
- **Description:** A minimal, standalone test file `test/backend/core/setting_manager_test.cpp` is created that does NOT depend on the actual `SettingsManager` implementation. It verifies the QtTest infrastructure works end-to-end. The test class is named `setting_manager_test` (snake_case, matching the source file name).
- **Acceptance Criteria:**
  - [ ] Given the test file exists with a test class `setting_manager_test` containing at least one test method, when the project is built and tests are run via CTest, then the test passes (exit code 0).
  - [ ] The test uses QtTest best practices: inherits `QObject`, uses `private slots:` for test methods, and follows the QTEST naming convention.
  - [ ] The test is registered with prefix `backend.core` so it appears as `backend.core.setting_manager_test` in CTest output.

### Requirement [REQ-06]: CLI and Frontend Test Scaffolding Excluded
- **Description:** Only the backend module is in scope for this iteration. The `test/cli/` and `test/frontend/` directories are left empty or unconfigured.
- **Acceptance Criteria:**
  - [ ] Given the current CMake configuration, when building, then no errors occur related to missing CLI or frontend test subdirectories.

### Requirement [REQ-07]: Test Naming Convention — snake_case
- **Description:** All test artifacts follow the repository's snake_case convention. The test file for `setting_manager.hpp/.cpp` is named `setting_manager_test.cpp`. The test class inside is named `setting_manager_test` (same as the file stem). The CTest test name uses the same snake_case class name, optionally prefixed by the module hierarchy.
- **Acceptance Criteria:**
  - [ ] Given a source file `src/backend/core/{name}.cpp/.hpp`, then the corresponding test file is `test/backend/core/{name}_test.cpp`.
  - [ ] Given the test file, then the class name inside matches the file stem (e.g., `class setting_manager_test : public QObject`).
  - [ ] No CamelCase is used for any test class, method, or file name in the test directory.

## 3. Scope & Constraints
- **In-Scope:**
  - Test directory structure for `src/backend/core/` → `test/backend/core/`
  - CMakeLists.txt configuration for `test/backend/core/` (single executable per module)
  - Reusable `qtest_add_test()` function in `cmake/qtest_add_test.cmake` with PREFIX support and DEF_SOURCE_LINE per test method
  - VS Code CMake Tools `DEF_SOURCE_LINE` integration pointing to test method lines
  - Minimal passing proof-of-concept test (`setting_manager_test.cpp`) with snake_case naming
  - Test entry point (`test/backend/core/main.cpp`) using `QTEST_MAIN()`
- **Out-of-Scope:**
  - CLI module tests (`test/cli/`)
  - Frontend module tests (`test/frontend/`)
  - Integration or end-to-end tests
  - Mocking frameworks or external dependencies beyond Qt6
  - CI/CD pipeline integration (future iteration)
  - Automatic test discovery via binary introspection (explicitly excluded by user)
- **Technical Constraints / Edge Cases:**
  - Qt6 Test module is already found in the root `CMakeLists.txt` (`find_package(Qt6 REQUIRED COMPONENTS ... Test)`), so no additional Qt dependency discovery is needed.
  - CMake version is 3.21 (minimum); `qtest_add_test()` must be compatible with this version. `qt6_add_tests()` (Qt 6.5+) may not be available — use manual `add_test()` + `set_tests_properties()` approach.
  - Test executables link against the backend library (`dir2md_backend`) to access internal headers, but the POC test intentionally avoids this dependency.
  - Windows environment: ensure test executables can be discovered and run by CTest on Windows (`.exe` suffix handled automatically by CMake).
  - DEF_SOURCE_LINE requires an absolute path — use `${CMAKE_CURRENT_SOURCE_DIR}` to construct it in CMake.
  - Finding the exact line number of each test method in CMake requires either: (a) parsing the source file at configure time via a custom script, or (b) having the test class author specify the line number explicitly. Recommendation: use a helper macro within the test file itself that records the line number via `__LINE__`, stored in a static variable accessible to CMake's `set_tests_properties()`.

## 4. Open Design Choices (Questions for User)
*All design choices from Iteration 1 have been resolved by user input. No open questions remain.*

---

**LOCKED**