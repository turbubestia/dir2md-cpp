# Implementation Plan: 7-fzy-cpp-wrapper

[Analysis Reference](./7-fzy-cpp-wrapper.plan.analysis.md)

---

## Phase 1 — Header Declaration

**References:** Analysis Section 2 (Component & File Impact Map), `src/backend/core/fzy.hpp`; Requirements FZY-007, FZY-008, FZY-009, FZY-010.

### Step 1.1 — Create `src/backend/core/fzy.hpp`

Create the public header declaring the `dir2md::backend::fzy` namespace with the following structure:

1. Include `<string_view>`, `<vector>`, `<utility>` for standard-library types.
2. Include the vendored C header `<fzy/match.h>` to expose `score_t` as a type alias (`using score_t = ::score_t;`).
3. Declare five public functions inside the namespace, in this exact order:

   - `auto has_match(std::string_view needle, std::string_view haystack) -> int;` — Returns non-zero if the needle matches the haystack, zero otherwise. An empty needle always returns zero.
   - `auto match(std::string_view needle, std::string_view haystack) -> score_t;` — Returns the fzy score for the match. An empty needle or a non-matching target returns `SCORE_MIN`.
   - `auto match_positions(std::string_view needle, std::string_view haystack) -> std::pair<score_t, std::vector<size_t>>;` — Returns the score and a vector of character positions in needle order. An empty needle or non-match returns `SCORE_MIN` with an empty position vector.
   - `auto match(std::string_view needle, std::vector<std::string_view> haystack) -> std::vector<size_t>;` — Returns original indices of matching targets, sorted by descending score with stable ordering for ties. An empty needle returns an empty vector.
   - `auto match_positions(std::string_view needle, std::vector<std::string_view> haystack) -> std::vector<std::pair<size_t, std::vector<size_t>>>;` — Returns pairs of (original_index, position_vector) for matching targets in the same descending-score order as the batch `match` overload. An empty needle returns an empty vector.

4. Add Doxygen-style comments to each function documenting:
   - The empty-needle contract (never matches, returns minimum score / empty results).
   - The no-match contract (returns `SCORE_MIN` with empty positions for single-target; excluded from batch results).
   - The stable-score-ordering guarantee for batch operations.

### Phase 1 Exit Criterion
Header file compiles independently when included in a translation unit that links the vendored fzy library. No implementation code is present — declarations only.

### Validation Command
```bash
cmake --build --preset debug 2>&1 | Select-String "fzy.hpp"
# Expected: no errors referencing fzy.hpp; file should not yet be compiled.
```

---

## Phase 2 — Wrapper Implementation

**References:** Analysis Section 2 (Component & File Impact Map), `src/backend/core/fzy.cpp`; Requirements FZY-007, FZY-008, FZY-009, FZY-010; Analysis Section 3 (Boundary & Edge Case Analysis).

### Step 2.1 — Create `src/backend/core/fzy.cpp`

Create the implementation file with the following structure:

1. Include the header `fzy.hpp`.
2. Include `<string>` for null-terminated C API adaptation.
3. Implement a private helper lambda or static function (within an anonymous namespace) that converts `std::string_view` to a null-terminated `std::string` for passing to the fzy C API. This ensures caller-owned `string_view` storage is never retained or dereferenced after the call.
4. Implement each of the five public functions declared in Phase 1:

   **Single-target `has_match`:**
   - If `needle.empty()`, return `0` immediately (empty-needle contract).
   - Otherwise, delegate to the vendored `::has_match(needle.data(), haystack.data())`.

   **Single-target `match`:**
   - If `needle.empty()`, return `SCORE_MIN`.
   - Otherwise, delegate to the vendored `::match(needle.data(), haystack.data())`.

   **Single-target `match_positions`:**
   - If `needle.empty()`, return `{SCORE_MIN, std::vector<size_t>{}}`.
   - Allocate a `std::vector<size_t>` of size `needle.length()` (satisfying the C API capacity requirement).
   - Call the vendored `::match_positions(needle.data(), haystack.data(), positions.data())`.
   - If the returned score is `SCORE_MIN` (no match), return `{SCORE_MIN, std::vector<size_t>{}}` (empty position vector per no-match contract).
   - Otherwise, return `{score, positions}`.

   **Batch `match`:**
   - If `needle.empty()`, return an empty vector immediately.
   - Create a temporary struct or pair type holding `(original_index, score)` for each target.
   - Iterate over the input `haystack` vector with its index; for each target, call the single-target `match` helper. If the returned score is greater than `SCORE_MIN`, append `{index, score}` to a matches collection.
   - Sort the matches collection by descending score using a stable sort (e.g., `std::stable_sort`) so equal scores retain original input order.
   - Extract and return only the indices from the sorted matches collection.

   **Batch `match_positions`:**
   - If `needle.empty()`, return an empty vector immediately.
   - Use the same matching logic as the batch `match` overload to build the sorted matches collection (ensuring identical ordering per Requirement FZY-009).
   - For each matched entry in sorted order, call the single-target `match_positions` helper to obtain the position vector.
   - Append `{original_index, positions}` to the result collection.
   - Return the result collection.

