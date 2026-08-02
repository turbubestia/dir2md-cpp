# Implementation Plan: 1-save-load-settings

[Analysis Reference](./1-save-load-settings.plan.analysis.md)

---

## Overview

This plan describes the step-by-step implementation of save/load settings functionality for the `SettingsManager` class in the backend core library. The feature adds JSON serialization/deserialization to persist application settings to disk and restore them, using atomic file writes via `QSaveFile`.

**Scope:** Backend Core only (`src/backend/core/`). No frontend, CLI, schema, or CMake changes required.

---

## Phase 1: Header Modifications — Add Signal Declaration

**References:** Analysis Section 2 (Component & File Impact Map), settings_manager.hpp structural changes.

### Step 1.1 — Add `settingsSaved` signal to SettingsManager

- **File:** `src/backend/core/settings_manager.hpp`
- **Action:** Add a new signal declaration inside the existing `signals:` block:
  - Declare `void settingsSaved(const QString &path);`
- **Exit Criterion:** The header compiles without errors; the signal is visible to Qt's MOC system.

### Step 1.2 — Add necessary includes to the header (if not already transitively included)

- **File:** `src/backend/core/settings_manager.hpp`
- **Action:** Verify that `#include <QJsonObject>` and `#include <QJsonDocument>` are present. If they are not, add them. These are needed if any downstream code includes the header and uses JSON types in conjunction with SettingsManager.
- **Exit Criterion:** All required Qt headers are included; no missing-type compilation errors.

**Validation Command:**
```bash
cmake --build --preset debug --target dir2md_backend
```

---

## Phase 2: Implementation — Anonymous Namespace Helpers

**References:** Analysis Section 2 (settings_manager.cpp structural changes), insertNestedValue and flattenJsonObject logic modifications.

### Step 2.1 — Add JSON/file I/O includes to settings_manager.cpp

- **File:** `src/backend/core/settings_manager.cpp`
- **Action:** Add the following includes after the existing `#include "settings_manager.hpp"`:
  - `#include <QJsonDocument>`
  - `#include <QJsonObject>`
  - `#include <QJsonArray>`
  - `#include <QFile>`
  - `#include <QSaveFile>`
  - `#include <QDebug>`
- **Exit Criterion:** All includes are present; no duplicate includes introduced.

### Step 2.2 — Create anonymous namespace with `insertNestedValue` helper

- **File:** `src/backend/core/settings_manager.cpp`
- **Action:** After the existing includes and before `namespace dir2md::backend`, create an anonymous namespace containing a static function:
  - **Signature:** `void insertNestedValue(QJsonObject &obj, const QStringList &pathParts, const QVariant &value)`
  - **Behavior:**
    1. If `pathParts` has exactly one element, set the value directly in `obj` using `obj.insert(pathParts[0], QJsonValue::fromVariant(value))`.
    2. If `pathParts` has more than one element, check if `obj[pathParts[0]]` is already a `QJsonObject`. If not, create one: `obj.insert(pathParts[0], QJsonObject{})`.
    3. Recurse: call `insertNestedValue` with the nested object, the remaining path parts (skip first), and the value.
  - **Separator semantics:** Path parts are already split on `/` by the caller. The `.` character is never treated as a separator — it remains part of the key name.
- **Exit Criterion:** Function compiles; logic correctly nests values at arbitrary depth.

### Step 2.3 — Create anonymous namespace with `flattenJsonObject` helper

- **File:** `src/backend/core/settings_manager.cpp`
- **Action:** In the same anonymous namespace, add a static function:
  - **Signature:** `void flattenJsonObject(const QJsonObject &obj, const QString &prefix, QHash<QString, QVariant> &out)`
  - **Behavior:**
    1. Iterate over all key-value pairs in `obj`.
    2. For each pair, build the full path: if `prefix` is empty, use the key as-is; otherwise, concatenate `prefix + "/" + key`.
    3. If the value is a `QJsonObject`, recurse with the new prefix.
    4. If the value is NOT a `QJsonObject` (i.e., a leaf), convert it to `QVariant` using `value.toVariant()` and insert into `out` with the full path as key.
- **Exit Criterion:** Function compiles; correctly flattens nested JSON back to flat key-value pairs with `/` separators.

**Validation Command:**
```bash
cmake --build --preset debug --target dir2md_backend
```

---

## Phase 3: Implementation — save_to_file Method

**References:** Analysis Section 2 (save_to_file logic), Analysis Section 3 (Error Handling for write failure, Atomic write pattern).

