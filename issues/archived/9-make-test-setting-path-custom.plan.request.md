# 9-make-test-setting-path-custom.prompt.md

## Goal
Replace the compile-time `DIR2MD_DEBUG_TEST_PATH` flag with a runtime test-only override for `SettingsManager` persistence paths, so all save/load tests run in both Debug and Release builds without bypassing production path constraints.

## Problem
* `SettingsManager::resolvePersistencePath` uses `#ifdef DIR2MD_DEBUG_TEST_PATH` to allow absolute/temporary paths.
* `CMakePresets.json` enables `DIR2MD_DEBUG_TEST_PATH` only for the Debug preset.
* Many tests build a full `QTemporaryDir` path and call `save_to_file`/`load_from_file` with that path. Those tests only succeed when the flag is on.
* Path-restriction tests are `#ifdef` guarded and skipped in Debug.

This makes the test suite non-portable to Release and hides the intent of the override.

## Desired design
* Introduce a test-only runtime override, not a compile-time flag.
* `SettingsManager` gains a static API:
  * `static void SettingsManager::setTestBaseDirectory(const QString &baseDir);`
  * `static void SettingsManager::clearTestBaseDirectory();`
  * Internally `resolvePersistencePath` checks if a test base is set. If set, it uses the test base directory for persistence instead of the production path. Otherwise it falls back to the production rule: plain name → `~/.config/dir2md/<name>`, reject separators/absolute paths.
* The override is only intended for tests. It should be documented as test-only and ideally guarded by a debug-only assertion or a comment that static analysis can flag as intentional test support.
* The test base directory is set in the test harness and cleared after tests run. Tests themselves should not need to know about the temp directory.

## Test harness changes
* Replace `QTEST_MAIN(setting_manager_test)` with a custom `main()` that:
  1. Creates a `QTemporaryDir` for the test run.
  2. Calls `SettingsManager::setTestBaseDirectory(tempDir.path())` before `QTest::qExec`.
  3. Calls `SettingsManager::clearTestBaseDirectory()` after execution.
* Tests should not reference `tempDir.path()` directly. The `SettingsManager` should handle path resolution internally using the test base directory, ensuring consistent application of path rules without folder separators in test code.
* Tests that verify production path rejection remain unchanged and will still reject paths outside the test base.

## Test updates required
* Remove all `#ifdef DIR2MD_DEBUG_TEST_PATH` guards from `setting_manager_test.cpp`.
* `test_save_path_rejects_absolute_path` should always assert rejection of `/tmp/settings.json` independently of the test base. The test base should only allow paths that are within the test base, not absolute paths.
* `test_save_path_rejects_traversal` and `test_save_path_rejects_separator_in_name` should always run and assert rejection.
* `test_save_path_accepts_debug_test_override` becomes a normal test that verifies saving to a path inside the test base succeeds.
* No test should be skipped based on compile flags.

## Implementation steps
1. Add static members to `SettingsManager` for test base directory storage, with thread-local or process-wide semantics.
2. Modify `resolvePersistencePath` to:
   * If test base is set, use the test base directory for persistence paths instead of production paths. Tests should pass simple names without separators, and the manager will resolve them within the test base.
   * Otherwise keep existing production logic: plain name → `~/.config/dir2md/<name>`, reject separators/absolute paths.
3. Add documentation comment marking the API as test-only.
4. Update `test/backend/core/setting_manager_test.cpp`:
   * Replace `QTEST_MAIN` with custom `main`.
   * Initialize/clear test base directory around `QTest::qExec`.
   * Remove `#ifdef DIR2MD_DEBUG_TEST_PATH` blocks.
   * Remove all references to `tempDir.path()` from test code.
5. Remove `DIR2MD_DEBUG_TEST_PATH` from `CMakePresets.json` and `src/backend/CMakeLists.txt`.
6. Verify all tests pass in both Debug and Release presets.
7. Run static analysis to ensure the test-only API is flagged appropriately and no compile-time flag remains.

## Acceptance criteria
* All save/load tests pass in Release build without `DIR2MD_DEBUG_TEST_PATH`.
* Path-restriction tests pass in both Debug and Release.
* No `#ifdef DIR2MD_DEBUG_TEST_PATH` remains in source or tests.
* `SettingsManager::setTestBaseDirectory` is clearly marked as test-only.
* CI runs tests for both presets successfully.

**LOCKED**