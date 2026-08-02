# Implementation Analysis: 4-improve-setting-manager

## 1. Architectural Impact & Data Flow

The change is localized to the QtCore settings subsystem and its backend unit-test/build wiring. `SettingsManager` remains the owner of the flat active-value store and schema registry; the JSON persistence format remains category-grouped nested JSON, but its validation, traversal, path boundary, and state-commit rules become explicit and consistent.

- **Affected Subsystems:**
  - Backend core settings model: `SettingSchema` validation, `SettingsManager` key/value semantics, schema replacement, and effective-value notifications.
  - Backend persistence: JSON flattening/insertion, category-preserving schema lookup, bounded configuration-file access, tolerant entry handling, and atomic state replacement.
  - CMake configuration: an explicit debug-only compile definition for test path overrides.
  - Backend core tests: regression coverage for all locked settings-manager requirements.
  - `CoreSchema`: existing schema keys/categories must remain compatible with the strengthened key and category contract; no separate production call-site changes are currently indicated.
- **Data Flow Changes:**
  - Schema registration validates the schema identity/key contract, replaces an existing schema deterministically, and revalidates any active value for the same key. A valid override remains active; an incompatible override is removed so `get()` resolves to the replacement default.
  - `set(key, value)` first validates key syntax, category/key consistency, schema existence, conversion success, range constraints, and enum membership. Unknown or invalid keys leave the existing active value unchanged and return the existing boolean failure contract.
  - Save accepts only schema-backed active values and resolves persistence to the application configuration directory under `{QDir::homePath()}/.config/dir2md` in production. A debug-configured test build may use an explicitly supplied isolated file/path.
  - Save transforms validated flat schema keys into category-rooted nested JSON using non-recursive traversal. Invalid key paths are rejected before they can create malformed JSON; intermediate scalar nodes are replaced according to the locked insertion behavior.
  - Load first validates file access, JSON syntax, and the required object-root structure. It flattens the document with stack-based traversal, retains the category as part of the lookup path, ignores unknown/invalid individual entries, and stages accepted typed values in a candidate map.
  - After all entries are examined, a successful file-level load replaces the active map in one state transition. Effective-value changes, including values that disappear and therefore fall back to defaults, follow the existing `settingChanged` signal contract without introducing a new event type.
  - Save/load failures caused by path, directory, read, write, parse, or top-level-structure errors report an explanatory message to stdout and leave the pre-existing active state unchanged where applicable.

## 2. Component & File Impact Map

### `src/backend/core/settings_manager.hpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Keep the public manager responsibilities and reference-returning accessors unchanged, as those are explicit no-action boundaries.
  - [ ] Clarify or adjust the persistence API surface so production callers select a filename within the application configuration directory, while explicit file/path access is available only under the debug test-path compile definition.
  - [ ] Preserve the boolean result and existing signals unless an internal/private mechanism is needed to support staged load and effective-value change reporting.
  - [ ] Represent the schema metadata needed for precise conversion and enum validation, including the relationship between symbolic enum options and their declared type.
- **Logic Modifications Required:**
  - [ ] Define the manager-level key invariant: non-empty slash-separated segments, no whitespace, no leading/trailing or repeated separators, and a first segment consistent with the schema's category/key convention.
  - [ ] Make unknown keys invalid for assignment rather than allowing schema-less active values.
  - [ ] Ensure schema-backed assignments store a successfully converted value in the schema type, not merely a value with a possible conversion path.
  - [ ] Preserve the default fallback behavior after an active override is removed.

### `src/backend/core/settings_manager.cpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Replace recursive `insertNestedValue` and `flattenJsonObject` helper contracts with stack-based traversal responsibilities in the implementation layer.
  - [ ] Add a single internal path-resolution/validation boundary for production configuration paths and the explicitly compile-configured test override.
  - [ ] Add internal candidate-state and effective-value comparison responsibilities for atomic loading and notifications.
- **Logic Modifications Required:**
  - [ ] Validate key syntax before registry lookup or mutation, including empty segments, whitespace, category mismatch, and unknown schema keys.
  - [ ] Update `SettingSchema::isValid` to validate the result of conversion, enforce numeric bounds against the converted value, and validate enum values against the declared allowed set rather than a broad type category.
  - [ ] Support Qt meta-enum symbolic names for enum input/output where metadata is available, with numeric persistence fallback and a debug-only stdout warning when no symbolic key can be obtained.
  - [ ] Revalidate active values when `registerSchema` replaces an existing schema; remove incompatible overrides and preserve valid ones. Emit the existing effective-value notification when replacement changes what `get()` returns.
  - [ ] Reject or skip schema-less values consistently so save cannot persist entries that load cannot resolve.
  - [ ] Preserve the category in flattened persisted keys and validate the category/key relationship instead of unconditionally stripping the first segment before schema lookup.
  - [ ] Ignore unknown and individually invalid entries with warnings, while continuing to process valid entries in the same document.
  - [ ] Treat malformed JSON, unreadable files, and any non-object top-level document as file-level failures; do not alter active state for those failures.
  - [ ] Stage accepted values in a new map and replace `m_values` only after file-level validation completes. Account for removed effective values in the existing signal behavior without defining a new consumer contract.
  - [ ] Restrict production reads and writes to `{QDir::homePath()}/.config/dir2md`, rejecting empty names, absolute paths, traversal, separators, and normalization escapes. Use the debug-only build capability for isolated test locations.
  - [ ] Report persistence and directory-creation failures to stdout under the interim policy and avoid printing setting contents.
  - [ ] Keep atomic file replacement through the existing persistence mechanism and ensure a successful signal is emitted only after the write is committed.