### Step 3.1 — Implement `SettingsManager::save_to_file(const QString &filePath)`

- **File:** `src/backend/core/settings_manager.cpp`
- **Action:** Add the method implementation after the existing methods in the `dir2md::backend` namespace:
  - **Step 3.1.1:** Group `m_values` by category. Iterate over all key-value pairs in `m_values`. For each key, look up its schema via `schema(key)`. If found, use `schema->category`; if not found, use `"General"` as the default category. Build a `QJsonObject` where top-level keys are categories and values are nested objects built by calling `insertNestedValue`.
  - **Step 3.1.2:** Wrap the category-rooted `QJsonObject` in a `QJsonDocument`: `QJsonDocument doc(categoryRoot)`.
  - **Step 3.1.3:** Serialize with pretty-printing: `doc.toJson(QJsonDocument::Indented)` (use 2-space indentation via `doc.setObject()` and manual formatting if needed, or use `QJsonDocument::Indented` which produces standard indentation).
  - **Step 3.1.4:** Write atomically using `QSaveFile`:
    1. Construct `QSaveFile saveFile(filePath)`.
    2. Call `saveFile.open(QIODevice::WriteOnly)`. Return `false` if open fails.
    3. Write the JSON bytes: `saveFile.write(jsonBytes)`. Check for write errors; return `false` on failure.
    4. Call `saveFile.commit()`. If commit returns `false`, return `false` (atomic rollback occurred).
  - **Step 3.1.5:** On successful commit, emit `settingsSaved(filePath)`.
  - **Step 3.1.6:** Return `true` on success.
- **Exit Criterion:** Method compiles; produces valid, pretty-printed JSON with categories as top-level keys.

**Validation Command:**
```bash
cmake --build --preset debug --target dir2md_backend
```

---

## Phase 4: Implementation — load_from_file Method

**References:** Analysis Section 2 (load_from_file logic), Analysis Section 3 (Error Handling for missing file, malformed JSON, invalid values, unknown keys).

### Step 4.1 — Implement `SettingsManager::load_from_file(const QString &filePath)`

- **File:** `src/backend/core/settings_manager.cpp`
- **Action:** Add the method implementation after `save_to_file` in the `dir2md::backend` namespace:
  - **Step 4.1.1:** Open file with `QFile`:
    1. Construct `QFile file(filePath)`.
    2. Call `file.open(QIODevice::ReadOnly | QIODevice::Text)`. Return `false` if open fails (missing/unreadable file — no warning logged, this is expected on first run).
  - **Step 4.1.2:** Read and parse:
    1. Read all bytes: `QByteArray data = file.readAll()`. Close the file.
    2. Parse with `QJsonDocument doc = QJsonDocument::fromJson(data)`.
    3. If `doc.isNull()` or `doc.isEmpty()`, log via `qWarning() << "Failed to parse JSON:"` with details from `QJsonParseError`, then return `false`.
    4. Extract the root object: `QJsonObject root = doc.object()`. Verify it is not empty; if empty, return `false`.
  - **Step 4.1.3:** Flatten the nested JSON:
    1. Create a temporary `QHash<QString, QVariant> loadedValues`.
    2. Call `flattenJsonObject(root, QString(), loadedValues)` to convert nested JSON → flat key-value pairs.
  - **Step 4.1.4:** Clear existing state and populate:
    1. Call `m_values.clear()` (full replacement semantics).
    2. Iterate over each key-value pair in `loadedValues`.
    3. For each pair, look up the schema via `schema(key)`:
       - **If schema exists:** Validate the value using `schema->isValid(value)`.
         - If valid: insert into `m_values`, emit `settingChanged(key, value)`.
         - If invalid: log via `qWarning() << "Invalid value for key" << key << "skipping"`, continue to next pair.
       - **If schema does NOT exist:** Silently skip (no warning, no insertion). This handles unknown/extra keys in the JSON file.
  - **Step 4.1.5:** Return `true` on success.
- **Exit Criterion:** Method compiles; correctly loads, validates, and populates settings from a JSON file.

**Validation Command:**
```bash
cmake --build --preset debug --target dir2md_backend
```

---

## Phase 5: Unit Tests — Save/Load Functionality

**References:** Analysis Section 3 (Verification Checklist items 1-7, 11, 13).

### Step 5.1 — Add test declarations to the test header