5. All functions must use `std::string_view` parameters (no ownership transfer). No global or static mutable state is introduced.

### Phase 2 Exit Criterion
The wrapper implementation compiles and links into the `dir2md_backend` library without warnings. The fzy C API is called only through null-terminated string conversions.

### Validation Command
```bash
cmake --build --preset debug 2>&1 | Select-String "fzy.cpp|dir2md_backend"
# Expected: successful compilation and linking of fzy.cpp into dir2md_backend; no errors or warnings.
```

---

## Phase 3 — CMake Integration (Backend)

**References:** Analysis Section 2, `src/backend/core/CMakeLists.txt`; Requirement FZY-001.

### Step 3.1 — Update `src/backend/core/CMakeLists.txt`

Add the new wrapper files to the existing `target_sources(dir2md_backend PRIVATE ...)` invocation:

- Insert `fzy.hpp` and `fzy.cpp` into the source list alongside the existing files (`assert.hpp`, `core_schema.hpp`, `core_schema.cpp`, `settings_manager.hpp`, `settings_manager.cpp`).
- Preserve the existing dependency direction: the backend library continues to receive the vendored `fzy` target linkage from its parent `src/backend/CMakeLists.txt`.

### Phase 3 Exit Criterion
The `dir2md_backend` static library includes the wrapper source files. CMake configure and build succeed without changes to the fzy discovery mechanism.

### Validation Command
```bash
cmake --build --preset debug 2>&1 | Select-String "error|warning"
# Expected: no errors or warnings related to fzy.hpp, fzy.cpp, or dir2md_backend.
```

---

## Phase 4 — Test Infrastructure (Header + Main)

**References:** Analysis Section 2, `test/backend/core/fzy_test.hpp`, `test/backend/core/fzy_test_main.cpp`; Requirement FZY-006.

### Step 4.1 — Create `test/backend/core/fzy_test.hpp`

Create a QtTest test class following the existing pattern from `setting_manager_test.hpp`:

1. Include `<QObject>`, `<QTest>`.
2. Define class `fzy_test : public QObject` with `Q_OBJECT` and `QTEST_MAIN(fzy_test)`.
3. Declare private slots (test methods), one per logical test group:

   - `void test_has_match_empty_needle();` — Empty needle returns zero for any target.
   - `void test_has_match_non_matching();` — Non-matching needle returns zero.
   - `void test_has_match_matching();` — Valid match returns non-zero.
   - `void test_match_empty_needle();` — Empty needle returns `SCORE_MIN`.
   - `void test_match_non_matching();` — Non-matching target returns `SCORE_MIN`.
   - `void test_match_matching();` — Valid match returns a score greater than `SCORE_MIN`.
   - `void test_match_positions_empty_needle();` — Empty needle returns `{SCORE_MIN, {}}`.
   - `void test_match_positions_non_matching();` — Non-match returns `{SCORE_MIN, {}}`.
   - `void test_match_positions_matching();` — Valid match returns score and position vector.
   - `void test_batch_match_sample();` — Verify the supplied sample (`"edfont"` against three targets) returns indices `[2, 1]`.
   - `void test_batch_match_empty_collection();` — Empty target vector returns empty result.
   - `void test_batch_match_empty_target();` — Non-empty needle against an empty target does not match.
   - `void test_batch_match_no_matches();` — Needle that matches nothing returns empty result.
   - `void test_batch_match_duplicate_targets();` — Duplicate target strings are distinguished by original index.
   - `void test_batch_match_equal_scores();` — Equal scores retain input order (ascending original index).
   - `void test_batch_match_positions_sample();` — Verify the supplied sample returns pairs indexed `2` and `1` with positions `[[0, 1, 7, 8, 9, 10], [17, 18, 20, 21, 22, 23]]`.
   - `void test_batch_match_positions_consistency();` — Batch index order matches batch position order for the same input.
   - `void test_batch_match_positions_position_validity();` — Every returned position is in range for its indexed target and identifies the corresponding needle character.
   - `void test_needle_longer_than_target();` — Needle longer than target does not cause undefined behavior; returns no match.
   - `void test_empty_needle_batch_all_overloads();` — Empty needle produces empty results from all batch overloads.

### Step 4.2 — Create `test/backend/core/fzy_test_main.cpp`

