# Implementation Plan: 4-improve-setting-manager

[Analysis Reference](./4-improve-setting-manager.plan.analysis.md)

---

## Overview

This plan translates the architectural analysis into a chronological, phase-by-phase blueprint for strengthening the `SettingsManager` subsystem. Every step explicitly references the corresponding requirement from the analysis document. The implementation touches five files across three directories:

- **Header:** `src/backend/core/settings_manager.hpp`
- **Implementation:** `src/backend/core/settings_manager.cpp`
- **Schema (review):** `src/backend/core/core_schema.cpp`
- **Tests:** `test/backend/core/setting_manager_test.hpp`, `test/backend/core/setting_manager_test.cpp`
- **Build:** `CMakePresets.json`, `test/backend/core/CMakeLists.txt`

No frontend, CLI, or thread-safety contracts are modified. All changes remain within the QtCore-only backend boundary.

---

## Phase 1: CMake Configuration — Debug Test-Path Compile Definition

**Analysis Reference:** Section 2 (Component & File Impact Map) — `CMakePresets.json`, `test/backend/core/CMakeLists.txt`; Section 3 (Security & Permissions) — debug-only bypass; Verification Checklist items 15–16.

### Objective
Introduce a compile-time definition (`DIR2MD_DEBUG_TEST_PATH`) that enables explicit test-file-path overrides in production builds only when the debug preset is used. Release builds must never expose this capability.

### Steps

1. **Modify `CMakePresets.json`:** Add a cache variable `DIR2MD_DEBUG_TEST_PATH` with value `"ON"` to the `debug` configure preset's `cacheVariables`. Do not add it to `release`, `debug-coverage`, or any other preset.

2. **Modify `test/backend/core/CMakeLists.txt`:** After the `target_link_libraries` block for `backend_core_test`, add `target_compile_definitions(backend_core_test PRIVATE DIR2MD_DEBUG_TEST_PATH)` so the test executable is compiled with the debug bypass enabled. Do not propagate this definition to the `dir2md_backend` library target or any frontend/CLI target.

3. **Verify:** Confirm that `CMakePresets.json` contains `DIR2MD_DEBUG_TEST_PATH` only in the `debug` preset's `cacheVariables`. Confirm that `test/backend/core/CMakeLists.txt` applies the definition exclusively to `backend_core_test`.

### Exit Criterion
- The debug preset defines `DIR2MD_DEBUG_TEST_PATH=ON`; release and coverage presets do not.
- Only `backend_core_test` receives the compile definition; `dir2md_backend`, frontend, and CLI targets are unaffected.

### Validation Command
```bash
cmake --preset debug     -D DIR2MD_DEBUG_TEST_PATH=ON  # should appear in compile commands
cmake --preset release    # DIR2MD_DEBUG_TEST_PATH must NOT appear
```

---

## Phase 2: SettingSchema::isValid — Strengthened Validation

**Analysis Reference:** Section 2 (Component & File Impact Map) — `settings_manager.hpp` / `settings_manager.cpp`; Section 3 (Boundary & Edge Case Analysis) — conversion, range, enum validation; Verification Checklist items 3–4.

### Objective
Replace the current permissive `isValid()` with strict, schema-backed validation that checks conversion success, numeric bounds against the converted value, and precise enum membership.

### Steps

1. **Modify `src/backend/core/settings_manager.cpp`** (the implementation of `SettingSchema::isValid`):
   - After `val.canConvert(type)` passes, perform the actual conversion and validate the **converted result**, not just the potential path.
   - For `int` and `double` types: convert the value, then compare the converted numeric against `min` and `max`. Use the converted value for bounds comparison to avoid floating-point edge cases with `QVariant::toDouble()`.
   - For enum types (`enumOptions` is non-empty): validate that the value's string representation matches one of the declared `enumOptions` entries exactly. Do not use a broad type-category check.
   - Return `false` for any validation failure; never throw.

  **User:The Qt documentation says**
  ```
  bool QVariant::canConvert(QMetaType type) const
  Returns true if the variant's type can be cast to the requested type, type. Such casting is done automatically when calling the toInt(), toBool(), ... methods.
  Note this function operates only on the variant's type, not the contents. It indicates whether there is a conversion path from this variant to type, not that the conversion will succeed when attempted.
  ```
  **User: This means canConvert only state if we can cast from/to the given type, but does not guarranty that convert() will success. We must check the return bool value of convert(). To convert enums to string it is also possible (and prefered) to do this:**
  ```cpp
  #include <QMetaEnum>

  Status::State state = Status::Completed;

  // Get key string using QMetaEnum
  QMetaEnum metaEnum = QMetaEnum::fromType<Status::State>();
  const char* key = metaEnum.valueToKey(state);

  qDebug() << key; // Output: "Completed"
  ```


