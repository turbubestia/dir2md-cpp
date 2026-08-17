# `src/backend/core/fzy.hpp` / `fzy.cpp`

## Purpose and Role

A thin C++ wrapper around the third-party **fzy** fuzzy-matching library (`thirdparty/fzy`, C API declared in `thirdparty/fzy/src/match.h`). It lives in the backend core (`dir2md::backend::fzy`) and provides a safe, idiomatic `std::string`-based interface for:

- checking whether a needle fuzzy-matches a haystack,
- scoring a single match,
- scoring a match with per-character positions,
- batch-matching one needle against many targets, ranked by score.

The wrapper exists to (a) hide the C `const char*` API behind `std::string`, (b) define project-level semantics for degenerate inputs (empty needle), and (c) provide batch ranking with stable tie-breaking. It is intended as a pure-logic utility shared by frontend and CLI; it holds no state.

## API Surface

All functions are free functions in `namespace dir2md::backend::fzy`. `score_t` is an alias for the library's `::score_t` (`double`; `SCORE_MIN == -INFINITY`, `SCORE_MAX == INFINITY`).

| Function | Signature (trailing return) | Returns |
|---|---|---|
| `has_match` | `(const std::string &needle, const std::string &haystack) -> int` | `0` if no match / empty needle, non-zero (`1`) on match |
| `match` (single) | `(const std::string &needle, const std::string &haystack) -> score_t` | fzy score, or `SCORE_MIN` if empty needle / no match |
| `match_positions` (single) | `(const std::string &needle, const std::string &haystack) -> std::pair<score_t, std::vector<size_t>>` | `(score, positions)`; `(SCORE_MIN, {})` if empty needle / no match |
| `match` (batch) | `(const std::string &needle, const std::vector<std::string> &haystack) -> std::vector<size_t>` | Original indices of matching targets, sorted by descending score, stable for ties; `{}` for empty needle |
| `match_positions` (batch) | `(const std::string &needle, const std::vector<std::string> &haystack) -> std::vector<std::pair<size_t, std::vector<size_t>>>` | `(original_index, positions)` pairs in the same order as the batch `match` overload; `{}` for empty needle |

## Behavior Details

### Empty-needle semantics (wrapper-defined)

The wrapper treats an **empty needle as "never a match"** across all five overloads: `has_match` returns `0`, single `match`/`match_positions` return `SCORE_MIN` / `(SCORE_MIN, {})`, and both batch overloads return empty vectors. This is a deliberate project-level convention: the underlying C API would treat an empty needle as a trivially satisfied match (its `if (!*needle) return SCORE_MIN;` guard only short-circuits scoring, while `has_match` returns `1` for an empty needle). The wrapper's early-return guards are what enforce the documented "empty needle never matches" contract.

### Single-target flow

- `has_match`: empty-needle guard, then delegates to `::has_match(needle.c_str(), haystack.c_str())`.
- `match`: empty-needle guard, then a `has_match` pre-check (returns `SCORE_MIN` on no match), then `::match`.
- `match_positions`: same guards, then allocates `std::vector<size_t> positions(needle.size())`, calls `::match_positions(..., positions.data())`, and returns `(SCORE_MIN, {})` if the library still reports `SCORE_MIN`; otherwise returns the score with the filled position vector (moved).

Positions are byte offsets into the haystack, one per needle character, in needle order. The wrapper pre-sizes the vector to `needle.size()`, which matches the number of entries the library writes on a successful match.

### Batch flow

Both batch overloads:

1. Return `{}` immediately for an empty needle.
2. Loop over all targets, calling the single-target `match` (which itself calls `has_match` + `::match`) and collecting `(original_index, score)` for entries with `score > SCORE_MIN`.
3. `std::stable_sort` by descending score — equal scores retain input order (verified by `test_batch_match_equal_scores`).
4. Batch `match`: extract indices. Batch `match_positions`: re-invoke the single-target `match_positions` for each matched entry to obtain its position vector.

Non-matching targets are excluded; duplicate target strings are distinguished by original index.

### Case sensitivity

Matching is case-insensitive (the library lowercases both strings internally). This is inherited behavior, not something the wrapper documents or controls.

## Invariants and Assumptions

- **No state, no allocation at namespace scope**: every function is self-contained; thread-safety of concurrent calls depends on the library's per-call local state (see Static Analysis).
- **`std::string` null-termination**: the wrapper relies on `std::string::c_str()` returning a NUL-terminated buffer; embedded NULs in a needle/haystack would truncate the pattern/target as seen by the C API.
- **Position vector size**: on success, `positions.size() == needle.size()` and each `positions[i]` indexes the character matching `needle[i]`.
- **Batch ordering contract**: descending score, stable for ties; indices are original (input) indices, not ranks.

## Contextual Dependencies

- `thirdparty/fzy/src/match.h` / `match.c`: C API (`has_match`, `match`, `match_positions`), `score_t`, `SCORE_MIN`/`SCORE_MAX`, and the `MATCH_MAX_LEN` limit (1024).
- `test/backend/core/fzy_test.cpp`: QTest suite covering empty needle, non-match, match, positions validity, batch ordering, duplicates, equal-score stability, and needle-longer-than-target.

## Static Analysis and Security

### Finding 1: Haystacks longer than 1024 characters silently never match (undocumented limit)

