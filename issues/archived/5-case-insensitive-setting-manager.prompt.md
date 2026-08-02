# Implementation Plan: Case-Insensitive Setting Manager Categories

[Analysis Reference](./5-case-insensitive-setting-manager.plan.analysis.md)

---

## Traceability Matrix

| Analysis Section | Prompt Phase | Description |
|---|---|---|
| Section 2.1 — `settings_manager.cpp` helpers | Phase 1 | Bidirectional normalization functions |
| Section 2.1 — `registerSchema()` modification | Phase 2 | Auto-fill + consistency enforcement |
| Section 2.1 — `save_to_file()` modification | Phase 3 | Normalized JSON key output |
| Section 2.1 — `load_from_file()` modification | Phase 4 | Case-insensitive category matching |
| Section 2.2 — `core_schema.cpp` update | Phase 5 | Display-format category strings |
| Section 2.3 — Test modifications + new tests | Phase 6 | Updated and new test suite |
| Section 3 — Verification checklist | Phase 7 | End-to-end validation |

---

## Phase 1: Implement Bidirectional Normalization Helpers

**Analysis Reference:** Section 2.1, Boundary & Edge Case Analysis table

### Objective
Add two internal helper functions in the anonymous namespace of `src/backend/core/settings_manager.cpp` that convert between display-format categories (e.g., `"File Editor"`) and normalized format keys (e.g., `"file-editor"`).

### Steps

1. **Add `toDisplayFormat()` function** to the anonymous namespace:
   - Input: a normalized category string like `"file-editor"` or `"my-category"`
   - Output: a display-format string like `"File Editor"` or `"My Category"`
   - Logic: split on `-`, collapse consecutive dashes into a single separator, title-case each segment (capitalize first letter, lowercase the rest of each segment), join with space
   - Non-alphanumeric characters (e.g., `+` in `"C++ Settings"`) are preserved as-is within their segments

2. **Add `toNormalizedFormat()` function** to the anonymous namespace:
   - Input: a display-format category string like `"File Editor"` or `"My Category"`
   - Output: a normalized key like `"file-editor"` or `"my-category"`
   - Logic: split on whitespace (including consecutive spaces), collapse consecutive spaces into a single separator, lowercase each segment, join with `-`
   - Non-alphanumeric characters are preserved within segments

3. **Placement**: Insert both functions in the existing anonymous namespace block, after `flattenJsonObject()` and before the closing `} // anonymous namespace`.

### Exit Criterion
- Both functions compile without warnings under the project's CMake build configuration
- The anonymous namespace contains exactly two new inline functions with correct signatures returning `QString`

### Validation Command
```bash
cmake --build .\build\cmake-debug --target backend_core
```

---

## Phase 2: Modify `registerSchema()` for Auto-Fill and Consistency Enforcement

**Analysis Reference:** Section 2.1, `registerSchema()` structural changes; Boundary & Edge Case Analysis — empty category, whitespace-only category rows

### Objective
Update the `registerSchema()` method to auto-fill empty categories using display-format conversion and enforce consistency between the provided category and the key prefix.

### Steps

1. **Replace raw prefix assignment with display-format conversion**:
   - Where the current code does `schema.category = prefix` (when category is empty/whitespace), replace with `schema.category = toDisplayFormat(prefix)` so the stored category uses display format (e.g., `"file-editor"` → `"File Editor"`)

2. **Add consistency enforcement when category is non-empty**:
   - After trimming whitespace, if the provided category is non-empty:
     - Normalize the provided category using `toNormalizedFormat()`
     - Extract the key prefix from `schema.key` (first segment before `/`)
     - Normalize the key prefix using `toDisplayFormat()` then normalize again for comparison — or equivalently, normalize both sides and compare
     - If normalized forms do not match: return early without inserting into `m_schemaRegistry` and without emitting any signal
     - If they match: proceed with storing the display-format category as-is

3. **Preserve existing schema replacement logic**:
   - The existing block that handles re-registration of an already-existing key (active value validation, signal emission) must remain unchanged in its control flow — only the category normalization/consistency check is added before it

### Exit Criterion
- `registerSchema()` with empty category auto-fills display format from key prefix
- `registerSchema()` with matching prefix+category accepts and stores correctly
- `registerSchema()` with mismatched prefix+category returns without side effects (no insertion, no signal)

### Validation Command
```bash
cmake --build .\build\cmake-debug --target backend_core
```

---

## Phase 3: Modify `save_to_file()` for Normalized JSON Key Output

**Analysis Reference:** Section 2.1, `save_to_file()` structural changes; Boundary & Edge Case Analysis — roundtrip row

### Objective
Update the `save_to_file()` method to use normalized (lowercase-dash) format as JSON top-level keys instead of raw display-format categories.

### Steps

1. **Replace direct category-as-key usage with normalized conversion**:
   - Where the current code uses `schema.category.trimmed()` directly as the JSON object key, replace with `toNormalizedFormat(schema.category.trimmed())`
   - This ensures that a schema with category `"File Editor"` produces a JSON key of `"file-editor"`, not `"File Editor"`