2. **Modify `src/backend/core/settings_manager.hpp`** (if needed): Ensure `SettingSchema::enumOptions` is documented as storing symbolic string representations of allowed enum values. No structural change to the struct itself is required unless enum metadata needs a Qt meta-enum type field — defer adding that until Phase 6 if enum schemas are introduced.

3. **Verify:** The existing `core/max_threads` schema (int, min=1, max=32) must still validate correctly for values within range and reject values outside. No changes to `core_schema.cpp` are required at this point.

### Exit Criterion
- `isValid()` rejects values that pass `canConvert()` but fail bounds or enum checks.
- Numeric bounds are checked against the converted value, not the raw `QVariant`.
- Enum validation uses exact string membership in `enumOptions`, not a broad type check.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug
# Run: ctest --preset debug --output-on-failure
```

---

## Phase 3: SettingsManager::set — Key Syntax & Schema-Backed Assignment

**Analysis Reference:** Section 2 (Component & File Impact Map) — `settings_manager.hpp` / `settings_manager.cpp`; Section 3 (Error Handling, Security); Verification Checklist items 1–2.

### Objective
Enforce key syntax validation and reject unknown keys in `set()`. The manager must no longer allow schema-less active values to be set or persisted.

### Steps

1. **Add internal key-validation helper** in the anonymous namespace of `settings_manager.cpp`:
   - Create a static function `isValidKeySyntax(const QString &key)` that returns `false` for: empty strings, keys containing whitespace, keys with leading/trailing `/`, keys with repeated `//` separators, and keys with any empty segment after splitting on `/`.
   - Return `true` only when all segments are non-empty and the key contains no whitespace.

2. **Modify `SettingsManager::set()`:**
   - First call `isValidKeySyntax(key)`. If it returns `false`, return `false` immediately without mutation or exception.
   - Check that `m_schemaRegistry.contains(key)`. If the key is not registered, return `false` (unknown keys are rejected).
   - Look up the schema and call `schema->isValid(value)`. If validation fails, return `false` without mutation.
   - Only after all checks pass: insert the value into `m_values`, emit `settingChanged(key, value)`, and return `true`.

3. **Update existing tests** in `test/backend/core/setting_manager_test.hpp` / `.cpp`:
   - Replace `test_settings_manager_get_set()` to register schemas before calling `set()`. The test must use registered keys like `core/tool_path` and `core/max_threads` (via `CoreSchema::registerSchemas`).
   - Replace `test_settings_manager_active_values()` similarly — register schemas, then set values.
   - Replace `test_settings_manager_signal()` to use a registered schema key.

4. **Verify:** Unregistered keys are rejected by `set()`. Valid registered keys with valid values still work. No exceptions are thrown on invalid input.

### Exit Criterion
- `set()` rejects: empty keys, whitespace-containing keys, leading/trailing/repeated separators, unknown (unregistered) keys, and values failing schema validation.
- All existing tests that previously set unregistered keys must now register schemas first.
- The boolean return contract is preserved; no new signals are introduced.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
```

---

## Phase 4: SettingsManager::registerSchema — Schema Replacement with Revalidation

**Analysis Reference:** Section 1 (Data Flow Changes); Section 2 (Component & File Impact Map) — `settings_manager.hpp`; Verification Checklist items 5–6.

### Objective
When `registerSchema()` replaces an existing schema, revalidate all active values against the new schema. Compatible overrides remain; incompatible ones are removed so reads fall back to the new default. Effective-value changes trigger the existing `settingChanged` signal.

### Steps

1. **Modify `SettingsManager::registerSchema()`:**
   - If the key already exists in `m_schemaRegistry`, store the old schema for comparison.
   - Insert/replace the schema in `m_schemaRegistry`.
   - If a previous schema existed with the same key, check whether any active value in `m_values` uses that key. For each such active value:
     - Call the **new** schema's `isValid()` on the active value.
     - If valid, keep it in `m_values`.
     - If invalid, remove it from `m_values` and emit `settingChanged(key, get(key))` so consumers observe the fallback to the new default.
   - If no previous schema existed (new registration), do nothing extra — just insert.

2. **Add test declaration** in `test/backend/core/setting_manager_test.hpp`:
   - `test_schema_replacement_retains_compatible()` — register a schema, set a valid value, replace with a compatible schema (same type, same constraints), verify the value persists.
   - `test_schema_replacement_removes_incompatible()` — register a schema, set a value, replace with an incompatible schema (different type or tighter constraints), verify the value is removed and `get()` returns the new default.
   - `test_schema_replacement_emits_signal_on_change()` — connect to `settingChanged`, replace a schema such that an active value becomes invalid, verify the signal fires with the new default value.

3. **Verify:** Schema replacement is deterministic. Compatible values survive; incompatible ones are removed. The existing signal contract handles both cases without introducing a new event type.

### Exit Criterion
- `registerSchema()` replaces deterministically and revalidates active values for the same key.
- Incompatible overrides are removed; `get()` returns the new schema's default.
- `settingChanged` fires when replacement changes the effective value. No new signals.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug -R schema_replacement --output-on-failure
```

