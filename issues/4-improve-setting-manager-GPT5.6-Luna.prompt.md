# Implementation Plan: Improve Settings Manager

[Analysis Reference](./4-improve-setting-manager.plan.analysis.md)

This plan implements the locked settings-manager contract from Analysis Sections 1 through 4 and Requirements SM-01 through SM-16. It is an implementation blueprint only: the coding phase must make the described changes without copying code or pseudocode into this document.

## Phase 1: Establish the Contract and Build Boundary

**Traceability:** Analysis Sections 1, 2, and 3; Requirements SM-01, SM-02, SM-03, SM-04, SM-07, SM-08, SM-10, SM-11, SM-12, SM-13, SM-14, and SM-16.

### Steps

1. Review the current `SettingSchema` and `SettingsManager` public surface in `src/backend/core/settings_manager.hpp`. Preserve the reference-returning `activeValues()` and `schemas()` accessors, the existing signals, and the non-thread-safe ownership model required by SM-07. Keep the public API as stable as possible; introduce only the smallest declarations needed to distinguish production filename persistence from the debug test-path capability required by SM-10 and SM-14.
2. Define the single key-validation contract used by schema registration, assignment, saving, and loading: keys must be non-empty, slash-separated, free of whitespace, contain no empty segments, and have a first segment compatible with the schema category. Decide and document the representation used for a schema's category and enum metadata before changing implementation logic. This implements SM-01, SM-02, and the category/path boundaries in Analysis Section 3.
3. Define the persistence-path boundary. Production operations must resolve a filename beneath `{QDir::homePath()}/.config/dir2md` and reject empty names, absolute paths, separators, parent traversal, normalization escapes, and paths outside the root. Explicit file and directory selection must exist only when the dedicated test compile definition is present, as required by SM-04, SM-10, and SM-14.
4. Add the dedicated test-path compile definition to the `debug` configure preset and propagate it only to the narrowest target boundary that needs it. Do not enable the bypass in the release preset or through runtime input. Keep the backend QtCore dependency and existing target structure unchanged, per Analysis Section 2 and SM-14.

**Exit criterion:** The header-level contract, path-mode boundary, and debug-only build capability are unambiguous; production builds cannot select an arbitrary persistence directory; no out-of-scope API or thread-safety redesign has been introduced.

**Validation command:** `cmake --preset debug` followed by `cmake --preset release`; inspect the configured target definitions to confirm the test symbol is present only in the intended debug/test target and absent from release.

## Phase 2: Implement Schema and Value Validation

**Traceability:** Analysis Sections 1, 2, and 3; Requirements SM-01, SM-02, SM-03, SM-08, and SM-12.

### Steps

1. Update `SettingSchema::isValid` in `src/backend/core/settings_manager.cpp` so conversion capability is not treated as conversion success. Attempt conversion to the declared `QMetaType`, check its result, and validate the converted value rather than the original variant. Ensure numeric bounds are compared against the converted numeric representation and that invalid bounds cannot accept an out-of-range value. This implements SM-02.
2. Implement precise enum validation using the schema's declared enum metadata. Accept registered symbolic names and valid numeric representations according to the chosen metadata contract; reject unknown names and values outside the allowed enum set even when their broad QVariant type is convertible. Implement Qt meta-enum lookup for symbolic serialization and numeric fallback when no symbolic key is discoverable, with the debug-only stdout warning required by SM-12 and SM-13.
3. Make `set` validate key syntax, category consistency, schema existence, conversion success, range constraints, and enum membership before mutating `m_values`. Unknown keys and invalid values must return the existing failure result without throwing and must leave any previous active value unchanged. Store the converted, schema-typed value so successful assignments have canonical types. This implements SM-01 and SM-02.
4. Make `registerSchema` validate the incoming schema identity and replace an existing schema deterministically. Before or as the replacement commits, revalidate an existing active value against the replacement schema; retain it when valid and remove it when invalid so `get` returns the replacement default. Preserve the existing effective-value notification behavior, including notification when replacement or removal changes the effective value. Do not invent a new event-consumer contract. This implements SM-03 and SM-08.

**Exit criterion:** All schema-backed writes are validated and typed, unknown writes are rejected without mutation, enum and numeric constraints are exact, and schema replacement cannot leave an incompatible active value behind.

**Validation command:** Build and run the focused backend test target after the first validation tests are added: `cmake --build --preset debug --target backend_core_test` and `ctest --test-dir build/cmake-debug -R backend\.core --output-on-failure`.

## Phase 3: Replace Recursive JSON Traversal and Define Persistence Representation

**Traceability:** Analysis Sections 1, 2, and 3; Requirements SM-04, SM-06, SM-09, SM-12, and SM-13.

### Steps