### `src/backend/core/core_schema.cpp`
- **Type of Change:** Review / Modify only if required by validation alignment
- **Structural Changes:**
  - [ ] Confirm each registered key has valid non-empty path segments and a category compatible with the strengthened persistence contract.
  - [ ] Add explicit enum metadata only for schemas that require enum behavior; no current enum schema is present in this file.
- **Logic Modifications Required:**
  - [ ] Ensure the existing `core/tool_path` and `core/max_threads` registrations remain valid under strict key, type, default, and category validation.
  - [ ] Avoid changing schema defaults or categories unless required to preserve the defined on-disk contract.

### `test/backend/core/setting_manager_test.hpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Replace tests that assert schema-less `set()` and save behavior with declarations for the locked rejection semantics.
  - [ ] Add focused test declarations for key syntax/category validation, conversion failure, enum behavior, schema replacement, atomic load, path restrictions, traversal depth, and notification behavior.
  - [ ] Add declarations for debug-only explicit test-path setup where the target compile definition is available.

### `test/backend/core/setting_manager_test.cpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add reusable test fixtures/helpers for registering schemas, writing controlled JSON documents, selecting isolated debug test paths, and observing effective-value signals.
  - [ ] Keep tests single-threaded and do not change the reference-returning API contract.
- **Logic Modifications Required:**
  - [ ] Verify unknown keys, empty segments, leading/trailing separators, repeated separators, whitespace, and category mismatches are rejected without mutation or exceptions.
  - [ ] Verify conversion operations are checked for success, invalid converted values are rejected, valid values are canonicalized to the schema type, and existing values remain unchanged after rejection.
  - [ ] Verify enum symbolic names, unknown names, numeric fallback, and debug-only fallback warnings where Qt meta-enum metadata is available.
  - [ ] Verify replacement schemas retain compatible active values, remove incompatible ones, restore replacement defaults, and preserve effective-value notification behavior.
  - [ ] Verify save rejects schema-less active values and serializes only valid schema-backed values.
  - [ ] Verify stack-based insertion handles an intermediate scalar replacement and stack-based flattening handles deeply nested JSON without recursion-dependent behavior.
  - [ ] Verify flattened keys retain category context and load accepts only the matching category/key pair.
  - [ ] Verify mixed documents load every valid entry while ignoring unknown and invalid entries, and ignored entries do not reappear after a later save.
  - [ ] Verify malformed/unreadable files and valid JSON with a non-object top level fail atomically and preserve all existing active values.
  - [ ] Verify successful load replaces the active map atomically and notifies observers when an effective value disappears.
  - [ ] Verify production path rejection for empty, absolute, traversal, separator-containing, and outside-root names, plus debug-only explicit temporary path access.
  - [ ] Verify directory and file I/O failures report the interim stdout error policy without exposing setting values.
  - [ ] Retain coverage for successful JSON creation, nested keys, round-trip types, missing files, malformed JSON, and successful save notifications after updating them to the new path/schema contract.

### `CMakePresets.json`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add the dedicated cache/compile configuration symbol to the `debug` configure preset as required by the locked test-path policy.
  - [ ] Keep release and other production-oriented presets free of the test-path bypass definition.
- **Logic Modifications Required:**
  - [ ] Ensure the symbol is explicit build configuration, not inferred from a runtime path, environment value, or filename.
  - [ ] Ensure the debug test preset inherits the same definition used by the debug build.

### `CMakeLists.txt`
- **Type of Change:** Modify only if the new preset symbol needs project-level declaration
- **Structural Changes:**
  - [ ] Declare the test-path option/definition boundary if it is not provided entirely by existing target configuration.
  - [ ] Keep the production default disabled.
- **Logic Modifications Required:**
  - [ ] Propagate the definition only to the targets that need explicit test persistence paths, preferably the backend test target and any implementation compilation unit that must expose the test-only behavior.
  - [ ] Preserve the QtCore-only backend dependency boundary.