---

## Phase 5: Persistence Path Restriction — Production Config Directory

**Analysis Reference:** Section 2 (Component & File Impact Map) — `settings_manager.hpp` / `settings_manager.cpp`; Section 3 (Security & Permissions); Verification Checklist items 13–14.

### Objective
Restrict production save/load to the application configuration directory `{QDir::homePath()}/.config/dir2md`. Reject empty names, absolute paths, parent traversal (`..`), separators in filenames, and normalization escapes. Enable explicit test-path override only under `DIR2MD_DEBUG_TEST_PATH`.

### Steps

1. **Add internal path-validation helper** in the anonymous namespace of `settings_manager.cpp`:
   - Create a static function `resolvePersistencePath(const QString &name)` that:
     - Returns an empty string for empty input.
     - Returns an empty string if the name is an absolute path (`QDir::isAbsolute()`).
     - Returns an empty string if the name contains `/` or `\` separators (filenames must be simple names, not paths).
     - Constructs the candidate as `QDir::homePath() + "/.config/dir2md/" + name`.
     - Normalizes the path via `QDir::cleanPath()`.
     - Verifies the normalized path starts with the base directory `QDir::homePath() + "/.config/dir2md"`. If not, returns empty string (traversal escape).
     - Returns the normalized path if all checks pass.

2. **Add internal test-path helper** in the anonymous namespace:
   - Create a static function `resolveTestPath(const QString &filePath)` guarded by `#ifdef DIR2MD_DEBUG_TEST_PATH`:
     - If the compile definition is active, return `filePath` as-is (allowing tests to specify isolated temporary paths).
     - If not active, return empty string (test bypass is unavailable in production builds).

3. **Modify `SettingsManager::save_to_file()`:**
   - First attempt `resolveTestPath(filePath)`. If non-empty, use it directly (test path bypass).
   - Otherwise, call `resolvePersistencePath(filePath)`. If the result is empty, print a warning to stdout (`qDebug()` or `qWarning()`) and return `false`.
   - Proceed with the existing atomic write logic using the resolved path.

4. **Modify `SettingsManager::load_from_file()`:**
   - Apply the same resolution logic: test-path bypass first, then production path restriction.
   - If resolution fails, print a warning to stdout and return `false`.
   - The existing file open/read/parse logic remains unchanged after path resolution.

5. **Add test declarations** in `test/backend/core/setting_manager_test.hpp`:
   - `test_save_path_rejects_empty_name()` — pass empty string, expect `false`.
   - `test_save_path_rejects_absolute_path()` — pass `/tmp/settings.json`, expect `false`.
   - `test_save_path_rejects_traversal()` — pass `../../etc/passwd`, expect `false`.
   - `test_save_path_rejects_separator_in_name()` — pass `dir/file.json`, expect `false`.
   - `test_save_path_accepts_debug_test_override()` — under `DIR2MD_DEBUG_TEST_PATH`, pass an explicit temp path, expect success.

6. **Verify:** Production builds reject all unsafe paths. Debug builds with the compile definition accept explicit test paths. Path normalization prevents traversal escapes.

