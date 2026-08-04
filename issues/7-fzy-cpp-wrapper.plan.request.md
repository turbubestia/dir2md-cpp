# Make a fzy C++ Wrapper Class in Backend

We want to make a C++ wrapper of the package `fzy` in `src/backend/core/fzy.hpp/.cpp` such that we can use `std::string` and `std::string_view` instead of the C types const char*.

## Goals

- Implement a C++ wrapper in the namespace `dir2md::backend::fzy` to access the two match methods using C++ STL classes like `std::string` and `std::string_view`.
- Add a method to match a `std::vector<std::string_view>` and return a `std::vector<size_t>` with the index of the only matched strings in the input vector sorted by their score.
- Add another method tha return a `std::vector<std::vector<size_t>>` with the match positions. Note in the `match_positions()` method, the `size_t *positions` must be allocated with at least query.length() elements (the length of the needle or query string). So for a given query, and a vector of target string, we will have a vector of arrays with the matched position of each character in the needle.

## Notes

- `score_t match_positions(const char *needle, const char *haystack, size_t *positions);`
Needle is the search key, haystack if the string to match into, and position is an array of length(needle) to store the index of the matching positions.

## Examples

Here is a concrete input/output example for both goals using a sample dataset.

---

### **Sample Input**

* **Query (Needle):** `"edfont"` *(length: 6)*
* **Target Strings Vector:**
* `[0]` `"files.autoSave"`
* `[1]` `"terminal.integrated.fontSize"`
* `[2]` `"editor.fontSize"`

---

### **Goal 2: Matched String Indices (Sorted by Score)**

**Output:** `std::vector<size_t>`

```text
[2, 1]

```

**Explanation:**

* Target **`[2]`** (`"editor.fontSize"`) matches and scores highest (boundary bonus for `editor` and `.fontSize`).
* Target **`[1]`** (`"terminal.integrated.fontSize"`) matches, but receives a lower score.
* Target **`[0]`** (`"files.autoSave"`) does not contain all characters in sequence, so it is filtered out.

---

### **Goal 3: Character Match Positions (Ordered by Match)**

**Output:** `std::vector<std::vector<size_t>>`

```text
[
  [0, 1, 7, 8, 9, 10],     // Positions for target [2] ("editor.fontSize")
  [17, 18, 20, 21, 22, 23]  // Positions for target [1] ("terminal.integrated.fontSize")
]

```

**Position Breakdown:**

1. **For Target `[2]` (`"editor.fontSize"`):**
* `'e'` $\rightarrow$ Index `0`
* `'d'` $\rightarrow$ Index `1`
* `'f'` $\rightarrow$ Index `7`
* `'o'` $\rightarrow$ Index `8`
* `'n'` $\rightarrow$ Index `9`
* `'t'` $\rightarrow$ Index `10`


2. **For Target `[1]` (`"terminal.integrated.fontSize"`):**
* `'e'` $\rightarrow$ Index `17` (the `e` in `integrated`)
* `'d'` $\rightarrow$ Index `18` (the `d` in `integrated`)
* `'f'` $\rightarrow$ Index `20` (the `f` in `fontSize`)
* `'o'` $\rightarrow$ Index `21`
* `'n'` $\rightarrow$ Index `22`
* `'t'` $\rightarrow$ Index `23`

---
# Refinement Iteration 1
**Status:** PENDING USER FEEDBACK

## 1. Executive Summary
Add a backend-only C++ wrapper around the existing `fzy` C API in the `dir2md::backend::fzy` namespace. The wrapper will expose score and match-position operations using `std::string` and `std::string_view`, plus batch operations that return matching target indices ordered by score and the corresponding character positions.

## 2. Refined Requirements & Acceptance Criteria
- **Requirement FZY-001: C++ wrapper boundary**
  - **Description:** Provide the wrapper in `src/backend/core/fzy.hpp` and `src/backend/core/fzy.cpp` under the `dir2md::backend::fzy` namespace. The public interface must use C++ standard-library string types rather than requiring callers to pass C string pointers.
  - **Acceptance Criteria:**
    - [ ] Given a `std::string` or `std::string_view` query and target, when the wrapper score operation is called, then it returns the score produced by the underlying `fzy` implementation.
    - [ ] Given a query and target, when the wrapper position operation is called, then it returns the matching character offsets in query order.
    - [ ] The backend target builds and links the wrapper without exposing Qt Widgets or frontend dependencies.

