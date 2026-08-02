# Implementation Analysis: 2-onboard-test-env

## 1. Architectural Impact & Data Flow

*High-level overview of how the QtTest-based test infrastructure integrates into the dir2md-cpp project.*

- **Affected Subsystems:**
  - **Build System (CMake):** Custom test helper function, test executable targets, CTest registration with VS Code Test Explorer properties.
  - **Backend Core Module (`src/backend/core/`):** No source code changes required — tests consume the existing public API via header inclusion and library linking.
  - **Test Directory (`test/`):** New test executables mirror backend module structure; each backend module gets one dedicated test executable.

- **Data Flow Changes:**
  ```
  Developer writes test class in test/backend/core/{module}_test.cpp
    -> CMakeLists.txt registers test classes via qtest_add_test()
    -> qtest_add_test() wraps add_test() + set_tests_properties(DEF_SOURCE_LINE)
    -> CTest discovers tests; VS Code CMake Tools Test Explorer reads DEF_SOURCE_LINE
    -> User clicks test in Test Explorer -> editor opens exact method line
    -> Running test executes QTEST_MAIN() entry point -> all registered test classes run
  ```

- **Structural Additions:**
  - `cmake/qtest_add_test.cmake` — reusable CMake function (library, not a target).
  - `test/backend/core/` — test executable per backend module (`backend_core_test`).
  - Test source files follow `{source_stem}_test.cpp` naming convention with matching snake_case class names.

## 2. Component & File Impact Map

### [CMakeLists.txt] (project root)
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add `list(APPEND CMAKE_MODULE_PATH ...)` or `set(CMAKE_MODULE_PATH ...)` to include the `cmake/` directory so custom `.cmake` files are discoverable.
- **Logic Modifications Required:**
  - [ ] Ensure `cmake/qtest_add_test.cmake` is accessible before any `add_subdirectory(test)` call.

### [cmake/qtest_add_test.cmake] (new content)
- **Type of Change:** Create (file exists but is empty)
- **Structural Changes:**
  - [ ] Define function `qtest_add_test()` with named arguments: `TARGET`, `TESTS` (variadic list of test class names), and optional `PREFIX`.
  - [ ] For each test class name, the function must:
    - Construct a CTest test name as `{PREFIX}.{class_name}` (or just `{class_name}` if no PREFIX).
    - Call `add_test(NAME <test_name> COMMAND <target>)`.
    - Set `DEF_SOURCE_LINE` property pointing to each **test method** line within the source file.
- **Logic Modifications Required:**
  - [ ] Since QtTest does not provide binary introspection and CMake cannot parse C++ at configure time, the function must accept an optional `SOURCES` argument listing the test source files. The function will use a CMake script or regex-based parsing to extract method line numbers from the listed `.cpp` files.
  - [ ] Alternatively (simpler approach): Accept a `METHOD_LINES` named argument mapping class names to their method line numbers, set by the test author inline. However, this adds boilerplate.
  - [ ] **Recommended approach:** Use a CMake `file(READ ...)` + `string(REGEX MATCHALL ...)` to scan each source file for `void <test_method>()` patterns inside `private slots:` blocks, and auto-extract line numbers. This keeps the API clean: `qtest_add_test(TARGET backend_core_test TESTS setting_manager_test PREFIX backend.core)`.
  - [ ] The function must handle Windows paths correctly in `DEF_SOURCE_LINE` (absolute path with forward slashes or native separators).

### [test/CMakeLists.txt]
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] No structural changes needed — already includes `CTest` and `add_subdirectory(backend)`.

### [test/backend/CMakeLists.txt] (check if exists)
- **Type of Change:** Create or Modify
- **Structural Changes:**
  - [ ] Ensure this file exists and contains `add_subdirectory(core)` to include the core test module.
- **Logic Modifications Required:**
  - [ ] If the file does not exist, create it with just the `add_subdirectory(core)` line.

### [test/backend/core/CMakeLists.txt] (exists but empty)
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Define executable target `backend_core_test` using `add_executable()`.
  - [ ] Add source files: `main.cpp` + all `{module}_test.cpp` files in the directory.
  - [ ] Link against `Qt6::Test` and `dir2md_backend` (the backend library).
  - [ ] Include the custom CMake helper: `include(${CMAKE_SOURCE_DIR}/cmake/qtest_add_test.cmake)`.
  - [ ] Call `qtest_add_test(TARGET backend_core_test TESTS setting_manager_test PREFIX backend.core)`.
- **Logic Modifications Required:**
  - [ ] Use `qt6_add_executable()` or standard `add_executable()` — since the project uses Qt6, prefer `qt6_add_executable()` if available (Qt 6.3+), otherwise fall back to `add_executable()`.
  - [ ] Set target properties: `CXX_STANDARD 20`, `CXX_STANDARD_REQUIRED ON`.