### Exit Criterion
- Production save/load is confined beneath `{QDir::homePath()}/.config/dir2md`.
- Empty names, absolute paths, `..` traversal, separator-containing filenames are rejected.
- `DIR2MD_DEBUG_TEST_PATH` enables explicit test paths only when compiled in.
- Path resolution failures print to stdout and return `false` without altering state.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug -R path_reject --output-on-failure
```

---

## Phase 6: JSON Traversal Refactoring — Stack-Based Iterative Helpers

**Analysis Reference:** Section 2 (Component & File Impact Map) — `settings_manager.cpp`; Section 3 (Performance / Scale Impact); Verification Checklist item 7.

### Objective
Replace the recursive `insertNestedValue` and `flattenJsonObject` helpers with iterative stack-based traversals that handle deeply nested JSON without call-stack growth. Preserve existing behavior for intermediate scalar replacement during insertion.

### Steps

1. **Replace `insertNestedValue`** in the anonymous namespace of `settings_manager.cpp`:
   - Implement an iterative version using a `QStack<QJsonObject*>` or equivalent stack-based approach.
   - Push the root object pointer onto the stack.
   - For each path segment (except the last), navigate into the nested object. If the segment points to a scalar value already present, replace that scalar with a new empty object and update the parent reference.
   - At the final segment, insert the `QJsonValue::fromVariant(value)`.
   - Handle deep paths (e.g., 100+ segments) without stack overflow risk.

2. **Replace `flattenJsonObject`** in the anonymous namespace of `settings_manager.cpp`:
   - Implement an iterative version using a stack of `(QJsonObject, QString prefix)` pairs.
   - Push the root object with an empty prefix onto the stack.
   - While the stack is non-empty, pop the top pair. For each key-value pair in the object:
     - Compute the full path as `prefix.isEmpty() ? key : prefix + "/" + key`.
     - If the value is an object, push `(value.toObject(), fullPath)` onto the stack.
     - Otherwise, insert `(fullPath, value.toVariant())` into the output hash.

3. **Add test declaration** in `test/backend/core/setting_manager_test.hpp`:
   - `test_insert_nested_scalar_replacement()` — set a key where an intermediate path segment already holds a scalar; verify the scalar is replaced by a nested object and the final value is inserted correctly.
   - `test_flatten_deeply_nested_json()` — create a JSON document with extreme nesting depth (e.g., 50+ levels); verify flattening completes without stack overflow and produces correct flat keys.

4. **Verify:** Both helpers produce identical output to their recursive predecessors for normal inputs. Deep paths are handled iteratively without recursion limits.

### Exit Criterion
- `insertNestedValue` uses iterative stack traversal; intermediate scalars are replaced by nested objects.
- `flattenJsonObject` uses iterative stack traversal; no recursion-dependent behavior.
- Output matches the previous recursive implementation for all existing test cases.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug -R "insert_nested|flatten_deep" --output-on-failure
```

---

## Phase 7: Category-Preserving Load — Atomic Commit & Tolerant Entry Handling

**Analysis Reference:** Section 1 (Data Flow Changes); Section 2 (Component & File Impact Map) — `settings_manager.cpp`; Section 3 (Error Handling, Format compatibility); Verification Checklist items 9–12.

### Objective
Retain the category as part of the lookup path during load. Ignore unknown/invalid entries with warnings while continuing to process valid entries. Stage accepted values in a candidate map and replace `m_values` atomically after file-level validation. Treat malformed JSON, unreadable files, and non-object top-level documents as atomic failures.

### Steps

1. **Modify the category retention logic in `load_from_file()`:**
   - After `flattenJsonObject(root, QString(), loadedValues)`, each flat key has the form `"Category/nested/key"`.
   - Split the flat key on `/` and validate that the first segment (category) matches the schema's registered category for the remaining path segments. Do NOT unconditionally strip the first segment.
   - If the category does not match the schema, skip the entry with a warning (`qWarning() << "Category mismatch for" << flatKey`).

2. **Add per-entry tolerant handling in `load_from_file()`:**
   - For each flattened entry:
     - Validate the key syntax using `isValidKeySyntax()` (the helper from Phase 3). If invalid, skip with a warning.
     - Look up the schema using the full category/key path. If no schema matches, skip with a warning (`qWarning() << "Unknown key" << schemaKey`).
     - Validate the value against the schema via `schema->isValid(value)`. If invalid, skip with a warning.
     - Convert and stage the typed value in a **candidate map** (`QHash<QString, QVariant> candidateValues`), not directly into `m_values`.