1. Replace the recursive `insertNestedValue` helper with an explicit stack-based traversal. Validate all path segments before traversal, create missing object nodes, and replace an intermediate scalar with an object when the requested nested value requires it. Preserve the category-rooted JSON format and ensure the final leaf contains the serialized typed value. This implements SM-06 and the insertion requirements in Analysis Sections 2 and 3.
2. Replace the recursive `flattenJsonObject` helper with a stack-based traversal that carries the complete path, including the category root, for every leaf. Reject or report malformed empty path segments rather than silently normalizing them. Ensure traversal depth is limited by available data structures rather than the C++ call stack. This implements SM-01, SM-05, and SM-06.
3. Update save preparation so only schema-backed, currently valid active values are serialized. Resolve each setting's category from its schema, retain the category in the nested document, and serialize enum values symbolically when Qt metadata provides a name. Use numeric enum fallback with the required debug warning when no symbolic key exists. Unknown or stale active entries must not be written. This implements SM-09, SM-12, and SM-13.
4. Centralize production path resolution and validation for both save and load. Create the application configuration directory according to the existing Qt filesystem APIs, report directory and file failures to stdout without printing setting values, and retain atomic replacement through `QSaveFile` for successful writes. This implements SM-04 and SM-11.

**Exit criterion:** Save produces the existing category-grouped JSON shape using only valid schema-backed values; insertion and flattening are iterative; enum output and production path checks follow the locked contract.

**Validation command:** `cmake --build --preset debug --target backend_core_test`; run the focused save tests with `ctest --test-dir build/cmake-debug -R backend\.core --output-on-failure`.

## Phase 4: Implement Atomic, Tolerant Loading

**Traceability:** Analysis Sections 1, 2, and 3; Requirements SM-04, SM-05, SM-09, SM-10, SM-11, SM-12, SM-13, SM-15, and SM-16.

### Steps

1. Validate the requested load path before opening a file. Apply the production directory restriction unless the compile-time debug test-path capability is active. Treat empty, absolute, separator-containing, traversal, and outside-root paths as file-level failures and report them using the interim stdout policy. This implements SM-04, SM-10, and SM-11.
2. Open and parse the document without changing `m_values`. Treat unreadable files, malformed JSON, and any top-level value that is not the required object structure as file-level failures. Preserve every existing active value on those failures, including valid JSON with an invalid top-level structure. This implements SM-05 and SM-15.
3. Flatten the object iteratively while retaining the category in every flattened key. For each leaf, validate the complete category/key relationship against the schema registry, ignore unknown or invalid entries with a warning, and continue processing other entries. Convert accepted values successfully to the schema type, including symbolic and numeric enum forms. This implements SM-01, SM-06, SM-09, and SM-12.
4. Stage accepted values in a candidate map. After file-level validation and all entry processing complete, replace `m_values` in one state transition so ignored entries cannot survive into a later save. Diff the old and candidate effective values and emit the existing `settingChanged` signal behavior for changed, added, or disappeared effective values without defining a new removal signal. This implements SM-05 and SM-08.
5. Keep successful load semantics separate from file-level failure semantics: a valid object containing some bad entries succeeds with the valid subset, while read, parse, and top-level failures return failure and preserve the previous map. This implements SM-09, SM-15, and SM-16.

**Exit criterion:** Loading is atomic for file-level failures, tolerant for individual invalid entries, category-preserving, path-bounded, and capable of loading enum symbolic names and numeric fallbacks without retaining ignored data.

**Validation command:** `cmake --build --preset debug --target backend_core_test`; `ctest --test-dir build/cmake-debug -R backend\.core --output-on-failure`.

## Phase 5: Add Focused Regression Coverage

**Traceability:** Analysis Sections 2, 3, and 4; Requirements SM-01 through SM-16.

### Steps

