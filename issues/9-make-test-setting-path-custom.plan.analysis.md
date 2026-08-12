# Implementation Analysis: 9-make-test-setting-path-custom

## 1. Architectural Impact & Data Flow
*High-level overview of how data flows through the system for this feature. Identify any new patterns or structural additions.*
- **Affected Subsystems:** 
  - Backend core settings subsystem: `dir2md::backend::SettingsManager` and its persistence path resolution logic.
  - Test harness for backend core: `setting_manager_test` QtTest executable.
  - Build configuration: CMake presets and backend CMakeLists that control compile-time test flags.
- **Data Flow Changes:** 
  - Currently: `save_to_file` / `load_from_file` → `resolvePersistencePath(filePath)` → compile-time `#ifdef DIR2MD_DEBUG_TEST_PATH` decides whether absolute/temporary paths are allowed. In Debug builds, absolute paths within home/temp are accepted; in Release, only plain names resolved under `~/.config/dir2md`.
  - After change: `save_to_file` / `load_from_file` → `resolvePersistencePath(filePath)` checks a runtime test base directory override. If set, plain names are resolved relative to the test base directory; absolute paths and separators remain rejected unless within the test base. If not set, production rule applies unchanged. Test harness sets/clears the override around `QTest::qExec`, so all tests share a single temporary base without per-test path construction.

## 2. Component & File Impact Map
*Identify the exact files that must be created, modified, or deleted, and what structural changes they require.*

### `./src/backend/core/settings_manager.hpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add static test-only API declarations to `SettingsManager`: `setTestBaseDirectory(const QString &)` and `clearTestBaseDirectory()`.
  - [ ] Add private static members to hold the test base directory state, with process-wide semantics.
  - [ ] Add documentation comment marking the API as test-only.
- **Logic Modifications Required:**
  - [ ] No public API change for production consumers; existing `save_to_file`/`load_from_file` signatures remain.

### `./src/backend/core/settings_manager.cpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Remove `#ifdef DIR2MD_DEBUG_TEST_PATH` block from `resolvePersistencePath`.
  - [ ] Extend `resolvePersistencePath` to consult the static test base directory state.
  - [ ] When test base is set, resolve plain names within the test base and reject paths with separators or absolute components outside the base.
  - [ ] When test base is not set, retain production logic: plain name → `~/.config/dir2md/<name>`, reject separators/absolute paths.
- **Logic Modifications Required:**
  - [ ] Ensure path validation still rejects traversal, separators, and absolute paths in production mode.
  - [ ] Ensure test mode still enforces containment within the test base directory.

### `./test/backend/core/setting_manager_test.cpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Replace `QTEST_MAIN(setting_manager_test)` with a custom `main()` that creates a `QTemporaryDir`, calls `SettingsManager::setTestBaseDirectory`, runs `QTest::qExec`, then calls `SettingsManager::clearTestBaseDirectory`.
  - [ ] Remove all `#ifdef DIR2MD_DEBUG_TEST_PATH` guards.
  - [ ] Remove per-test `QTemporaryDir` construction and direct `tempDir.path()` usage; tests should pass simple file names.
- **Logic Modifications Required:**
  - [ ] `test_save_path_rejects_absolute_path` must always assert rejection of `/tmp/settings.json`.
  - [ ] `test_save_path_rejects_traversal` and `test_save_path_rejects_separator_in_name` must always run and assert rejection.
  - [ ] `test_save_path_accepts_debug_test_override` becomes a normal test verifying save succeeds for a simple name within the test base.

### `./CMakePresets.json`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Remove `DIR2MD_DEBUG_TEST_PATH` cache variable from the `debug` preset.
- **Logic Modifications Required:**
  - [ ] Ensure no preset enables the compile-time flag.

### `./src/backend/CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Remove conditional `target_compile_definitions(dir2md_backend PUBLIC DIR2MD_DEBUG_TEST_PATH)` block.
- **Logic Modifications Required:**
  - [ ] Backend library no longer receives the compile-time test flag.

## 3. Boundary & Edge Case Analysis
*Detail how system boundaries, errors, and edge cases will be handled structurally.*
- **Error Handling:** 
  - `resolvePersistencePath` returns empty `QString` for invalid paths; `save_to_file`/`load_from_file` emit warnings and return false. Test base override must not bypass validation for paths outside the base.
  - Test harness must guarantee `clearTestBaseDirectory` runs even if tests fail, to avoid leaking state to subsequent runs.
- **Security & Permissions:** 
  - Production path sandbox remains enforced when test base is not set. Test-only API must be clearly documented and not exposed to frontend/CLI consumers.
  - Test base directory is created per test run and is temporary; no production data is written there.
- **Performance / Scale Impact:** 
  - Static test base state adds negligible overhead. No additional I/O or database queries introduced.

## 4. Verification Checklist
*A concrete list of what needs to be verified during/after implementation to ensure the analysis was correct.*
- [ ] All save/load tests pass in Release build without `DIR2MD_DEBUG_TEST_PATH`.
- [ ] Path-restriction tests pass in both Debug and Release presets.
- [ ] No `#ifdef DIR2MD_DEBUG_TEST_PATH` remains in `src/` or `test/`.
- [ ] `SettingsManager::setTestBaseDirectory` is clearly marked as test-only in header documentation.
- [ ] `CMakePresets.json` no longer contains `DIR2MD_DEBUG_TEST_PATH`.
- [ ] `src/backend/CMakeLists.txt` no longer adds the compile definition.
- [ ] Test harness custom `main()` sets and clears test base directory around `QTest::qExec`.
- [ ] Tests no longer reference `tempDir.path()` directly.
- [ ] CI runs tests for both Debug and Release presets successfully.