3. **Implement atomic state replacement in `load_from_file()`:**
   - After all entries are examined and the candidate map is fully populated:
     - Compare `candidateValues` against the existing `m_values` to determine which keys changed, were added, or disappeared.
     - Replace `m_values` with `candidateValues` in one assignment.
     - For each key that was added or changed, emit `settingChanged(key, candidateValues[key])`.
     - For each key that existed in `m_values` but not in `candidateValues`, emit `settingChanged(key, get(key))` so consumers observe the fallback to the default.

4. **Add file-level failure handling in `load_from_file()`:**
   - If the file cannot be opened/read: return `false` (existing behavior, no state change).
   - If JSON parsing fails (`doc.isNull()` or parse error): print a warning to stdout and return `false` (no state change).
   - If the top-level document is not an object (`!doc.isObject()`): print a warning to stdout and return `false` (no state change).
   - In all failure cases, `m_values` remains completely unchanged.

5. **Add test declarations** in `test/backend/core/setting_manager_test.hpp`:
   - `test_load_category_mismatch_ignored()` — write JSON with a category that does not match the schema's registered category; verify the entry is skipped.
   - `test_load_mixed_valid_invalid_entries()` — write JSON with valid, invalid (out-of-range), and unknown keys; verify only valid entries are loaded and invalid ones disappear after save.
   - `test_load_malformed_json_fails_atomically()` — write malformed JSON; verify load returns `false` and active state is unchanged.
   - `test_load_non_object_top_level_fails()` — write a JSON array as the top-level document; verify load returns `false`.
   - `test_load_successful_replaces_atomically()` — register schemas, set values, save, change values, load from file; verify all loaded values are applied atomically and signals fire for disappeared keys.

6. **Verify:** Unknown and invalid entries produce warnings but do not prevent valid entries from loading. Malformed/unreadable/non-object documents fail atomically with no state change. Successful load replaces the active map and emits `settingChanged` for changed, added, and disappeared values.

### Exit Criterion
- Category is retained during load and validated against the schema's registered category.
- Unknown/invalid entries are skipped with warnings; valid entries continue loading.
- Accepted values are staged in a candidate map; `m_values` is replaced atomically after full validation.
- File-level failures (malformed JSON, non-object top level, unreadable files) return `false` and preserve existing state.
- `settingChanged` fires for added, changed, and disappeared effective values. No new signal types.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug -R "load_" --output-on-failure
```

---

## Phase 8: Save Schema-Backed Only — Reject Schema-Less Values

**Analysis Reference:** Section 2 (Component & File Impact Map) — `settings_manager.cpp`; Section 3 (Format compatibility); Verification Checklist item 2.

### Objective
Ensure `save_to_file()` serializes only schema-backed active values. Previously saved schema-less values must not reappear after a save cycle.

### Steps

1. **Modify the iteration logic in `save_to_file()`:**
   - Before processing each key-value pair from `m_values`, check that `m_schemaRegistry.contains(key)`. If the key is not registered, skip it silently (do not serialize).
   - This ensures that only values backed by a registered schema are persisted.

2. **Add test declaration** in `test/backend/core/setting_manager_test.hpp`:
   - `test_save_rejects_schema_less_values()` — set an unregistered key, register a schema for a different key, save; verify the unregistered key does not appear in the saved JSON.

3. **Update existing tests** that previously relied on saving unregistered keys:
   - `test_save_to_file_unregistered_keys_general_category()` must be replaced with the new test above. The old behavior (saving unregistered keys under "General") is intentionally removed.

4. **Verify:** After a save/load cycle, any schema-less values that were set before schema registration are not persisted and do not reappear after load. Only registered schema keys survive the round-trip.

### Exit Criterion
- `save_to_file()` serializes only keys present in `m_schemaRegistry`.
- Schema-less active values are never written to disk.
- After save/load, unknown entries from previous saves are gone.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug -R "save_" --output-on-failure
```

---

## Phase 9: Test Suite Overhaul — Regression Coverage for All Locked Requirements

**Analysis Reference:** Section 2 (Component & File Impact Map) — `setting_manager_test.hpp` / `.cpp`; Verification Checklist items 1–17.

### Objective
Replace outdated tests with focused test declarations covering all strengthened requirements. Add reusable fixtures/helpers. Ensure comprehensive coverage of key validation, conversion, enum behavior, schema replacement, atomic load, path restrictions, traversal depth, and notification behavior.