- **File:** `test/backend/core/setting_manager_test.hpp`
- **Action:** Add new test method declarations in the `private slots:` section:
  - `void test_save_to_file_creates_json();`
  - `void test_save_to_file_nested_keys();`
  - `void test_save_to_file_unregistered_keys_general_category();`
  - `void test_load_from_file_missing_returns_false();`
  - `void test_load_from_file_malformed_json();`
  - `void test_load_from_file_valid_replaces_values();`
  - `void test_roundtrip_preserves_types();`
  - `void test_load_from_file_invalid_value_skipped();`
  - `void test_load_from_file_unknown_key_silently_ignored();`
  - `void test_settings_saved_signal_emitted();`

### Step 5.2 — Implement `test_save_to_file_creates_json`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Creates a `SettingsManager`, sets a few values (string, int, bool).
  2. Calls `save_to_file()` with a temporary file path (use `QTemporaryDir` to get a clean path).
  3. Verifies the file exists and was written.
  4. Reads the file content and parses it as JSON.
  5. Asserts the JSON is valid and contains the expected top-level keys (categories or `"General"` for unregistered keys).
- **Analysis Reference:** Verification Checklist items 1, 4.

### Step 5.3 — Implement `test_save_to_file_nested_keys`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Sets values with `/` in their keys (e.g., `"editor/tab_size"`, `"editor/indent_style"`).
  2. Saves to file.
  3. Loads the JSON and verifies that keys are nested under their category with `/` creating hierarchy (e.g., `{ "General": { "editor": { "tab_size": 4 } } }`).
- **Analysis Reference:** Verification Checklist item 3.

### Step 5.4 — Implement `test_save_to_file_unregistered_keys_general_category`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Sets values WITHOUT registering schemas for them.
  2. Saves to file.
  3. Verifies the JSON contains a `"General"` top-level key containing those unregistered keys.
- **Analysis Reference:** Verification Checklist item 4.

### Step 5.5 — Implement `test_save_to_file_dot_keys_not_split`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Sets a value with `.` in the key (e.g., `"file.name"`).
  2. Saves to file.
  3. Verifies the key remains as a single flat key within its category — NOT split on `.`.
- **Analysis Reference:** Verification Checklist item 2.

### Step 5.6 — Implement `test_load_from_file_missing_returns_false`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Creates a `SettingsManager` with some values set.
  2. Calls `load_from_file()` with a non-existent file path.
  3. Verifies it returns `false`.
  4. Verifies `m_values` is unchanged (no modification on missing file).
- **Analysis Reference:** Verification Checklist item 7.

### Step 5.7 — Implement `test_load_from_file_malformed_json`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Creates a temporary file containing invalid JSON (e.g., `{ broken json }`).
  2. Calls `load_from_file()` with that path.
  3. Verifies it returns `false`.
  4. Verifies a warning was logged (check stderr or use QSignalSpy on qWarning if needed).
- **Analysis Reference:** Verification Checklist item 8.

### Step 5.8 — Implement `test_load_from_file_valid_replaces_values`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Creates a `SettingsManager`, sets initial values.
  2. Saves those values to a file.
  3. Changes some values in memory.
  4. Calls `load_from_file()` with the saved file.
  5. Verifies all values are restored to what was saved (full replacement, no merge).
- **Analysis Reference:** Verification Checklist item 5.

### Step 5.9 — Implement `test_roundtrip_preserves_types`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Creates a `SettingsManager`, sets values of all supported types (int, double, QString, bool).
  2. Saves to file.
  3. Clears the manager (or creates a new one).
  4. Loads from the saved file.
  5. Verifies each key/value pair matches exactly, including type (use `QVariant::type()` comparison).
- **Analysis Reference:** Verification Checklist item 6.

### Step 5.10 — Implement `test_load_from_file_invalid_value_skipped`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Registers a schema with constraints (e.g., int between 1 and 32).
  2. Manually writes a JSON file with one valid value and one invalid value (outside constraint range).
  3. Calls `load_from_file()`.
  4. Verifies the valid value is loaded and the invalid value is skipped.
- **Analysis Reference:** Verification Checklist item 9.

### Step 5.11 — Implement `test_load_from_file_unknown_key_silently_ignored`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Registers only one schema key.
  2. Manually writes a JSON file containing both the registered key and an unknown key (not in any schema).
  3. Calls `load_from_file()`.
  4. Verifies the registered key is loaded but the unknown key is NOT present in `m_values`.
  5. Verifies no warning was emitted for the unknown key.
- **Analysis Reference:** Verification Checklist item 10.

### Step 5.12 — Implement `test_settings_saved_signal_emitted`