### [test/backend/core/main.cpp] (exists but empty)
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Include `<QTest>` header.
  - [ ] Add `QTEST_MAIN(setting_manager_test)` — or more generally, include all test classes via a macro approach. Since `QTEST_MAIN()` only registers one class, use the pattern of manually adding each class to the test framework:
    ```cpp
    #include <QTest>
    // Forward declarations or includes of all test classes
    QTEST_MAIN(setting_manager_test)  // Only one call needed; other classes are auto-discovered by QtTest when listed in qtest_add_test
    ```
  - **Note:** Actually, `QTEST_MAIN()` creates a `main()` that runs ALL test classes found in the linked translation units. So including all test class headers and calling `QTEST_MAIN()` for one class is sufficient — QtTest's internal registration will pick up all `QObject`-derived test classes in the binary.
- **Logic Modifications Required:**
  - [ ] Include all test class headers (e.g., `#include "setting_manager_test.cpp"` or individual `.hpp` files) so the linker picks up all test classes.

### [test/backend/core/setting_manager_test.cpp] (exists but empty)
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Define class `setting_manager_test : public QObject` (snake_case per repo convention).
  - [ ] Add `Q_OBJECT` macro.
  - [ ] Add `private slots:` section with at least one test method, e.g., `void test_basic()`.
  - [ ] The test must be **standalone** — no dependency on actual `SettingsManager` implementation. Use a simple assertion like `QVERIFY(true)` or test a trivial Qt type.
- **Logic Modifications Required:**
  - [ ] Test method body: use `QCOMPARE`, `QVERIFY`, or `QTEST_MAIN`-compatible assertions to verify infrastructure works. Example: `QVERIFY(true)` or `QCOMPARE(1 + 1, 2)`.

### [test/backend/core/CMakeLists.txt] — DEF_SOURCE_LINE Strategy
- **Type of Change:** Modify (as part of the CMakeLists.txt above)
- **Structural Changes:**
  - [ ] The `qtest_add_test()` function must compute absolute paths using `${CMAKE_CURRENT_SOURCE_DIR}`.
  - [ ] For each test class, find the line number of each test method (`void <method>()` inside `private slots:`) and set `DEF_SOURCE_LINE` to `<absolute_path>:<line_number>`.

## 3. Boundary & Edge Case Analysis

- **Error Handling:**
  - If a test source file listed in `qtest_add_test()` does not exist, CMake should fail at configure time with a clear error (use `file(READ ...)` to validate).
  - If no test methods are found in a source file, the function should emit a `WARNING()` rather than failing silently.

- **Security & Permissions:**
  - No security concerns — tests run locally during development only.

- **Performance / Scale Impact:**
  - One executable per backend module means all test classes for that module share a single binary. Build time scales with the number of test classes but avoids per-class compilation overhead.
  - CTest discovery at configure time (via `qtest_add_test()`) adds negligible overhead — it's just string manipulation and file reads in CMake.

- **Cross-Platform Considerations:**
  - Windows paths in `DEF_SOURCE_LINE` must use the native format (backslashes are acceptable per VS Code CMake Tools spec, but forward slashes are safer).
  - Test executables produce `.exe` on Windows automatically via CMake — no special handling needed.

- **QtTest-Specific Edge Cases:**
  - `QTEST_MAIN()` requires that all test classes be in the same binary and inherit `QObject`. This is guaranteed by the single-executable-per-module design.
  - Test methods must be declared in `private slots:` — the regex parser in `qtest_add_test()` should only look inside `private slots:` blocks to find method signatures.
  - If a test class has no test methods (only setup/teardown slots like `initTestCase()`), it should not be registered as a CTest test by `qtest_add_test()`.

## 4. Verification Checklist

- [ ] Verify that `cmake/qtest_add_test.cmake` defines the function `qtest_add_test()` and is includable via `include()`.
- [ ] Verify that configuring the project (`cmake --preset debug`) succeeds with no errors related to test targets.
- [ ] Verify that the executable `backend_core_test` (or `backend_core_test.exe` on Windows) is built in the output directory.
- [ ] Verify that running `ctest --output-on-failure` lists tests with the `backend.core.` prefix (e.g., `backend.core.setting_manager_test`).
- [ ] Verify that the POC test `setting_manager_test` passes (exit code 0).
- [ ] Verify that VS Code CMake Tools Test Explorer shows the test under the `backend.core` hierarchy.
- [ ] Verify that clicking a test in the Test Explorer opens the editor at the correct test method line (not just the class declaration).
- [ ] Verify that building does not produce errors related to missing `test/cli/` or `test/frontend/` subdirectories.
- [ ] Verify that no CamelCase is used for any test class, method, or file name in the test directory.
- [ ] Verify that the test file `setting_manager_test.cpp` does not depend on the actual `SettingsManager` implementation (no includes of `settings_manager.hpp`).