Create the QtTest entry point following the existing pattern from `test/backend/core/main.cpp`:

1. Include `<QTest>`.
2. Include `"fzy_test.hpp"`.
3. Call `QTEST_MAIN(fzy_test)`.

### Phase 4 Exit Criterion
Test header and main files compile as part of the test target. No test logic is implemented yet — only declarations and entry point.

### Validation Command
```bash
cmake --build --preset debug 2>&1 | Select-String "fzy_test"
# Expected: no errors referencing fzy_test files; test target not yet registered in CMake.
```

---

## Phase 5 — Test Implementation

**References:** Analysis Section 4 (Verification Checklist); Requirement FZY-006; Analysis Section 3 (Boundary & Edge Case Analysis).

### Step 5.1 — Create `test/backend/core/fzy_test.cpp`

Implement all test slots declared in Phase 4 using QtTest assertion macros (`QCOMPARE`, `QVERIFY`, `QTEST`). Structure each test as follows:

**Single-target tests:**
- `test_has_match_empty_needle`: Call `has_match("", "any_target")` and verify the result is `0`. Test against multiple targets.
- `test_has_match_non_matching`: Call `has_match("xyz", "no_match_here")` and verify the result is `0`.
- `test_has_match_matching`: Call `has_match("edf", "editor.fontSize")` and verify the result is non-zero.

**Single-target score tests:**
- `test_match_empty_needle`: Call `match("", "any_target")` and verify the result equals `SCORE_MIN`.
- `test_match_non_matching`: Call `match("xyz", "no_match_here")` and verify the result equals `SCORE_MIN`.
- `test_match_matching`: Call `match("edf", "editor.fontSize")` and verify the result is greater than `SCORE_MIN`.

**Single-target position tests:**
- `test_match_positions_empty_needle`: Call `match_positions("", "any_target")` and verify the pair equals `{SCORE_MIN, std::vector<size_t>{}}`.
- `test_match_positions_non_matching`: Call `match_positions("xyz", "no_match_here")` and verify the pair equals `{SCORE_MIN, std::vector<size_t>{}}`.
- `test_match_positions_matching`: Call `match_positions("edf", "editor.fontSize")` and verify the score is greater than `SCORE_MIN` and the position vector has exactly 3 elements.

**Batch match tests:**
- `test_batch_match_sample`: Create the sample input (`"edfont"` against `["files.autoSave", "terminal.integrated.fontSize", "editor.fontSize"]`). Call `match(needle, targets)`. Verify the result equals `{2, 1}`.
- `test_batch_match_empty_collection`: Pass an empty vector as haystack. Verify the result is empty.
- `test_batch_match_empty_target`: Include an empty string in the target vector. Verify it does not appear in results for a non-empty needle.
- `test_batch_match_no_matches`: Use a needle that matches nothing. Verify the result is empty.
- `test_batch_match_duplicate_targets`: Create targets with duplicate strings at different indices. Verify both original indices appear in results.
- `test_batch_match_equal_scores`: Create targets with equal scores (e.g., identical strings at different indices). Verify they are returned in ascending original-index order.

**Batch position tests:**
- `test_batch_match_positions_sample`: Use the sample input. Call `match_positions(needle, targets)`. Verify the result contains two pairs: `{2, {0, 1, 7, 8, 9, 10}}` and `{1, {17, 18, 20, 21, 22, 23}}`.
- `test_batch_match_positions_consistency`: Call both batch overloads on the same input. Verify the index sequences are identical.
- `test_batch_match_positions_position_validity`: For each returned pair, verify every position is less than the target string length and that `target[position]` equals the corresponding needle character.

**Edge case tests:**
- `test_needle_longer_than_target`: Use a needle longer than its target. Verify no match occurs without undefined behavior (no crash, no assertion failure).
- `test_empty_needle_batch_all_overloads`: Call both batch overloads with an empty needle. Verify both return empty vectors.

### Phase 5 Exit Criterion
All test implementations compile and link into the `backend_core_test` executable. Each test slot is discoverable by QtTest.

### Validation Command
```bash
cmake --build --preset debug 2>&1 | Select-String "fzy_test.cpp"
# Expected: successful compilation of fzy_test.cpp as part of backend_core_test.
```

---

## Phase 6 — CMake Integration (Tests)

**References:** Analysis Section 2, `test/backend/core/CMakeLists.txt`; Requirement FZY-006; Analysis Section 4 (Verification Checklist).

### Step 6.1 — Update `test/backend/core/CMakeLists.txt`

1. Add the new test files to the existing `qt6_add_executable(backend_core_test ...)` invocation:
   - Insert `fzy_test.hpp`, `fzy_test.cpp` into the source list alongside the existing files.