### Steps

1. **Add reusable test helpers** in `test/backend/core/setting_manager_test.cpp`:
   - Create a helper function `registerCoreSchemas(dir2md::backend::SettingsManager &manager)` that calls `dir2md::backend::CoreSchema::registerSchemas(manager)`.
   - Create a helper function `writeJsonFile(const QString &filePath, const QJsonObject &obj)` that serializes and writes a JSON file.
   - Create a helper function `readJsonFile(const QString &filePath)` that reads and parses a JSON file, returning the root object.

2. **Update existing tests** to use registered schemas:
   - `test_settings_manager_get_set()` — register core schemas, set `core/tool_path` and `core/max_threads`, verify get returns correct values.
   - `test_settings_manager_active_values()` — register schemas, set values via registered keys, verify activeValues size and content.
   - `test_settings_manager_signal()` — register a schema, set a value, verify signal emission.
   - `test_save_to_file_creates_json()` — register core schemas, set values, save, verify JSON structure with category grouping.
   - `test_save_to_file_nested_keys()` — register schemas with nested keys, save, verify nested JSON structure.
   - `test_load_from_file_valid_replaces_values()` — register schemas, save, change in memory, load, verify restoration.
   - `test_roundtrip_preserves_types()` — register schemas for int/double/string/bool, save, load in new manager, verify type preservation.
   - `test_load_from_file_invalid_value_skipped()` — register schema with constraints, write invalid value to file, load, verify default is used.
   - `test_load_from_file_unknown_key_silently_ignored()` — register one schema, write JSON with known and unknown keys, load, verify only known key is loaded.
   - `test_settings_saved_signal_emitted()` — register schemas, set values, save, verify signal.

3. **Add new test declarations** (listed in previous phases):
   - Key syntax tests: `test_set_rejects_empty_key()`, `test_set_rejects_whitespace_key()`, `test_set_rejects_leading_separator()`, `test_set_retracts_trailing_separator()`, `test_set_rejects_repeated_separator()`, `test_set_rejects_unknown_key()`, `test_set_rejects_category_mismatch()`.
   - Conversion tests: `test_set_rejects_invalid_conversion()`, `test_set_canonicalizes_to_schema_type()`.
   - Enum tests: `test_enum_symbolic_name_validates()`, `test_enum_numeric_fallback_loads()` (if enum schemas are added to core_schema).
   - Schema replacement tests: `test_schema_replacement_retains_compatible()`, `test_schema_replacement_removes_incompatible()`, `test_schema_replacement_emits_signal_on_change()`.
   - Path restriction tests: `test_save_path_rejects_empty_name()`, `test_save_path_rejects_absolute_path()`, `test_save_path_rejects_traversal()`, `test_save_path_rejects_separator_in_name()`, `test_save_path_accepts_debug_test_override()`.
   - Traversal tests: `test_insert_nested_scalar_replacement()`, `test_flatten_deeply_nested_json()`.
   - Load tests: `test_load_category_mismatch_ignored()`, `test_load_mixed_valid_invalid_entries()`, `test_load_malformed_json_fails_atomically()`, `test_load_non_object_top_level_fails()`, `test_load_successful_replaces_atomically()`.
   - Save tests: `test_save_rejects_schema_less_values()`.

4. **Add test for directory I/O failure** in `test/backend/core/setting_manager_test.hpp`:
   - `test_save_directory_creation_failure_reports_to_stdout()` — set a path where the parent directory cannot be created (e.g., read-only location), verify `save_to_file` returns `false` and a warning is printed to stdout.

5. **Verify:** All new tests compile and pass under the debug preset. No test asserts on schema-less `set()` behavior (replaced with rejection semantics). Coverage includes all verification checklist items 1–17.