2. **Preserve all existing logic**:
   - Path resolution, atomic write via QSaveFile, signal emission, nested value insertion — all remain unchanged
   - Only the category-to-key mapping is modified

### Exit Criterion
- Saved JSON files use normalized (lowercase-dash) format for top-level category keys
- Display-format categories are preserved in memory; only the serialized output is normalized

### Validation Command
```bash
cmake --build .\build\cmake-debug --target backend_core
```

---

## Phase 4: Modify `load_from_file()` for Case-Insensitive Category Matching

**Analysis Reference:** Section 2.1, `load_from_file()` structural changes; Boundary & Edge Case Analysis — backward compatibility row

### Objective
Update the `load_from_file()` method to accept JSON keys in any case format when matching against schema categories, using normalized comparison instead of strict case-sensitive string equality.

### Steps

1. **Replace case-sensitive category comparison with normalized comparison**:
   - Where the current code does `if (category != sch->category)`, replace with a normalized comparison:
     - Normalize the JSON file's category key using `toNormalizedFormat()`
     - Normalize the schema's stored category using `toNormalizedFormat()`
     - Compare the two normalized forms for equality
   - This allows `"General"`, `"GENERAL"`, `"general"`, `"GeNeRaL"` all to match a schema with display category `"General"`

2. **Preserve existing validation flow**:
   - Key syntax validation, schema lookup, value validation, atomic commit pattern — all remain unchanged
   - The `qWarning()` message for mismatched categories should still be emitted when normalized forms do not match

3. **Handle underscore-separated keys**:
   - Since underscores are NOT converted during normalization (only spaces and dashes), a JSON key like `"file_editor"` will NOT normalize to `"file-editor"`, so it will correctly fail the normalized comparison and be rejected with a warning

### Exit Criterion
- JSON files with category keys in any case format load successfully against registered schemas
- Underscore-separated keys are rejected as unknown/mismatched
- Old settings files with lowercase keys (backward compatibility) load correctly

### Validation Command
```bash
cmake --build .\build\cmake-debug --target backend_core
```

---

## Phase 5: Update `core_schema.cpp` to Display-Format Categories

**Analysis Reference:** Section 2.2, core_schema.cpp structural changes

### Objective
Update the category strings in `src/backend/core/core_schema.cpp` from lowercase format to display-format (title-case) so they match the expected display categories after normalization.

### Steps

1. **Change `"general"` to `"General"`** in the `ToolPath` schema registration
2. **Change `"performance"` to `"Performance"`** in the `MaxThreads` schema registration

### Exit Criterion
- Both CoreSchema registrations use title-case category strings (`"General"`, `"Performance"`)
- These values are stored as-is in `SettingSchema::category` since they already match the display format derived from their key prefixes (`"general"` → `"General"`, `"performance"` → `"Performance"`)

### Validation Command
```bash
cmake --build .\build\cmake-debug --target backend_core
```

---

## Phase 6: Update and Create Test Suite

**Analysis Reference:** Section 2.3, test/backend/core/setting_manager_test.cpp; Boundary & Edge Case Analysis — all edge case rows

### Objective
Update existing tests that use lowercase categories to use display-format categories, and add new tests covering normalization functions, consistency enforcement, case-insensitive loading, and roundtrip behavior.

### Part A: Update Existing Tests in `setting_manager_test.cpp`

1. **Rename and update `test_load_category_mismatch_ignored`**:
   - Rename the test slot to `test_load_category_mismatch_normalized`
   - Change the test semantics: instead of verifying that a wrong category is rejected, verify that case-variations of the same category (`"General"` vs `"general"` vs `"GENERAL"`) are now accepted, while truly wrong categories (`"WrongCategory"`) are still rejected

2. **Update `test_save_to_file_creates_json`**:
   - After updating CoreSchema to display-format categories, verify that JSON keys use normalized format (lowercase-dash). The test currently checks for `"general"` and `"performance"` as top-level keys — these should remain the same since normalization of `"General"` produces `"general"` and normalization of `"Performance"` produces `"performance"`. No change needed if CoreSchema is updated correctly.

3. **Update `test_settings_manager_schema`**:
   - The test manually creates a schema with `category = "test"` — update to use display format `"Test"` for consistency, or keep as-is since the normalization of `"test"` produces `"Test"` and the key prefix is also `"test"`, so consistency check passes.

4. **Update any other tests that directly reference category strings**:
   - Review all test methods that construct schemas with explicit categories and ensure they use display-format values

### Part B: Add New Test Declarations in `setting_manager_test.hpp`

Add the following new test slot declarations to the `setting_manager_test` class:

