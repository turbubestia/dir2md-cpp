# Implementation Analysis: 5-case-insensitive-setting-manager

## 1. Architectural Impact & Data Flow

### High-Level Overview
This feature introduces a **bidirectional category format conversion layer** between the internal `SettingSchema::category` field (display format, e.g., `"File Editor"`) and the JSON file keys (normalized format, e.g., `"file-editor"`). The conversion is mediated by two helper functions operating on the first path segment of schema keys.

### Affected Subsystems
- **Backend Schema Registry** — `registerSchema()` gains auto-fill + consistency enforcement logic
- **Backend Persistence Layer** — `save_to_file()` and `load_from_file()` gain normalization-aware serialization/deserialization
- **Test Suite** — existing tests using lowercase categories must be updated to use display-formatted categories

### Data Flow Changes

```
registerSchema():
  Input: SettingSchema { key, category }
    ├─ If category is empty → derive from key prefix → convert to DISPLAY format → store
    └─ If category is non-empty → normalize it → compare with normalized key prefix
       ├─ Match → accept schema (store display-format category)
       └─ Mismatch → reject (no insertion, no signal)

save_to_file():
  For each value in m_values:
    ├─ Look up schema → get display-format category
    ├─ Convert display format → normalized format (e.g., "General" → "general")
    └─ Use normalized format as JSON top-level key

load_from_file():
  For each flat key from JSON:
    ├─ Extract JSON top-level key (any case/format)
    ├─ Normalize it → compare with schema's normalized prefix
    ├─ Match → accept value; Mismatch → skip with warning
    └─ Validate value against schema → stage in candidate map

Roundtrip:
  In-memory (display category) → save → JSON (normalized key) → load → In-memory (display category)
```

## 2. Component & File Impact Map

### [`src/backend/core/settings_manager.cpp`]
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add two new internal helper functions in the anonymous namespace:
    - `toDisplayFormat(const QString &normalized)` — converts `"file-editor"` → `"File Editor"` (dash→space, title-case, collapse multi-dash)
    - `toNormalizedFormat(const QString &display)` — converts `"File Editor"` → `"file-editor"` (space→dash, lowercase, collapse multi-space)
  - [ ] Modify `registerSchema()`:
    - [ ] Replace raw prefix assignment (`schema.category = prefix`) with display-format conversion (`schema.category = toDisplayFormat(prefix)`)
    - [ ] Add consistency enforcement: when `category` is non-empty, normalize both the provided category and the key prefix, compare them; reject if mismatched
  - [ ] Modify `save_to_file()`:
    - [ ] Replace direct use of `schema.category.trimmed()` as JSON key with normalized conversion: `toNormalizedFormat(schema.category.trimmed())`
  - [ ] Modify `load_from_file()`:
    - [ ] Replace case-sensitive category comparison (`category != sch->category`) with normalized comparison: normalize both sides and compare
    - [ ] The schema lookup via `m_schemaRegistry.contains(schemaKey)` already uses the full flat key — no change needed there since the flat key is constructed from the JSON file's actual structure

- **Logic Modifications Required:**
  - Normalization must handle: multiple consecutive dashes/spaces collapsing to single separator, title-casing each segment, lowercase conversion
  - Non-alphanumeric characters in categories (e.g., `"C++ Settings"`) should be preserved as-is during conversion

### [`src/backend/core/settings_manager.hpp`]
- **Type of Change:** No change required
- The public API surface remains unchanged — all modifications are internal to the `.cpp` file via anonymous namespace helpers

### [`src/backend/core/core_schema.cpp`]
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Update category strings from lowercase (`"general"`, `"performance"`) to display format (`"General"`, `"Performance"`)
  - These values will be stored as-is in `SettingSchema::category` since they already match the key prefix after normalization

### [`test/backend/core/setting_manager_test.cpp`]
- **Type of Change:** Modify + Create
- **Structural Changes (Modify existing tests):**
  - [ ] `test_load_category_mismatch_ignored` — rename to `test_load_category_mismatch_normalized` and update: the test should verify that case-variations of the same category (`"General"` vs `"general"` vs `"GENERAL"`) are now accepted, while truly wrong categories (`"WrongCategory"`) are still rejected
  - [ ] `test_save_to_file_creates_json` — verify JSON keys use normalized format (lowercase), which is already the case since CoreSchema uses lowercase categories; after updating to display format, this test must explicitly check for normalized output keys
  - [ ] All tests using `registerCoreSchemas()` will automatically get display-formatted categories from `core_schema.cpp` — no per-test changes needed for those