- **Requirement FZY-002: Batch matching and ranking**
  - **Description:** Match one query against a vector of target string views and return the original indices of targets that match, ordered from highest score to lowest score.
  - **Acceptance Criteria:**
    - [ ] Given the sample query and targets, then the returned indices are `[2, 1]`.
    - [ ] Targets that do not match all query characters are excluded from the returned indices.
    - [ ] Returned indices refer to the original input order, including when target strings are duplicated.
    - [ ] The input vector and its referenced strings are not modified.

- **Requirement FZY-003: Batch match positions**
  - **Description:** Return the match positions for the matching targets in the same score-sorted order as the batch matching operation. Each result contains one position for every character in the query.
  - **Acceptance Criteria:**
    - [ ] Given the sample query and targets, then the returned position arrays are `[[0, 1, 7, 8, 9, 10], [17, 18, 20, 21, 22, 23]]`.
    - [ ] Every position is an offset into its corresponding original target string.
    - [ ] Unmatched targets do not produce position arrays.
    - [ ] Position storage supports queries of arbitrary length, including queries longer than a target.

- **Requirement FZY-004: Result ordering and consistency**
  - **Description:** Use the score returned by `fzy` to rank matching targets, and apply one deterministic tie-breaking rule consistently for all batch operations.
  - **Acceptance Criteria:**
    - [ ] Matching results are sorted by descending score.
    - [ ] The matching-indices and matching-positions operations return results in identical target order for the same inputs.
    - [ ] Equal-score results follow the documented tie-breaking rule.

- **Requirement FZY-005: Boundary and lifetime behavior**
  - **Description:** Define behavior for empty queries, empty target collections, empty targets, non-matching queries, and `std::string_view` inputs whose storage is owned by the caller.
  - **Acceptance Criteria:**
    - [ ] Empty input collections return empty result collections without invoking undefined behavior.
    - [ ] The wrapper does not retain any `std::string_view` beyond the duration of the operation.
    - [ ] Empty-query behavior is documented and covered by tests.
    - [ ] No-match behavior is documented and covered by tests.

- **Requirement FZY-006: Verification**
  - **Description:** Add focused backend tests covering direct wrapper operations and both batch operations.
  - **Acceptance Criteria:**
    - [ ] Tests cover the supplied sample, no matches, empty inputs, duplicate targets, and equal-score ordering.
    - [ ] Tests verify that every returned position belongs to the target selected at the corresponding result index.
    - [ ] The existing debug CMake configure, build, and test workflow succeeds.

## 3. Scope & Constraints
- **In-Scope:**
    - A backend C++ wrapper for the two relevant `fzy` operations.
    - Single-target score and position access using C++ string types.
    - Batch matching over `std::vector<std::string_view>` with score ordering.
    - Batch position results aligned with the matching order.
    - Backend build integration and focused unit tests.
- **Out-of-Scope:**
    - Changes to the upstream `fzy` algorithm or its C source.
    - Frontend, CLI, or user-interface integration beyond making the backend wrapper buildable.
    - Unicode-aware matching or a new ranking algorithm unless required by the existing `fzy` API.
- **Technical Constraints / Edge Cases:**
    - Position storage must have capacity for at least the query length before calling the underlying position function.
    - `std::string_view` inputs are non-owning; no result may depend on their storage after the call returns.
    - The wrapper must preserve the existing `fzy` matching and scoring semantics, including case sensitivity behavior.
    - Result ordering must be deterministic when scores are equal.

## 4. Open Design Choices (Questions for User)
- **[Technical]:** What exact public method names and overloads should the wrapper expose for single-target score, single-target positions, batch matching, and batch positions?
**User: The basic ones would be:**
`auto has_match(std::string_view needle, std::string_view haystack) -> int;`
`auto match_positions(std::string_view needle, std::string_view haystack) -> std::pair<score_t, std::vector<size_t>>;`
`auto match(std::string_view needle, std::string_view haystack) -> score_t;`

overloads

return the index of the only the matched vector sorted by score
`auto match(std::string_view needle, std::vector<std::string_view> haystack) -> std::vector<size_t>;`

return the index of the only matched and positions of the input vector, where thy will be sorted by score.
`auto match_positions(std::string_view needle, std::vector<std::string_view> haystack) -> std::vector<std::pair<size_t,std::vector<size_t>>>;`

- **[Business Logic]:** What should an empty query return: a successful match for every target, no matches, or a defined special result? What should single-target positions return when there is no match?
**User: an empty needle does not match anything. In the search if needle is empty we will not run the search match.**

- **[Business Logic]:** When two targets have equal scores, should results retain original input order, or use another tie-breaker?
**user: yes, retain order.**

- **[Technical]:** Should batch position results be returned independently, as shown in the example, or should the API also expose the original target indices alongside each position array to prevent ambiguity for callers?
**User: each `std::pair<size_t,std::vector<size_t>>` will have the index of the matched input and the vector of matched characters.**

