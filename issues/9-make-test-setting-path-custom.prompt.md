# Implementation Plan: 9-make-test-setting-path-custom

## Traceability Linkage
[Analysis Reference](./9-make-test-setting-path-custom.plan.analysis.md)

This implementation plan translates the architectural requirements defined in the analysis document into executable steps. All phases and steps below reference specific sections from the analysis to maintain strict traceability.

---

## Phase 1: Backend Header Modification

**References:** Analysis Section 2.1 (Component & File Impact Map - settings_manager.hpp)

### Step 1.1: Add Test-Only API Declarations
- Add static test-only API declarations to `SettingsManager` class in `./src/backend/core/settings_manager.hpp`
- Declare `setTestBaseDirectory(const QString &)` method
- Declare `clearTestBaseDirectory()` method
- Add private static members to hold test base directory state with process-wide semantics
- Add documentation comment marking the API as test-only

**Exit Criterion:** Header file contains test-only API declarations with proper documentation comments

**Validation Command:** `grep -n "setTestBaseDirectory\|clearTestBaseDirectory" src/backend/core/settings_manager.hpp`

### Step 1.2: Verify Header Syntax
- Ensure header maintains existing public API for production consumers
- Verify no changes to `save_to_file`/`load_from_file` signatures
- Confirm snake_case conventions are preserved

**Exit Criterion:** Header compiles without syntax errors

**Validation Command:** `cmake --preset debug --build --target dir2md_backend`

---

## Phase 2: Backend Implementation Modification

**References:** Analysis Section 2.2 (Component & File Impact Map - settings_manager.cpp)

### Step 2.1: Remove Compile-Time Flag
- Remove `#ifdef DIR2MD_DEBUG_TEST_PATH` block from `resolvePersistencePath` function in `./src/backend/core/settings_manager.cpp`
- Remove all conditional compilation logic related to debug test path

**Exit Criterion:** No `#ifdef DIR2MD_DEBUG_TEST_PATH` remains in settings_manager.cpp

**Validation Command:** `grep -n "DIR2MD_DEBUG_TEST_PATH" src/backend/core/settings_manager.cpp`

### Step 2.2: Extend Path Resolution Logic
- Modify `resolvePersistencePath` to consult static test base directory state
- Implement logic: when test base is set, resolve plain names within test base directory
- Implement validation: reject paths with separators or absolute components outside test base
- Implement fallback: when test base not set, retain production logic resolving to `~/.config/dir2md/<name>`
- Ensure path validation rejects traversal, separators, and absolute paths in production mode
- Ensure test mode enforces containment within test base directory

**Exit Criterion:** Path resolution correctly handles both test and production modes

**Validation Command:** `cmake --preset debug --build --target dir2md_backend`

### Step 2.3: Verify Implementation Integrity
- Confirm `save_to_file` and `load_from_file` continue to use `resolvePersistencePath`
- Verify error handling returns empty QString for invalid paths
- Verify warnings are emitted for invalid paths

**Exit Criterion:** Implementation maintains existing error handling patterns

**Validation Command:** `grep -n "resolvePersistencePath" src/backend/core/settings_manager.cpp`

---

## Phase 3: Test Harness Modification

**References:** Analysis Section 2.3 (Component & File Impact Map - setting_manager_test.cpp)

### Step 3.1: Replace Test Main Function
- Replace `QTEST_MAIN(setting_manager_test)` with custom `main()` function in `./test/backend/core/setting_manager_test.cpp`
- Create `QTemporaryDir` instance in main function
- Call `SettingsManager::setTestBaseDirectory` with temporary directory path before test execution
- Run `QTest::qExec` with test instance
- Call `SettingsManager::clearTestBaseDirectory` after test execution
- Ensure cleanup runs even if tests fail

**Exit Criterion:** Custom main function properly sets up and tears down test base directory

**Validation Command:** `grep -n "QTEST_MAIN\|setTestBaseDirectory\|clearTestBaseDirectory" test/backend/core/setting_manager_test.cpp`

### Step 3.2: Remove Conditional Compilation Guards
- Remove all `#ifdef DIR2MD_DEBUG_TEST_PATH` guards from test file
- Remove per-test `QTemporaryDir` construction
- Remove direct `tempDir.path()` usage in tests
- Update tests to pass simple file names instead of full paths

**Exit Criterion:** No `#ifdef DIR2MD_DEBUG_TEST_PATH` remains in test file

**Validation Command:** `grep -n "DIR2MD_DEBUG_TEST_PATH" test/backend/core/setting_manager_test.cpp`

### Step 3.3: Update Path Restriction Tests
- Update `test_save_path_rejects_absolute_path` to always assert rejection of `/tmp/settings.json`
- Update `test_save_path_rejects_traversal` to always run and assert rejection
- Update `test_save_path_rejects_separator_in_name` to always run and assert rejection
- Update `test_save_path_accepts_debug_test_override` to verify save succeeds for simple name within test base