- **New Tests (Create):**
  - [ ] `test_normalize_toDisplayFormat` — verify `"file-editor"` → `"File Editor"`, `"my-category"` → `"My Category"`, `"some-deeply-nested-category"` → `"Some Deeply Nested Category"`, `"general"` → `"General"`
  - [ ] `test_normalize_toNormalizedFormat` — verify `"File Editor"` → `"file-editor"`, `"My Category"` → `"my-category"`, `"General"` → `"general"`
  - [ ] `test_normalize_multiSpace_collapse` — verify `"File  Editor"` (double space) → normalized → `"file-editor"`, `"file--editor"` (double dash) → display → `"File Editor"`
  - [ ] `test_registerSchema_autoFillEmptyCategory` — verify empty/whitespace category is auto-filled with display-format conversion from key prefix
  - [ ] `test_registerSchema_consistencyEnforcement_accept` — verify matching prefix+category is accepted
  - [ ] `test_registerSchema_consistencyEnforcement_reject` — verify mismatched prefix+category (e.g., `"file-editor/tab-size"` + `"Display Preferences"`) is rejected
  - [ ] `test_load_caseInsensitiveCategoryMatching` — verify JSON keys in various cases (`"File Editor"`, `"FILE-EDITOR"`, `"file-editor"`) all load successfully against a schema with display category `"File Editor"`
  - [ ] `test_load_underscoreNotSupported` — verify underscore-separated keys (`"file_editor"`) are rejected as unknown/mismatched
  - [ ] `test_save_normalizedFormatOutput` — verify saved JSON uses normalized (lowercase-dash) category keys, NOT display format
  - [ ] `test_roundtrip_preservesCategories` — verify save→load→save produces semantically equivalent output with correct category key normalization

### [`test/backend/core/setting_manager_test.hpp`]
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add declarations for all new test slots listed above

## 3. Boundary & Edge Case Analysis

### Error Handling
- **Schema registration rejection** (RM-04): When prefix-category mismatch is detected, `registerSchema()` returns without inserting into `m_schemaRegistry` and without emitting any signal. No side effects.
- **Load-time category mismatch** (RM-02): Values with unrecognized category keys are skipped with a `qWarning()` message. The atomic commit pattern ensures no partial state change — only validated values are staged in `candidateValues`.
- **Normalization edge cases**: Non-alphanumeric characters (e.g., `"C++ Settings"`) are preserved as-is; only spaces→dashes and lowercasing are applied during normalization.

### Security & Permissions
- No new security concerns introduced. The existing `resolvePersistencePath()` path traversal protection remains unchanged.
- Schema registration rejection is a local in-memory operation — no file I/O or network calls affected.

### Performance / Scale Impact
- **O(1) schema lookup preserved**: The flat key lookup (`m_schemaRegistry.contains(schemaKey)`) uses the full key path (e.g., `"file-editor/tab-size"`), which is unchanged. Normalization only affects the category comparison logic, not the lookup mechanism.
- **Normalization overhead**: Each `registerSchema()`, `save_to_file()`, and `load_from_file()` call performs a constant number of string conversions per schema/value. For typical settings files (dozens to hundreds of keys), this is negligible.
- **No iteration over schemas during load**: The normalized flat key is constructed from the JSON file's category key + sub-path, then looked up directly in `m_schemaRegistry`. No linear scan required.

### Edge Cases
| Case | Behavior |
|------|----------|
| Empty category on registration | Auto-filled via `toDisplayFormat(keyPrefix)` |
| Whitespace-only category | Trimmed → treated as empty → auto-filled |
| Multi-consecutive dashes (`"file--editor"`) | Collapsed to single space in display: `"File Editor"` |
| Multi-consecutive spaces (`"File  Editor"`) | Collapsed to single dash in normalized: `"file-editor"` |
| Underscore in JSON key (`"file_editor"`) | NOT converted — treated as unknown category, rejected on load |
| Non-alphanumeric chars (`"C++ Settings"`) | Preserved as-is; normalization only affects spaces and lowercasing |
| Roundtrip (save→load→save) | Semantically equivalent; display categories preserved in memory, normalized keys used in JSON |

## 4. Verification Checklist

- [ ] Verify `toDisplayFormat("file-editor")` returns `"File Editor"` (dash→space, title-case)
- [ ] Verify `toNormalizedFormat("File Editor")` returns `"file-editor"` (space→dash, lowercase)
- [ ] Verify multi-dash (`"a--b"`) normalizes to display `"A B"` (single space)
- [ ] Verify multi-space (`"A  B"`) normalizes to `"a-b"` (single dash)
- [ ] Verify `registerSchema()` with empty category auto-fills display format from key prefix
- [ ] Verify `registerSchema()` with mismatched prefix+category is rejected (no insertion, no signal)
- [ ] Verify `registerSchema()` with matching prefix+category is accepted
- [ ] Verify `save_to_file()` outputs normalized (lowercase-dash) category keys in JSON
- [ ] Verify `load_from_file()` accepts JSON keys in any case (`"General"`, `"GENERAL"`, `"general"`) against display-formatted schema category
- [ ] Verify `load_from_file()` rejects underscore-separated keys (`"file_editor"`) as unknown
- [ ] Verify backward compatibility: old settings files with lowercase keys load correctly
- [ ] Verify roundtrip (save→load→save) preserves all values and types
- [ ] Verify existing tests pass after updating CoreSchema categories to display format
- [ ] Verify `test_load_category_mismatch_ignored` is updated to test normalized comparison instead of strict case-sensitive matching
- [ ] Verify no changes to `settings_manager.hpp` public API surface