---
# Refinement Iteration 2
**Status:** LOCKED

## 1. Executive Summary
Define the public C++ wrapper API for the backend `fzy` integration using the requested single-target and batch overloads. Matching results are ranked by descending `fzy` score, equal scores retain input order, batch position results include the original target index, and an empty needle matches nothing.

## 2. Refined Requirements & Acceptance Criteria
- **Requirement FZY-007: Public wrapper API**
  - **Description:** Expose the wrapper in the `dir2md::backend::fzy` namespace through `src/backend/core/fzy.hpp` and `src/backend/core/fzy.cpp`. The public operations shall use `std::string_view` for query and target strings and shall provide single-target match status, single-target score, single-target score with character positions, batch matching, and batch matching with positions.
  - **Acceptance Criteria:**
    - [ ] The wrapper provides `has_match` for one query and one target and returns an integer match status.
    - [ ] The wrapper provides `match` for one query and one target and returns `score_t`.
    - [ ] The wrapper provides `match_positions` for one query and one target and returns `std::pair<score_t, std::vector<size_t>>`.
    - [ ] The wrapper provides `match` for one query and `std::vector<std::string_view>` targets and returns the original indices of matching targets.
    - [ ] The wrapper provides `match_positions` for one query and `std::vector<std::string_view>` targets and returns pairs containing each original matching-target index and its position vector.

- **Requirement FZY-008: Empty and non-matching queries**
  - **Description:** An empty needle does not match any target. Search callers are expected to skip the matching operation when the needle is empty, and the wrapper shall document and test its direct-call behavior for that input. Non-matching targets shall be excluded from batch results.
  - **Acceptance Criteria:**
    - [ ] Given an empty needle, `has_match` reports no match.
    - [ ] Given an empty needle and any target collection, both batch operations return empty result collections.
    - [ ] Given a non-matching needle and target, the direct operations report no match according to the documented no-match return convention.
    - [ ] Given a collection containing matching and non-matching targets, only matching targets appear in either batch result.

- **Requirement FZY-009: Score ordering and original indices**
  - **Description:** Batch operations shall order matching targets by descending score returned by the underlying `fzy` implementation. Equal scores shall retain the original input order. Every batch result index shall refer to the original target vector, including duplicate target strings.
  - **Acceptance Criteria:**
    - [ ] The supplied sample returns matching indices `[2, 1]`.
    - [ ] Equal-score targets are returned in ascending original-index order.
    - [ ] The index order from batch `match` is identical to the index order in batch `match_positions`.
    - [ ] Duplicate target values remain distinguishable by their original indices.

- **Requirement FZY-010: Position result alignment**
  - **Description:** For every matched target, the position vector shall contain one target offset for each character in the needle, in needle order. Batch position results shall preserve the same score order as batch matching and shall include the original target index with each vector.
  - **Acceptance Criteria:**
    - [ ] The supplied sample returns positions `[[0, 1, 7, 8, 9, 10], [17, 18, 20, 21, 22, 23]]` associated with original indices `2` and `1`, respectively.
    - [ ] Every returned position is within the corresponding original target and identifies the character matched by the corresponding needle character.
    - [ ] Position storage is sufficient for the complete needle before the underlying C API is called, including when the needle is longer than the target.
    - [ ] No position vector is returned for an unmatched target.

## 3. Scope & Constraints
- **In-Scope:**
  - The requested `dir2md::backend::fzy` C++ wrapper API and backend implementation.
  - Single-target operations using `std::string_view` and STL result types.
  - Batch score filtering, stable score ordering, original-index preservation, and aligned match positions.
  - Focused backend tests for the sample, empty needle, no matches, empty collections, duplicate targets, equal scores, and position alignment.
- **Out-of-Scope:**
  - Changes to the upstream `fzy` algorithm or its C source.
  - Frontend, CLI, or UI integration.
  - Unicode-aware matching or a replacement ranking algorithm.
- **Technical Constraints / Edge Cases:**
  - The wrapper must not retain any caller-owned `std::string_view` after an operation returns.
  - Empty target collections return empty result collections.
  - Empty targets are handled without undefined behavior and match only if permitted by the explicitly documented needle rule; an empty needle never matches.
  - The wrapper preserves the existing `fzy` score and case-sensitivity semantics.

## 4. Open Design Choices (Questions for User)
- **[Technical]:** For a non-empty needle that does not match a single target, what exact result should `match_positions` return: the underlying `fzy` no-match score with an empty position vector, or another sentinel/result convention? The same convention should be used consistently by `match` and `has_match`.
**User: the minimum score and empty vector. the overload will return empty vectors.**

**locked**