### Exit Criterion
- All existing tests updated to use registered schemas.
- New tests cover: key syntax validation, conversion failure, enum behavior, schema replacement, atomic load, path restrictions, traversal depth, notification behavior, and I/O failure reporting.
- No test asserts on the old schema-less `set()` or save behavior.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
```

---

## Phase 10: Build, Test & Coverage Verification

**Analysis Reference:** Section 4 (Verification Checklist) — all 18 items.

### Objective
Perform a complete build, test, and coverage verification to confirm no regressions in frontend, CLI, or existing backend behavior.

### Steps

1. **Configure and build the debug preset:**
   ```bash
   cmake --preset debug
   cmake --build --preset debug
   ```

2. **Run the full CTest suite:**
   ```bash
   ctest --preset debug --output-on-failure
   ```
   - Verify all tests pass, including new and updated tests from Phase 9.
   - Verify no frontend or CLI build errors are introduced.

3. **Build the release preset to confirm no test-path leak:**
   ```bash
   cmake --preset release
   cmake --build --preset release
   ```
   - Confirm `DIR2MD_DEBUG_TEST_PATH` is not defined in release compile commands.

4. **Generate coverage report (if BUILD_COVERAGE is desired):**
   ```bash
   cmake --preset debug-coverage
   cmake --build --preset debug-coverage
   ctest --preset debug --output-on-failure
   # Generate coverage with gcovr or llvm-cov as per project tooling
   ```

5. **Final verification against the checklist:**
   - [ ] All schema keys satisfy non-empty, whitespace-free, slash-separated key rules and category alignment.
   - [ ] Unknown keys are rejected by assignment and never enter active values or saved JSON.
   - [ ] Invalid conversion results are rejected even when `canConvert` reports a conversion path.
   - [ ] Numeric ranges are checked after conversion; enum values are checked against declared allowed values.
   - [ ] Qt meta-enum symbolic names round-trip (if enum schemas exist); numeric fallback loads correctly.
   - [ ] Compatible schema replacement retains values; incompatible replacement removes them so reads return the new default.
   - [ ] Schema replacement and load removals preserve the existing effective-value notification behavior.
   - [ ] Nested insertion and flattening use iterative traversal; deep paths and intermediate scalar replacement work.
   - [ ] Persisted flattened keys retain category context; category mismatches are ignored during load.
   - [ ] Mixed valid/invalid/unknown files load valid settings independently; invalid entries disappear after save.
   - [ ] Malformed, unreadable, and non-object-top-level documents return failure and leave active state untouched.
   - [ ] Successful loads stage all accepted values before replacing the active map.
   - [ ] Production save/load rejects empty names, absolute paths, traversal, invalid separators, and outside-root paths.
   - [ ] Debug preset exposes `DIR2MD_DEBUG_TEST_PATH`; release does not.
   - [ ] Tests can use isolated temporary paths only when the debug test capability is compiled in.
   - [ ] Directory, read, write, and commit failures print diagnostics to stdout without exposing setting values.
   - [ ] Complete debug configure, build, and CTest suite pass; no frontend, CLI, or thread-safety contract is unintentionally changed.

### Exit Criterion
- All 18 verification checklist items confirmed.
- Full test suite passes under debug preset.
- Release build succeeds without test-path definition.
- No regressions in frontend or CLI targets.

### Validation Command
```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
cmake --preset release && cmake --build --preset release
```

---

## Execution Order & Dependencies

| Phase | Files Modified | Depends On |
|-------|---------------|------------|
| 1 | `CMakePresets.json`, `test/backend/core/CMakeLists.txt` | — |
| 2 | `src/backend/core/settings_manager.cpp`, `settings_manager.hpp` | 1 (compile definition available) |
| 3 | `src/backend/core/settings_manager.cpp`, `settings_manager.hpp`, `test/backend/core/setting_manager_test.{hpp,cpp}` | 2 |
| 4 | `src/backend/core/settings_manager.cpp`, `settings_manager.hpp`, `test/backend/core/setting_manager_test.{hpp,cpp}` | 2 |
| 5 | `src/backend/core/settings_manager.cpp`, `settings_manager.hpp`, `test/backend/core/setting_manager_test.{hpp,cpp}` | 1, 3 |
| 6 | `src/backend/core/settings_manager.cpp` | — (independent refactor) |
| 7 | `src/backend/core/settings_manager.cpp`, `test/backend/core/setting_manager_test.{hpp,cpp}` | 3, 5, 6 |
| 8 | `src/backend/core/settings_manager.cpp`, `test/backend/core/setting_manager_test.{hpp,cpp}` | 3 |
| 9 | `test/backend/core/setting_manager_test.{hpp,cpp}` | 2–8 (all logic changes) |
| 10 | All files (verification only) | 1–9 |

**Recommended execution order:** Phases 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10. Phase 6 (traversal refactor) is independent and can be executed in parallel with phases 3–5 if desired, but running it sequentially after phase 2 ensures the build remains stable.