### `test/backend/core/CMakeLists.txt`
- **Type of Change:** Modify if target-level propagation is needed
- **Structural Changes:**
  - [ ] Make the backend core test target receive the debug-only test-path compile definition.
  - [ ] Keep production backend library compilation from gaining a runtime-selectable path bypass.
- **Logic Modifications Required:**
  - [ ] Ensure the test executable can use isolated temporary locations while production behavior remains constrained.
  - [ ] Continue registering the focused tests through the existing CTest/VS Code integration.

### `src/backend/core/CMakeLists.txt`
- **Type of Change:** Review / Modify only if the compile definition must reach implementation code
- **Structural Changes:**
  - [ ] Preserve the current source ownership and QtCore linkage.
  - [ ] If the test-only API is compiled into the backend implementation, expose the definition through the narrowest target boundary that keeps release builds constrained.
- **Logic Modifications Required:**
  - [ ] Ensure no frontend or CLI dependency is introduced by persistence validation or enum metadata handling.

## 3. Boundary & Edge Case Analysis

- **Error Handling:**
  - Ordinary key/schema/value validation failures are non-throwing rejections for `set` and per-entry skips for `load`.
  - File open/read, JSON parse, invalid top-level structure, directory creation, path validation, write, and atomic commit failures are file-level failures returning `false`; loading preserves the prior active map.
  - Persistence failures print concise diagnostics to stdout without setting values or other sensitive content. Unknown and invalid persisted entries produce warnings while valid entries continue loading.
  - A successful load commits a complete candidate map, then applies the existing `settingChanged` notification semantics for changed, added, or disappeared effective values. No new removal event or thread-safety contract is introduced.
- **Security & Permissions:**
  - Production persistence is confined beneath `{QDir::homePath()}/.config/dir2md`; path normalization must prevent absolute paths, parent traversal, empty filenames, empty segments, separators, and escape from the allowed root.
  - The explicit path bypass exists only when enabled by the dedicated debug CMake definition. Runtime input cannot activate it.
  - JSON values are accepted only for registered schemas, after successful conversion and precise enum/range validation. Unknown persisted keys are ignored and are not retained for later saves.
  - This task does not add encryption, integrity signing, permission hardening, thread synchronization, or redesign of internal reference-returning accessors.
- **Performance / Scale Impact:**
  - JSON insertion and flattening become iterative stack traversals, removing call-stack growth for deeply nested settings while retaining linear traversal over JSON nodes and path segments.
  - Loading temporarily holds the parsed document, flattened entries, and candidate value map before commit; this is required for atomicity and remains bounded by file size.
  - Schema and active-value lookups remain hash-based. Signal diffing adds work proportional to the union of old and candidate active/effective keys during state replacement.
  - Configuration directory creation and path canonicalization add small per-save/load filesystem overhead but establish a bounded trust boundary.
- **Format and compatibility boundary:**
  - The existing category-rooted nested JSON shape remains the persistence format. The category is now meaningful during load and must agree with the registered schema rather than being discarded unconditionally.
  - Previously saved schema-less values are intentionally not preserved because unknown settings are ignored and subsequent saves serialize only accepted schema-backed values.
  - Valid existing files with mixed entries remain partially usable; syntactically valid documents with an invalid top-level structure are not partially applied.

## 4. Verification Checklist

- [ ] Confirm all schema keys satisfy non-empty, whitespace-free, slash-separated key rules and category alignment.
- [ ] Confirm unknown keys are rejected by assignment and never enter active values or saved JSON.
- [ ] Confirm invalid conversion results are rejected even when `canConvert` reports a conversion path.
- [ ] Confirm numeric ranges are checked after conversion and enum values are checked against their declared allowed values.
- [ ] Confirm Qt meta-enum symbolic names round-trip and numeric fallback loads correctly, with the required debug warning.
- [ ] Confirm compatible schema replacement retains values and incompatible replacement removes them so reads return the new default.
- [ ] Confirm schema replacement and load removals preserve the existing effective-value notification behavior.
- [ ] Confirm nested insertion and flattening use iterative traversal and handle deep paths plus intermediate scalar replacement.
- [ ] Confirm persisted flattened keys retain category context and category mismatches are ignored.
- [ ] Confirm mixed valid/invalid/unknown files load valid settings independently and invalid entries disappear after save.
- [ ] Confirm malformed, unreadable, and non-object-top-level documents return failure and leave active state untouched.
- [ ] Confirm successful loads stage all accepted values before replacing the active map.
- [ ] Confirm production save/load rejects empty names, absolute paths, traversal, invalid separators, and outside-root paths.
- [ ] Confirm the debug preset exposes the dedicated test-path compile definition and release does not.
- [ ] Confirm tests can use isolated temporary paths only when the debug test capability is compiled in.
- [ ] Confirm directory, read, write, and commit failures print diagnostics to stdout without exposing setting values.
- [ ] Run the complete debug configure, build, and CTest suite; confirm no frontend, CLI, or thread-safety contract is unintentionally changed.