**Exit Criterion:** All path restriction tests run unconditionally in both Debug and Release

**Validation Command:** `grep -A 10 "test_save_path_rejects_absolute_path" test/backend/core/setting_manager_test.cpp`

---

## Phase 4: Build Configuration Cleanup

**References:** Analysis Section 2.4-2.5 (Component & File Impact Map - CMakePresets.json, src/backend/CMakeLists.txt)

### Step 4.1: Remove Compile Definition from CMakePresets
- Remove `DIR2MD_DEBUG_TEST_PATH` cache variable from `debug` preset in `./CMakePresets.json`
- Verify no preset enables the compile-time flag

**Exit Criterion:** CMakePresets.json contains no reference to DIR2MD_DEBUG_TEST_PATH

**Validation Command:** `grep -n "DIR2MD_DEBUG_TEST_PATH" CMakePresets.json`

### Step 4.2: Remove Compile Definition from Backend CMakeLists
- Remove conditional `target_compile_definitions(dir2md_backend PUBLIC DIR2MD_DEBUG_TEST_PATH)` block from `./src/backend/CMakeLists.txt`
- Verify backend library no longer receives compile-time test flag

**Exit Criterion:** src/backend/CMakeLists.txt contains no DIR2MD_DEBUG_TEST_PATH references

**Validation Command:** `grep -n "DIR2MD_DEBUG_TEST_PATH" src/backend/CMakeLists.txt`

### Step 4.3: Verify Build Configuration
- Reconfigure project with debug preset
- Reconfigure project with release preset
- Confirm no compile definitions related to test path override

**Exit Criterion:** Both Debug and Release presets configure successfully without test path flag

**Validation Command:** `cmake --preset debug && cmake --preset release`

---

## Phase 5: Testing and Validation

**References:** Analysis Section 4 (Verification Checklist)

### Step 5.1: Run Debug Build Tests
- Build backend tests with debug preset
- Execute `setting_manager_test` executable
- Verify all save/load tests pass

**Exit Criterion:** All tests pass in Debug build

**Validation Command:** `cmake --preset debug --build --target backend_core_setting_manager_test && ctest --preset debug -R setting_manager`

### Step 5.2: Run Release Build Tests
- Build backend tests with release preset
- Execute `setting_manager_test` executable
- Verify all save/load tests pass without DIR2MD_DEBUG_TEST_PATH

**Exit Criterion:** All tests pass in Release build

**Validation Command:** `cmake --preset release --build --target backend_core_setting_manager_test && ctest --preset release -R setting_manager`

### Step 5.3: Verify Path Restriction Tests
- Confirm path-restriction tests pass in both Debug and Release presets
- Verify absolute path rejection works consistently
- Verify traversal rejection works consistently
- Verify separator rejection works consistently

**Exit Criterion:** Path restriction tests pass in both build configurations

**Validation Command:** `ctest --preset debug -R setting_manager -V | grep "test_save_path"`

### Step 5.4: Verify Code Cleanup
- Confirm no `#ifdef DIR2MD_DEBUG_TEST_PATH` remains in `src/` or `test/`
- Confirm `SettingsManager::setTestBaseDirectory` is marked test-only in header documentation
- Confirm tests no longer reference `tempDir.path()` directly
- Confirm test harness custom main sets and clears test base directory

**Exit Criterion:** All verification checklist items from Analysis Section 4 are satisfied

**Validation Command:** 
```
grep -r "DIR2MD_DEBUG_TEST_PATH" src/ test/ && echo "FAIL" || echo "PASS"
grep -n "test-only" src/backend/core/settings_manager.hpp
```

### Step 5.5: Coverage Validation
- Run tests with coverage preset
- Generate coverage report for settings_manager
- Verify coverage meets project standards

**Exit Criterion:** Coverage report generated and reviewed

**Validation Command:** `cmake --preset debug-coverage --build --target backend_core_setting_manager_test && ctest --preset debug-coverage -R setting_manager`

---

## Limitations and Constraints

- Do not create or modify implementation code in this plan document
- This plan is purely descriptive and instructional
- All changes must preserve existing snake_case conventions
- All changes must maintain trailing return types
- No [[nodiscard]] decorators should be added
- QML file names must remain CamelCase (not applicable to this change)
- Test-only API must be clearly documented and not exposed to frontend/CLI consumers

---

## Success Criteria

All verification checklist items from Analysis Section 4 must be satisfied:
- All save/load tests pass in Release build without DIR2MD_DEBUG_TEST_PATH
- Path-restriction tests pass in both Debug and Release presets
- No `#ifdef DIR2MD_DEBUG_TEST_PATH` remains in src/ or test/
- SettingsManager::setTestBaseDirectory is clearly marked as test-only
- CMakePresets.json no longer contains DIR2MD_DEBUG_TEST_PATH
- src/backend/CMakeLists.txt no longer adds compile definition
- Test harness custom main sets and clears test base directory
- Tests no longer reference tempDir.path() directly
- CI runs tests for both Debug and Release presets successfully
