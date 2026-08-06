# Implementation Analysis: 7-fzy-cpp-wrapper

## 1. Architectural Impact & Data Flow

The backend will gain a narrow C++ adaptation layer around the already-vendored `fzy` C library. The wrapper owns conversion at the C/C++ boundary and exposes only `std::string_view`, `std::vector`, and pair-based result values to backend consumers. It does not introduce Qt, frontend, or CLI dependencies.

- **Affected Subsystems:** Backend core library, vendored `fzy` integration through the existing `fzy` target, and backend QtTest coverage.
- **Data Flow Changes:** Caller provides a needle and either one target or a vector of target views -> `dir2md::backend::fzy` validates the empty-needle contract -> the wrapper invokes the underlying `fzy` matcher for applicable targets -> batch operations retain each source index and score -> matching entries are ordered by descending score while preserving input order for ties -> public results contain either original input indices or original indices paired with position vectors.
- **Boundary Ownership:** The wrapper is the only new backend component that includes the vendored C API. It does not retain caller-owned string-view storage; all returned collections are independently owned.
- **API Contract:** Single-target operations provide match status, score, and score with positions. Batch operations filter unmatched targets, preserve original indices including duplicates, and expose results in one consistent stable score order.

## 2. Component & File Impact Map

### `src/backend/core/fzy.hpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] Declare the `dir2md::backend::fzy` namespace and the locked single-target and batch overloads using `std::string_view` and STL collection/result types.
  - [ ] Make the `score_t` type required by the public score-bearing return values available from the existing `fzy` public interface.
  - [ ] Document the direct-call contract that an empty needle is not a match, and that an unmatched single target yields the minimum score and an empty position vector.
  - [ ] Define batch result semantics: original input indices, descending score order, and stable original ordering when scores tie.

### `src/backend/core/fzy.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] Provide the C++ wrapper implementation behind the public header.
  - [ ] Adapt bounded `std::string_view` inputs safely for the null-terminated C API without retaining caller storage after the call.
  - [ ] Centralize the single-target match decision so all overloads apply identical empty-needle and no-match rules.
- **Logic Modifications Required:**
  - [ ] Delegate valid single-target matching and scoring to the vendored `fzy` API while preserving its matching and case-sensitivity semantics.
  - [ ] Allocate position result storage for every needle character before invoking the C position API, then return no positions for a non-match.
  - [ ] For batch operations, evaluate each target against the same needle, discard unmatched entries, retain its original vector index and score, and stably rank the retained entries by descending score.
  - [ ] Return batch position pairs in exactly the same target order as batch index results, with each vector aligned to the corresponding needle-character order.

### `src/backend/core/CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add the new wrapper header and implementation to the existing `dir2md_backend` core source list so the static backend library owns the wrapper.
  - [ ] Preserve the existing dependency direction: the backend library continues to receive the vendored `fzy` dependency from its parent backend CMake target.

### `test/backend/core/fzy_test.hpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] Define a dedicated QtTest test class with slots for the wrapper API contract.
  - [ ] Cover direct single-target status, score, and position behavior without coupling these tests to settings-manager tests.

### `test/backend/core/fzy_test.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] Implement focused wrapper tests using standard-library target collections and the backend public include path.
- **Logic Modifications Required:**
  - [ ] Verify the supplied `edfont` sample returns indices `[2, 1]` and indexed position results for targets `2` then `1`, with the specified offsets.
  - [ ] Verify batch score ordering, stable original ordering for equal scores, and preservation of distinct original indices for duplicate target values.
  - [ ] Verify that batch index and batch-position overloads produce identical target-index ordering and that each returned position maps to the matching character in its associated target.
  - [ ] Verify empty target collections, empty targets, no matches, queries longer than targets, and a non-empty needle with no match.
  - [ ] Verify an empty needle reports no direct match, gives the minimum score with an empty position vector, and produces empty batch results.

### `test/backend/core/fzy_test_main.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] Supply a dedicated QtTest executable entry point for the wrapper test class, matching the existing backend-core test organization.

### `test/backend/core/CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Register the dedicated wrapper test executable with its sources, backend include path, QtTest dependency, and `dir2md_backend` linkage.
  - [ ] Register the new test source with `qtest_add_test` so CTest and VS Code Test Explorer discover each test slot.
  - [ ] Preserve the project C++20 requirement for the new test target.

## 3. Boundary & Edge Case Analysis

- **Error Handling:** The wrapped C API does not expose recoverable error objects. The C++ boundary therefore represents a non-match as the documented minimum score and empty positions for single-target position results; `has_match` reports no match; and batch overloads omit that target. Empty needles are handled at the wrapper boundary as non-matches, without passing them to search behavior that callers are expected to skip.
- **Input and Lifetime Boundaries:** `std::string_view` may refer to non-null-terminated substrings and caller-owned memory. The wrapper must treat views only as call-duration inputs and must provide C-compatible input storage for each underlying invocation. Returned indices and positions must not refer to view storage.
- **Position Capacity:** Before calling the C `match_positions` API, storage must contain at least one element per needle character. A needle longer than its target remains valid wrapper input and resolves through the no-match contract without exposing incomplete or stale positions.
- **Ordering and Consistency:** Batch ranking is descending by the raw `fzy` score. Equal scores retain the original target-vector order, which establishes deterministic behavior for duplicate strings. Both batch overloads must use the same retained-entry ordering so their index sequences remain identical.
- **Empty Inputs:** Empty target vectors produce empty result collections. Empty targets do not match non-empty needles. An empty needle matches nothing, including against an empty target, and produces the no-match form for direct methods.
- **Security & Permissions:** This pure in-process backend utility has no external I/O, authorization, persistence, or new permissions.
- **Performance / Scale Impact:** Batch matching is linear in the number of targets plus sorting the matched subset. Position allocation is proportional to needle length per matched target. The wrapper should only compute position vectors for targets included in position results and should not retain copies beyond each operation.
- **Integration Boundary:** No changes are required to upstream `thirdparty/fzy` sources, the `Findfzy.cmake` package definition, frontend, or CLI. `dir2md_backend` already links the `fzy` target and therefore supplies its public `<fzy/match.h>` include contract to the new core implementation.

## 4. Verification Checklist

- [ ] Configure the debug CMake preset successfully with the existing vendored `fzy` discovery.
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