- **Evidence**: The wrapper passes `haystack.c_str()` straight through. Contextual dependency behavior (`thirdparty/fzy/src/match.c`, `setup_match_struct` and each scoring entry point): `if (match->haystack_len > MATCH_MAX_LEN || match->needle_len > match->haystack_len) return;` / `return SCORE_MIN;` with `MATCH_MAX_LEN == 1024`. So any haystack longer than 1024 bytes yields `has_match == 0` and `SCORE_MIN` from every overload, with no error or diagnostic.
- **Risk**: Callers matching against long strings (e.g., long file paths, concatenated setting keys, large text blobs) get silent non-matches that look like "no fuzzy hit" rather than a limitation. The wrapper's header documents empty-needle and no-match behavior but says nothing about the length cap.
- **Impact**: Correctness/maintainability — subtle feature degradation that is hard to diagnose; batch ranking silently drops long candidates.
- **Mitigation**: Document the 1024-byte haystack limit in `fzy.hpp` (and note that needles longer than the haystack also never match, which is inherent to subsequence matching). Optionally add an explicit size check in the wrapper that logs or asserts when a target exceeds the cap, making the limitation visible at the call boundary.
- **Follow-up test recommendation**: Add a test asserting `has_match`/`match`/batch overloads return no-match for a haystack of length 1025 (and a positive control at length ≤ 1024), pinning the limit as intended behavior.

### Finding 2: Batch `match_positions` recomputes scores and match checks (redundant work)

- **Evidence**: In `fzy.cpp`, batch `match_positions` first loops over all targets calling single-target `match` (each of which runs `has_match` + `::match`) to build the ranked list, then loops over the matched entries again calling single-target `match_positions` (which runs `has_match` + `::match_positions`). Every matched target is therefore scored twice and match-checked three times.
- **Risk**: Quadratic-ish overhead for large batches: O(N) scoring passes plus O(K) re-scoring passes (K = number of matches). For a UI autocomplete-style use case with hundreds of targets this is wasteful, though not incorrect.
- **Impact**: Performance only; no correctness or security effect. The batch `match` overload has the same per-target double call (`has_match` inside `match`), but at least does not recompute afterwards.
- **Mitigation**: Have the first pass collect `(index, score)` directly from a single `::match_positions` call per target (or cache scores in the `match_entry` and reuse them), eliminating the second scoring pass. Alternatively, document that batch callers should prefer `match` when positions are not needed.
- **Follow-up test recommendation**: A benchmark-style or timing-guarded test is not required; a unit test asserting batch `match_positions` output equals per-target single `match_positions` results (already partially covered by `test_batch_match_positions_consistency`) plus a large-N smoke test would guard against regressions if the optimization changes behavior.

### Finding 3: Duplicated batch logic between the two batch overloads

- **Evidence**: The collect-and-stable-sort block (`match_entry` struct, reserve, loop, `std::stable_sort`) is copy-pasted verbatim in both `match(needle, vector)` and `match_positions(needle, vector)` in `fzy.cpp`.
- **Risk**: The two implementations can drift (e.g., one changes tie-breaking or filtering and the other does not), breaking the documented contract that both overloads return entries "in the same descending-score order".
- **Impact**: Maintainability; a future edit to one path may silently desynchronize ordering semantics.
- **Mitigation**: Extract a shared private helper (e.g., `rank_matches(needle, haystack) -> std::vector<match_entry>`) used by both overloads.
- **Follow-up test recommendation**: `test_batch_match_positions_consistency` already cross-checks index order between the two overloads; keep it as the drift guard.

### Finding 4: `has_match` return type is `int` but only ever `0` or `1`

- **Evidence**: `fzy.hpp` documents "non-zero if matched"; the wrapper returns the C library's `int` directly, which is `0` or `1`.
- **Risk**: Minor API-clarity issue: callers may treat the value as a score or boolean. No correctness impact since all documented comparisons are against zero.
- **Impact**: Maintainability/readability only.
- **Mitigation**: Consider returning `bool` (or documenting that the value is exactly `0`/`1`). If kept as `int`, tighten the doc comment to state the exact range.
- **Follow-up test recommendation**: Existing tests already assert `== 0` and `!= 0`; no new test needed beyond a compile-time or static assertion if the type changes.

### Finding 5: Embedded NUL bytes truncate patterns/targets (API misuse precondition)

- **Evidence**: The wrapper bridges to a C API via `c_str()`; the library uses `strlen` and `char*` scans. Any embedded `\0` in a `std::string` needle or haystack is invisible to the library.
- **Risk**: If callers ever pass binary-ish or NUL-containing data, matching operates on a truncated prefix with no error.
- **Impact**: Low likelihood (expected inputs are setting keys / identifiers), but a latent correctness hazard.
- **Mitigation**: Document that inputs must not contain embedded NULs; optionally add a debug-mode assertion (`assert(needle.find('\0') == std::string::npos)`).
- **Follow-up test recommendation**: A test documenting current behavior with an embedded NUL (e.g., `"a\0b"` vs `"abc"`) to pin whatever semantics the C API exhibits.

### Residual risks and assumptions not fully analyzed

- **Thread-safety**: The wrapper itself is stateless. Contextual assumption: the library's `match`/`match_positions` use stack-allocated match structs (and `malloc`/`free` internally in `match_positions`) with no visible global mutable state, so concurrent calls from multiple threads appear safe; this was not exhaustively verified against all library code paths.
- **Exception safety**: All wrapper functions are effectively `noexcept` in practice (only `std::vector` allocation can throw); no strong exception-safety guarantees are documented or required by current call sites.
- **Locale behavior**: The library uses `tolower` from `<ctype.h>`; matching is case-insensitive under the C locale. Locale-dependent edge cases (e.g., non-ASCII input) were not analyzed in depth.
- **`SCORE_MAX` exposure**: Equal-length case-insensitive matches return `SCORE_MAX` (`INFINITY`) from the library; the wrapper passes this through. Callers comparing scores with floating-point arithmetic should be aware of the infinite sentinel values.