- `test_normalize_toDisplayFormat()` — verify `"file-editor"` → `"File Editor"`, `"my-category"` → `"My Category"`, `"some-deeply-nested-category"` → `"Some Deeply Nested Category"`, `"general"` → `"General"`
- `test_normalize_toNormalizedFormat()` — verify `"File Editor"` → `"file-editor"`, `"My Category"` → `"my-category"`, `"General"` → `"general"`
- `test_normalize_multiSpace_collapse()` — verify `"File  Editor"` (double space) normalizes to `"file-editor"`, `"file--editor"` (double dash) displays as `"File Editor"`
- `test_registerSchema_autoFillEmptyCategory()` — verify empty/whitespace category is auto-filled with display-format conversion from key prefix
- `test_registerSchema_consistencyEnforcement_accept()` — verify matching prefix+category is accepted
- `test_registerSchema_consistencyEnforcement_reject()` — verify mismatched prefix+category (e.g., `"file-editor/tab-size"` + `"Display Preferences"`) is rejected
- `test_load_caseInsensitiveCategoryMatching()` — verify JSON keys in various cases (`"File Editor"`, `"FILE-EDITOR"`, `"file-editor"`) all load successfully against a schema with display category `"File Editor"`
- `test_load_underscoreNotSupported()` — verify underscore-separated keys (`"file_editor"`) are rejected as unknown/mismatched
- `test_save_normalizedFormatOutput()` — verify saved JSON uses normalized (lowercase-dash) category keys, NOT display format
- `test_roundtrip_preservesCategories()` — verify save→load→save produces semantically equivalent output with correct category key normalization

### Part C: Implement New Tests in `setting_manager_test.cpp`

Implement each new test method following the existing test patterns in the file. Use `QTemporaryDir` for file-based tests, `QSignalSpy` for signal verification, and direct `SettingsManager` API calls for unit-level tests.

For normalization function tests (`test_normalize_toDisplayFormat`, `test_normalize_toNormalizedFormat`, `test_normalize_multiSpace_collapse`):
- Since these functions are in the anonymous namespace of `settings_manager.cpp`, they cannot be called directly from tests. Instead, test them indirectly through the public API:
  - Register schemas with known categories and verify the stored category matches expected display format
  - Save settings and inspect JSON output to verify normalized keys
  - Load settings with various category key formats and verify values are accepted/rejected as expected

### Exit Criterion
- All existing tests pass after category string updates
- All new tests compile and execute successfully
- Test coverage for `settings_manager.cpp` is maintained or improved

### Validation Command
```bash
ctest --test-dir .\build\cmake-debug\test -R setting_manager_test --output-on-failure
```

---

## Phase 7: End-to-End Validation and Coverage

**Analysis Reference:** Section 4, Verification Checklist (all items)

### Objective
Run the full test suite with coverage to verify correctness, backward compatibility, and code quality.

### Steps

1. **Rebuild all targets** to ensure no stale artifacts:
   ```bash
   cmake --preset debug
   cmake --build .\build\cmake-debug
   ```

2. **Run the full test suite**:
   ```bash
   ctest --test-dir .\build\cmake-debug\test --output-on-failure
   ```

3. **Run settings manager tests specifically with coverage** (if debug-coverage preset exists):
   ```bash
   cmake --build --preset debug-coverage --target backend_core_test
   # Run the test executable to produce .profraw files
   # Merge and generate coverage report
   llvm-profdata merge -o default.profdata *.profraw
   llvm-cov show build/cmake-debug-coverage/test/backend/core/backend_core_test.exe -instr-profile=default.profdata
   ```

4. **Verify backward compatibility** by creating a manual test:
   - Write a JSON file with lowercase category keys (old format): `{"general": {"core": {"tool_path": "/usr/bin/tool"}}}`
   - Load it into a SettingsManager with updated CoreSchema
   - Verify the value loads correctly

5. **Verify no public API changes**:
   - Confirm that `settings_manager.hpp` has not been modified (all changes are internal to `.cpp`)
   - Confirm that `core_schema.hpp` has not been modified (only `.cpp` categories changed)

### Exit Criterion
- All tests pass (existing + new)
- No compilation warnings or errors
- Backward compatibility confirmed: old settings files load correctly
- Public API surface unchanged (`settings_manager.hpp`, `core_schema.hpp`)
- Coverage report shows no regressions in tested code paths

### Validation Command
```bash
cmake --build .\build\cmake-debug --target backend_core_test; ctest --test-dir .\build\cmake-debug\test -R setting_manager_test --output-on-failure
```

---

## Implementation Order Summary

| Phase | Files Modified | Complexity | Dependencies |
|---|---|---|---|
| 1 | `src/backend/core/settings_manager.cpp` | Low | None |
| 2 | `src/backend/core/settings_manager.cpp` | Medium | Phase 1 |
| 3 | `src/backend/core/settings_manager.cpp` | Low | Phase 1 |
| 4 | `src/backend/core/settings_manager.cpp` | Medium | Phase 1 |
| 5 | `src/backend/core/core_schema.cpp` | Low | None (can run in parallel with Phases 1-4) |
| 6 | `test/backend/core/setting_manager_test.{cpp,hpp}` | High | Phases 1-5 |
| 7 | All files | Verification | Phase 6 |

Phases 1-4 modify the same file (`settings_manager.cpp`) and should be applied sequentially. Phase 5 can be done in parallel with any of them since it modifies a different file. Phase 6 depends on all implementation changes being complete. Phase 7 is the final validation gate.