1. Update the existing backend test fixture in `test/backend/core/setting_manager_test.hpp` and `test/backend/core/setting_manager_test.cpp`. Keep tests single-threaded and use isolated temporary paths through the debug-only test capability. Do not create tests that require changing the reference-returning APIs or thread-safety behavior. This implements the test constraints in SM-07, SM-10, and SM-14.
2. Replace legacy assertions that expect schema-less `set` or save behavior with tests for rejection and non-mutation. Add cases for unknown keys, leading or trailing separators, repeated separators, empty segments, whitespace, category mismatch, conversion failure, canonical typed storage, numeric range boundaries, and unchanged prior values after rejection. This covers SM-01 and SM-02.
3. Add schema replacement tests for valid-value retention, incompatible-value removal, default fallback, deterministic registry replacement, and effective-value notifications when the effective result changes or disappears. This covers SM-03 and SM-08.
4. Add JSON traversal tests for deeply nested insertion and flattening, intermediate scalar replacement, empty-path rejection, category-preserving flattened keys, and category mismatch handling. The tests must demonstrate behavior without relying on recursive call-stack depth. This covers SM-06 and the traversal items in Analysis Section 4.
5. Add save tests for schema-backed values only, nested category-rooted JSON, enum symbolic serialization, numeric enum fallback plus debug warning behavior, invalid path rejection, directory creation failure reporting, write/commit failure reporting, and absence of unknown or invalid entries after a later save. This covers SM-04, SM-09, SM-11, SM-12, and SM-13.
6. Add load tests for mixed valid and invalid entries, unknown-entry warnings, successful subset loading, ignored-entry removal after save, malformed JSON, unreadable files, invalid top-level JSON structures, category mismatches, conversion failures, enum names and numeric fallback, and preservation of all prior active values on file-level failure. This covers SM-05, SM-09, SM-11, SM-13, and SM-15.
7. Add production-path tests for empty names, absolute paths, traversal, separators, normalization escapes, and outside-root paths. Add debug-only tests proving explicit temporary file and directory access works only when the CMake definition is present. This covers SM-10 and SM-14.
8. Retain coverage for successful JSON creation, nested keys, round-trip types, missing files, malformed JSON, and successful `settingsSaved` notifications, updating their setup to register valid schemas and use the supported persistence path mode. This preserves prior behavior where it remains compatible with SM-16.

**Exit criterion:** The existing backend test target covers every acceptance criterion from SM-01 through SM-16, including both individual-entry tolerance and file-level atomicity, while explicitly leaving SM-07 no-action boundaries untouched.

**Validation command:** `cmake --build --preset debug --target backend_core_test`; `ctest --test-dir build/cmake-debug -R backend\.core --output-on-failure`.

## Phase 6: Full Build, Coverage, and Contract Review

**Traceability:** Analysis Section 4; Requirements SM-07, SM-14, and SM-16.

### Steps

1. Configure and build the complete debug project with `cmake --preset debug` and `cmake --build --preset debug`. Confirm backend, CLI, and frontend targets still compile and no QtWidgets dependency has entered the backend. This validates the cross-target boundary in Analysis Section 2 and SM-16.
2. Run the complete registered debug suite with `ctest --test-dir build/cmake-debug --output-on-failure`. Confirm the settings-manager tests pass alongside all unrelated project tests. This validates SM-16.
3. Configure and build coverage with `cmake --preset debug-coverage` and `cmake --build --preset debug-coverage`. Run the relevant application/test target so coverage data is produced, then merge the generated profile data with `llvm-profdata merge -o default.profdata *.profraw` and inspect the backend settings-manager implementation with `llvm-cov show` using the built instrumented executable and profile. This validates the coverage requirement in Analysis Section 4 and SM-16.
4. Review the final diff for accidental changes to `activeValues()`, `schemas()`, thread-safety/re-entrancy behavior, event-consumer semantics, persistence JSON structure, CLI/frontend ownership, or unrelated files. Confirm release configuration does not expose the test-path bypass. This preserves SM-07 and SM-14.
5. Confirm all persistence diagnostics are explanatory, go to stdout under the interim policy, and do not include setting values. Confirm successful writes emit `settingsSaved` only after atomic commit and successful loads notify only according to the existing effective-value signal contract. This validates SM-08, SM-11, SM-15, and SM-16.

**Exit criterion:** Debug and coverage builds succeed, the full CTest suite passes, coverage exercises the new validation/traversal/path/atomic-load branches, and the final implementation satisfies SM-01 through SM-16 without violating the explicit no-action boundaries.

**Validation command:** `cmake --preset debug`; `cmake --build --preset debug`; `ctest --test-dir build/cmake-debug --output-on-failure`; `cmake --preset debug-coverage`; `cmake --build --preset debug-coverage`; run the instrumented backend tests and inspect the resulting report with `llvm-cov show`.

## Completion Checklist

- [ ] Every step above is implemented in the named existing source, test, or CMake surface.
- [ ] Every requirement SM-01 through SM-16 has at least one focused regression test.
- [ ] Production persistence is confined to `{QDir::homePath()}/.config/dir2md`.
- [ ] Explicit test paths require the debug CMake compile definition.
- [ ] JSON insertion and flattening are stack-based and category-preserving.
- [ ] Invalid individual entries are ignored while file-level failures remain atomic.
- [ ] Schema replacement revalidates active values and restores defaults when needed.
- [ ] Enum symbolic persistence and numeric fallback behavior are covered.
- [ ] The no-action boundaries in SM-07 remain unchanged.
- [ ] Debug, release configuration, full CTest, and coverage validation have completed successfully.