- **File:** `test/backend/core/setting_manager_test.cpp`
- **Action:** Write a test that:
  1. Creates a `SettingsManager`, sets values, saves to file.
  2. Uses `QSignalSpy` to monitor the `settingsSaved` signal.
  3. Verifies the signal was emitted exactly once with the correct file path argument.
- **Analysis Reference:** Verification Checklist item 11.

**Validation Command:**
```bash
cmake --build --preset debug --target backend_core_test
ctest --preset debug --output-on-failure
```

---

## Phase 6: Coverage Testing

**References:** Project build instructions (Coverage section in workspace memory).

### Step 6.1 — Build with coverage instrumentation

- **Action:** Configure and build the coverage preset:
  ```bash
  cmake --preset debug-coverage
  cmake --build --preset debug-coverage
  ```
- **Exit Criterion:** All targets build successfully with coverage flags (`--coverage`).

### Step 6.2 — Run tests under coverage

- **Action:** Execute the test binary from the coverage build directory:
  ```bash
  cd build/cmake-debug-coverage/test/backend/core
  ./backend_core_test.exe
  ```
- **Exit Criterion:** All tests pass; `.profraw` files are generated in the build directory.

### Step 6.3 — Merge profraw and generate coverage report

- **Action:** Run the LLVM/MinGW coverage toolchain:
  ```bash
  llvm-profdata merge -o default.profdata *.profraw
  llvm-cov show build/cmake-debug-coverage/test/backend/core/backend_core_test.exe -instr-profile=default.profdata
  ```
- **Exit Criterion:** Coverage report shows >90% branch coverage for `settings_manager.cpp` (new save/load code paths).

**Validation Command:**
```bash
cmake --build --preset debug-coverage --target backend_core_test
ctest --preset debug --test-dir build/cmake-debug-coverage --output-on-failure
```

---

## Phase 7: Integration Verification & Final Checks

**References:** Analysis Section 3 (Verification Checklist items 12-13), Analysis Section 1 (New Patterns — atomic write).

### Step 7.1 — Verify anonymous namespace encapsulation

- **Action:** Confirm that `insertNestedValue` and `flattenJsonObject` are not visible outside `settings_manager.cpp`:
  - They are declared inside an anonymous namespace, so they have internal linkage by C++ rules.
  - No declarations of these functions appear in any header file.
- **Exit Criterion:** Code review confirms no external visibility; grep for function names returns matches only in `settings_manager.cpp`.

### Step 7.2 — Verify QSaveFile atomic write behavior

- **Action:** Confirm that `save_to_file` uses `QSaveFile` correctly:
  - File is opened, written to, then committed.
  - On commit failure, the temporary file is automatically removed by `QSaveFile`'s destructor (no partial/corrupt file left behind).
- **Exit Criterion:** Code review confirms proper use of `QSaveFile` pattern; no direct `QFile::remove()` calls needed.

### Step 7.3 — Full build verification across all presets

- **Action:** Build and test all presets:
  ```bash
  cmake --build --preset debug
  ctest --preset debug --output-on-failure
  cmake --build --preset release
  ```
- **Exit Criterion:** All targets (backend, frontend, cli) build successfully in both Debug and Release modes; all tests pass.

### Step 7.4 — Verify no regressions in existing functionality

- **Action:** Run the full test suite including any existing tests that exercise `SettingsManager` get/set/schema APIs:
  ```bash
  ctest --preset debug --output-on-failure
  ```
- **Exit Criterion:** All pre-existing tests continue to pass; no new warnings or errors in diagnostics.

**Validation Command:**
```bash
cmake --build --preset debug && ctest --preset debug --output-on-failure
cmake --build --preset release
```

---

## Summary of Files Modified

| File | Change Type | Description |
|------|-------------|-------------|
| `src/backend/core/settings_manager.hpp` | Modify | Add `settingsSaved` signal |
| `src/backend/core/settings_manager.cpp` | Modify | Add includes, anonymous namespace helpers, `save_to_file`, `load_from_file` |
| `test/backend/core/setting_manager_test.hpp` | Modify | Add 10 new test method declarations |
| `test/backend/core/setting_manager_test.cpp` | Modify | Implement 10 new test methods |

## Summary of Files NOT Modified

| File | Reason |
|------|--------|
| `src/backend/core/CMakeLists.txt` | QJson/QSaveFile are part of Qt6::Core, already linked |
| `src/backend/core/core_schema.cpp` | Schema definitions unchanged |
| `src/backend/core/core_schema.hpp` | No schema changes needed |
| `src/frontend/` | No UI changes required |
| `src/cli/` | No CLI changes required |