2. Register the new test source with `qtest_add_test`:
   - Add a second `qtest_add_test()` call (or extend the existing one's `SOURCES` argument) to include `fzy_test.cpp` with prefix `backend.core`.
   - This ensures CTest and VS Code Test Explorer discover each test slot.
3. Preserve the existing C++20 requirement and linkage to `Qt6::Test` and `dir2md_backend`.

### Phase 6 Exit Criterion
The `backend_core_test` executable includes the fzy test sources. CMake configure succeeds. CTest discovers all test slots from both test files.

### Validation Command
```bash
cmake --preset debug 2>&1 | Select-String "error"
# Expected: no CMake errors.
ctest --preset debug --test-name-pattern "backend.core.fzy*" --output-on-failure
# Expected: all fzy_test slots discovered and passing.
```

---

## Phase 7 — Full Verification & Coverage

**References:** Analysis Section 4 (Verification Checklist); Requirement FZY-006.

### Step 7.1 — Run the Debug Test Suite

Execute the full debug test suite to confirm existing tests remain intact:

```bash
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Verify:
- All existing backend, frontend, and CLI tests pass.
- All new fzy_test slots pass.
- No regressions in settings_manager_test or other existing tests.

### Step 7.2 — Run Coverage Build

Execute the coverage build to verify the wrapper is exercised by tests:

```bash
cmake --build --preset debug-coverage
ctest --preset debug-coverage --output-on-failure
llvm-profdata merge -o default.profdata *.profraw
llvm-cov show build/cmake-debug-coverage/src/backend/core/fzy.cpp -instr-profile=default.profdata
```

Verify:
- The wrapper source file is covered by test execution.
- All five public functions have at least one executed branch.

### Step 7.3 — Final Checklist Verification

Confirm each item from the Analysis Section 4 (Verification Checklist):

- [ ] Configure the debug CMake preset successfully with the existing vendored fzy discovery.
- [ ] Build the debug preset and confirm `dir2md_backend` compiles and links the wrapper source.
- [ ] Run the dedicated wrapper QtTest target through CTest and confirm per-slot discovery through `qtest_add_test`.
- [ ] Verify direct `has_match`, `match`, and `match_positions` behavior for a valid matching needle and target.
- [ ] Verify a non-matching single target yields the minimum score and an empty position vector, while `has_match` reports no match.
- [ ] Verify an empty needle is never considered a match and all batch overloads return empty collections.
- [ ] Verify empty target collections, empty targets, and needles longer than targets do not cause undefined behavior or produce position results for non-matches.
- [ ] Verify the supplied sample returns batch indices `[2, 1]`, with position pairs indexed `2` and `1` and the specified character offsets.
- [ ] Verify every returned position is in range for its indexed target and identifies the corresponding needle character.
- [ ] Verify batch matching excludes non-matches, preserves original indices for duplicate strings, ranks descending by score, and retains input order when scores tie.
- [ ] Verify batch index and batch-position operations have identical index order for the same input.
- [ ] Run the full debug test suite to confirm existing backend, frontend, and CMake test registration behavior remains intact.

### Phase 7 Exit Criterion
All checklist items are verified. Coverage build confirms the wrapper is exercised. No regressions in the existing test suite.

### Validation Command
```bash
cmake --build --preset debug-coverage
ctest --preset debug-coverage --output-on-failure
llvm-profdata merge -o default.profdata *.profraw
llvm-cov show build/cmake-debug-coverage/src/backend/core/fzy.cpp -instr-profile=default.profdata
# Expected: fzy.cpp is covered; all tests pass; no regressions.
```

---

## Summary of Files to Create / Modify

| File | Action | Phase |
|------|--------|-------|
| `src/backend/core/fzy.hpp` | Create | 1 |
| `src/backend/core/fzy.cpp` | Create | 2 |
| `src/backend/core/CMakeLists.txt` | Modify (add sources) | 3 |
| `test/backend/core/fzy_test.hpp` | Create | 4 |
| `test/backend/core/fzy_test_main.cpp` | Create | 4 |
| `test/backend/core/fzy_test.cpp` | Create | 5 |
| `test/backend/core/CMakeLists.txt` | Modify (add sources + qtest_add_test) | 6 |

## Summary of Requirements Traceability

| Requirement | Phases |
|-------------|--------|
| FZY-001: C++ wrapper boundary | 1, 2, 3 |
| FZY-007: Public wrapper API | 1, 2 |
| FZY-008: Empty and non-matching queries | 2, 5 |
| FZY-009: Score ordering and original indices | 2, 5 |
| FZY-010: Position result alignment | 2, 5 |
| FZY-006: Verification (tests) | 4, 5, 6, 7 |
