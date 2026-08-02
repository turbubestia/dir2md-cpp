# Settings Manager Static Analysis Benchmark

## Method

Only findings under each report's `Static Analysis and Security` section were counted. A normalized issue is **common** when at least two models reported it and **unique** when exactly one model reported it. The requested `Unit` column records unique findings.

Accuracy was judged against `settings_manager.hpp`, `settings_manager.cpp`, the tests, and the locked persistence requirements. In particular, full replacement during load, silent skipping of unknown keys, and skipping invalid values with a warning are intentional behavior; reports receive less credit when they present those requirements as defects without identifying an additional consistency or observability problem.

## Finding Counts

| Model | Total | Common | Unit |
|---|---:|---:|---:|
| Claude-Haiku-4.5 | 7 | 7 | 0 |
| Claude-Opus-5 | 8 | 7 | 1 |
| Claude-Sonnet-5 | 5 | 4 | 1 |
| Gemini-3.1-Pro | 2 | 2 | 0 |
| Gemini-3.6-Flash | 4 | 4 | 0 |
| GPT-5.3-Codex | 5 | 5 | 0 |
| GPT-5.4 | 6 | 5 | 1 |
| GPT-5.5 | 6 | 6 | 0 |
| GPT-5.6-Luna | 5 | 5 | 0 |
| GPT-5.6-Terra | 5 | 5 | 0 |
| Grok-4.5 | 10 | 9 | 1 |
| Kimi-K2.7-Code | 9 | 8 | 1 |
| MAI-Code-1-Flash | 2 | 2 | 0 |
| Qwen3.6-35B-A3B | 6 | 6 | 0 |
| Raptor-mini | 5 | 3 | 2 |

## Findings by Model

- Model: **Claude-Haiku-4.5**
  - Finding 1 (common): Prefix-colliding keys can overwrite scalar values during nested JSON insertion.
  - Finding 2 (common): Unconditional category stripping makes loading dependent on the exact category-first file layout.
  - Finding 3 (common): Schema default values are registered without validation.
  - Finding 4 (common): `load_from_file()` ignores the result of `QVariant::convert()`.
  - Finding 5 (common): Unknown keys are skipped during load without diagnostics.
  - Finding 6 (common): Shared hashes have no synchronization or enforced thread-affinity contract.
  - Finding 7 (common): Enum validation compares `toString()` output without type-aware normalization.

- Model: **Claude-Opus-5**
  - Finding 1 (common): `canConvert()` does not prove a particular value converts successfully, and numeric bounds cover too few numeric types.
  - Finding 2 (common): Replacement loading clears prior values before entry validation and does not notify observers about removed keys.
  - Finding 3 (common): The dead branch in `insertNestedValue()` permits nondeterministic data loss for prefix-colliding keys.
  - Finding 4 (common): Unregistered values bypass validation and cannot be restored consistently from disk.
  - Finding 5 (common): Empty and repeated key path components are accepted and serialize ambiguously.
  - Finding 6 (common): Caller-provided file paths are used without policy validation or restriction.
  - Finding 7 (common): Internal hash references and unsynchronized mutation create lifetime and cross-thread hazards.
  - Finding 8 (unique): Minor hazards are bundled around detach behavior, short writes, text mode, parse diagnostics, recursion, and unchecked defaults.

- Model: **Claude-Sonnet-5**
  - Finding 1 (common): The no-op conditional in `insertNestedValue()` masks scalar overwrite on prefix collisions.
  - Finding 2 (common): A valid empty JSON object written by save is rejected by load.
  - Finding 3 (unique): The public API has no operation to remove an override and restore schema-default lookup.
  - Finding 4 (common): Unregistered keys accept unvalidated and potentially unbounded values.
  - Finding 5 (common): Concurrent access is neither synchronized nor explicitly constrained.

- Model: **Gemini-3.1-Pro**
  - Finding 1 (common): Nested insertion can discard a scalar when setting paths overlap.
  - Finding 2 (common): Numeric validation through `double` can introduce precision or boundary errors.

- Model: **Gemini-3.6-Flash**
  - Finding 1 (common): A semantically unusable but valid JSON file can replace all active settings with an empty or partial set.
  - Finding 2 (common): Identical nested-insertion branches permit scalar nodes to be overwritten.
  - Finding 3 (common): Min/max constraints are ignored for numeric metatypes other than exactly `int` and `double`.
  - Finding 4 (common): Loading always removes a top-level segment and therefore rejects non-category file layouts.

- Model: **GPT-5.3-Codex**
  - Finding 1 (common): Unregistered keys are accepted and saved but dropped during load.
  - Finding 2 (common): `set()` validates but stores the original `QVariant` instead of normalizing it to the schema type.
  - Finding 3 (common): Replacement loading commits the clear before semantic validation finishes.
  - Finding 4 (common): Thread-affinity and synchronization requirements are implicit and unenforced.
  - Finding 5 (common): Empty, leading, trailing, and repeated path separators are not validated.

- Model: **GPT-5.4**
  - Finding 1 (common): Existing values are discarded before the incoming entries are semantically validated.
  - Finding 2 (common): Unregistered runtime keys do not round-trip through persistence.
  - Finding 3 (common): The boolean load result cannot distinguish a complete load from one where entries were skipped.
  - Finding 4 (common): `QVariant` conversion semantics permit lossy or unsuccessful value conversions.
  - Finding 5 (common): Empty and malformed path segments have weakly defined persistence behavior.
  - Finding 6 (unique): JSON arrays are accepted as terminal variants without schema-specific structural validation.

- Model: **GPT-5.5**
  - Finding 1 (common): Replacement loading can clear active state even when no usable input values are accepted.
  - Finding 2 (common): Values removed by replacement loading do not emit change notifications.
  - Finding 3 (common): Permissive conversion validates values without storing a canonical schema type.
  - Finding 4 (common): The write check does not explicitly reject a positive short write before commit.
  - Finding 5 (common): Unregistered keys can be saved but cannot be loaded back.
  - Finding 6 (common): Mutable state has no explicit synchronization contract.

- Model: **GPT-5.6-Luna**
  - Finding 1 (common): Loading discards current state before knowing whether incoming entries are usable.
  - Finding 2 (common): Broad conversion acceptance can produce lossy values and inconsistent runtime types.
  - Finding 3 (common): Unknown keys bypass validation and later fail to restore from persistence.
  - Finding 4 (common): Category, key syntax, and prefix-collision assumptions are not validated.
  - Finding 5 (common): Const references expose internal container lifetime and invalidation hazards.

- Model: **GPT-5.6-Terra**
  - Finding 1 (common): `set()` stores a convertible value without normalizing it to the declared schema type.
  - Finding 2 (common): Replacement loading provides no notification for values that disappear.
  - Finding 3 (common): Duplicate relative keys under different categories collapse to one active key nondeterministically.
  - Finding 4 (common): Leading and empty path components are lossy during round-trip serialization.
  - Finding 5 (common): Concurrent access to the manager is unsafe without external coordination.

- Model: **Grok-4.5**
  - Finding 1 (common): Unregistered and category-sensitive key paths do not have pure round-trip behavior.
  - Finding 2 (common): Replacement loading clears overrides before all entries are accepted.
  - Finding 3 (common): Removed effective values produce no observer notification.
  - Finding 4 (common): Conversion, numeric-range, and enum checks are coarse and type-permissive.
  - Finding 5 (common): The redundant nested-insertion branch can overwrite an intermediate scalar.
  - Finding 6 (common): Empty path segments and unconditional category stripping create edge-case key loss.
  - Finding 7 (common): Accessors expose references to containers that mutation can invalidate.
  - Finding 8 (common): File locations are accepted without an application-level path policy.
  - Finding 9 (common): Thread safety and signal re-entrancy are not constrained.
  - Finding 10 (unique): Re-registering a schema does not revalidate an already stored value.

- Model: **Kimi-K2.7-Code**
  - Finding 1 (common): Unregistered keys are lost on a save/load round trip.
  - Finding 2 (common): The dead nested-insertion branch can clobber non-object values.
  - Finding 3 (common): `load_from_file()` ignores conversion failure.
  - Finding 4 (common): Empty and duplicate path segments produce ambiguous keys.
  - Finding 5 (common): Loading clears active values before validating incoming entries.
  - Finding 6 (common): Category mismatch is not detected during loading.
  - Finding 7 (common): Enum validation uses string comparison regardless of declared type.
  - Finding 8 (unique): Unbounded `readAll()` permits excessive memory use for a very large settings file.
  - Finding 9 (common): The value and schema hashes are not safe for concurrent mutation.

- Model: **MAI-Code-1-Flash**
  - Finding 1 (common): Load can report success while intentionally dropping unknown or invalid settings.
  - Finding 2 (common): Round-trip behavior depends on a strict, unvalidated category and slash-path convention.

- Model: **Qwen3.6-35B-A3B**
  - Finding 1 (common): `insertNestedValue()` contains identical branches and can overwrite a scalar on path collision.
  - Finding 2 (common): The report claims open, parse, or empty-root failures occur after `m_values` is cleared.
  - Finding 3 (common): Unknown and invalid entries are dropped while load still returns success.
  - Finding 4 (common): Category and key reconstruction are fragile across schema or file-layout changes.
  - Finding 5 (common): The manager has no thread-safety guarantee.
  - Finding 6 (common): Numeric type coercion during load can alter values.

- Model: **Raptor-mini**
  - Finding 1 (common): `set()` accepts arbitrary values for keys without schemas.
  - Finding 2 (common): Unknown loaded keys are skipped without a structured diagnostic result.
  - Finding 3 (unique): Arbitrary `QVariant` values may serialize to null or unexpected JSON.
  - Finding 4 (unique): Per-key signals emitted during load let observers react to partially populated state.
  - Finding 5 (common): A valid empty JSON object is rejected as a failed load.

## Leaderboard

| Rank | Model | Findings | Accuracy | Tie-break analysis quality |
|---:|---|---:|---|---|
| 1 | Grok-4.5 | 10 | High | Excellent |
| 2 | Kimi-K2.7-Code | 9 | High | Very good |
| 3 | Claude-Opus-5 | 8 | High, with minor overclaims | Excellent |
| 4 | Claude-Haiku-4.5 | 7 | High | Very good |
| 5 | GPT-5.5 | 6 | High, except short-write severity | Excellent |
| 6 | GPT-5.4 | 6 | Medium-high | Very good |
| 7 | GPT-5.3-Codex | 5 | High | Very good |
| 8 | GPT-5.6-Terra | 5 | High | Very good |
| 9 | GPT-5.6-Luna | 5 | Medium-high | Very good |
| 10 | Claude-Sonnet-5 | 5 | Medium-high | Very good |
| 11 | Gemini-3.6-Flash | 4 | Medium-high | Very good |
| 12 | Qwen3.6-35B-A3B | 6 | Medium-low; one material factual error | Good |
| 13 | Raptor-mini | 5 | Medium-low | Limited |
| 14 | Gemini-3.1-Pro | 2 | Medium; one finding is overstated | Limited |
| 15 | MAI-Code-1-Flash | 2 | Low; mostly intended behavior | Limited |

The strongest reports combined broad coverage with concrete code evidence and distinguished actual data-loss or consistency defects from documented policy. Qwen's count is discounted because `m_values.clear()` occurs only after successful open, parse, and non-empty-root checks, contrary to its second finding. Gemini-3.1-Pro also overstates 64-bit precision risk for the exact `int` branch implemented here, while MAI-Code-1-Flash primarily restates intentional load behavior.

## Finding Traceability Audits

These audits distinguish findings that map to actionable defects from intended behavior in the locked persistence requirements, ordinary API constraints, and conditional hardening opportunities.

### Finding Classification

Finding numbers are listed directly rather than counted. `-` means that no finding from that model belongs to the category. Each finding appears in exactly one classification column. The table covers all 15 models; detailed traceability remains limited to the four models audited below.

| Model | Total Findings | Actionable Bug | Intended Design | Hardening | Invalid |
|---|---:|---|---|---|---|
| Claude-Haiku-4.5 | 7 | 1, 3, 4 | 5, 6, 7 | - | 2 |
| Claude-Opus-5 | 8 | 1, 2, 3, 5 | 4, 7 | 6, 8 | - |
| Claude-Sonnet-5 | 5 | 1, 2 | 3, 5 | 4 | - |
| Gemini-3.1-Pro | 2 | 1 | - | - | 2 |
| Gemini-3.6-Flash | 4 | 2, 3 | 1, 4 | - | - |
| GPT-5.3-Codex | 5 | 2, 5 | 1, 3, 4 | - | - |
| GPT-5.4 | 6 | 4, 5 | 1, 2, 3 | 6 | - |
| GPT-5.5 | 6 | 2, 3 | 1, 5, 6 | 4 | - |
| GPT-5.6-Luna | 5 | 2, 4 | 1, 3, 5 | - | - |
| GPT-5.6-Terra | 5 | 1, 2, 4 | 5 | 3 | - |
| Grok-4.5 | 10 | 3, 4, 5, 6, 10 | 1, 2, 7, 9 | 8 | - |
| Kimi-K2.7-Code | 9 | 2, 3 | 1, 5, 6, 7, 9 | 4, 8 | - |
| MAI-Code-1-Flash | 2 | - | 1, 2 | - | - |
| Qwen3.6-35B-A3B | 6 | 1, 6 | 3, 5 | 4 | 2 |
| Raptor-mini | 5 | 5 | 1, 2 | 3, 4 | - |

### Grok-4.5

| # | Verdict | Code trace and assessment |
|---:|---|---|
| 1 | Mostly intended | `set()` and `save_to_file()` accept unregistered keys, while `load_from_file()` skips them. The asymmetry is real, but skipping unknown loaded keys is explicitly required. A different category does not block loading because the first path component is discarded. |
| 2 | Intended behavior | `m_values.clear()` implements the required full-replacement load policy; invalid values must be skipped and unknown keys silently ignored. |
| 3 | Actionable consistency bug | `load_from_file()` clears old values but emits `settingChanged` only for inserted values, so observers can retain stale state for removed overrides. |
| 4 | Actionable validation bug | `SettingSchema::isValid()` accepts broadly convertible values, while `set()` stores the original `QVariant`; runtime values can therefore differ from the declared schema type or be converted lossily. |
| 5 | Actionable data-loss bug | `insertNestedValue()` cannot represent both a scalar and children at one path. Prefix-colliding keys such as `a` and `a/b` overwrite one another, with the survivor dependent on `QHash` iteration order. |
| 6 | Actionable edge-case bug | Empty or leading path components are accepted, but leading empty components are removed and an empty resulting path is not written, so key identity is not preserved. |
| 7 | API caveat | Returning const references to internal containers is conventional but requires callers not to retain them past mutation or object destruction. |
| 8 | Conditional security concern | Raw paths are intentionally accepted by the low-level API. This becomes a vulnerability only when a caller supplies attacker-controlled paths without a higher-level policy. |
| 9 | Out of scope | The locked requirements explicitly state that thread safety is not required. Re-entrancy remains a usage concern but no concrete failure is demonstrated. |
| 10 | Actionable invariant bug | `registerSchema()` replaces metadata without revalidating an existing override, so `get()` can return a value that violates the current schema. |

**Result:** Five findings identify actionable defects (3, 4, 5, 6, and 10), two are conditional concerns (7 and 8), and three are intended behavior or out of scope (1, 2, and 9).

### Kimi-K2.7-Code

| # | Verdict | Code trace and assessment |
|---:|---|---|
| 1 | Intended behavior | Unregistered keys are saved under `General` and skipped on load, but silent skipping of unknown loaded keys is explicitly required. The API asymmetry is surprising rather than a contract violation. |
| 2 | Actionable data-loss bug | The identical branches in `insertNestedValue()` expose a real prefix-collision failure: a scalar node is replaced by an object when keys such as `a` and `a/b` coexist. |
| 3 | Actionable validation bug | `load_from_file()` ignores the boolean result of `QVariant::convert()` and inserts the variant regardless, so failed conversion can violate the schema type invariant. |
| 4 | Partially actionable | Empty and leading path components can be lossy and should be rejected. Kimi overstates duplicate and trailing components because JSON supports empty property names and many such paths round-trip unchanged. |
| 5 | Intended behavior | Clearing active values before applying accepted entries follows the required full-replacement policy, including skipping invalid and unknown entries. |
| 6 | Design choice | Categories are persistence and presentation grouping, not part of setting identity; the requirements do not require category matching during load. |
| 7 | Overlapping concern | `enumOptions` is a `QStringList`, so comparison through a string representation is consistent with its declared storage. Type conversion remains coarse, but that is principally covered by finding 3 and `set()` normalization. |
| 8 | Conditional hardening | `QFile::readAll()` can consume excessive memory for a very large file. A size limit is prudent when settings files cross a trust boundary, but this is not an unconditional application bug. |
| 9 | Out of scope | Thread safety is explicitly excluded by the locked requirements; callers are expected to respect the manager's `QObject` thread affinity. |

**Result:** Two findings identify definite actionable bugs (2 and 3), two provide useful hardening or edge-case work (4 and 8), and five are intended behavior, design choices, overlapping concerns, or out of scope (1, 5, 6, 7, and 9).

### Claude-Haiku-4.5

| # | Verdict | Code trace and assessment |
|---:|---|---|
| 1 | Actionable data-loss bug | `insertNestedValue()` converts an existing scalar node to an empty object when another key needs that node as a parent. Prefix-colliding keys such as `display/theme` and `display/theme/dark_mode` therefore cannot both be serialized, and `QHash` iteration order determines which value survives. |
| 2 | Inaccurate or design-level concern | The loader intentionally removes the top-level category. A schema key such as `tab_size` saved under `Editor` correctly becomes `Editor/tab_size` on disk and maps back to `tab_size`; it does not require the schema key to contain the category. A mismatched on-disk category still loads because category is not part of setting identity under the locked format. |
| 3 | Actionable invariant bug | `registerSchema()` stores `defaultValue` without calling `isValid()`, and `get()` returns that default directly. An unset setting can therefore return a value that violates its own declared type, range, or enum constraint. |
| 4 | Actionable validation bug | `load_from_file()` calls `QVariant::convert()` but ignores its boolean result and inserts the variant regardless. Because `canConvert()` only establishes that a conversion path exists, value-level conversion failure can violate the schema type invariant. |
| 5 | Intended behavior | Silently skipping unknown loaded keys is explicitly required by the locked persistence contract. Optional diagnostics could improve usability but are not a bug fix. |
| 6 | Out of scope | The locked requirements state that thread safety is not required. The manager should follow normal `QObject` thread-affinity usage rather than adding internal synchronization absent a changed contract. |
| 7 | Design choice | `enumOptions` is explicitly a `QStringList`, so comparing against `val.toString()` implements string-representation enum semantics. Numeric or native enum support would require a different schema contract; Haiku does not demonstrate incorrect behavior under the current one. |

**Result:** Three findings identify actionable bugs (1, 3, and 4); finding 2 contains an incorrect category-key claim; and findings 5, 6, and 7 are intentional behavior, out of scope, or schema design choices.

### GPT-5.6-Terra

| # | Verdict | Code trace and assessment |
|---:|---|---|
| 1 | Actionable type-invariant bug | `SettingSchema::isValid()` accepts values that can convert to the schema type, but `set()` stores and emits the original `QVariant`. The same setting can therefore expose different runtime types depending on whether it was set directly or loaded from JSON. Conversion must be attempted, checked, validated, and stored canonically. |
| 2 | Actionable observer-consistency bug | `load_from_file()` clears all overrides but emits `settingChanged` only for accepted replacements. A key omitted from the file or skipped during validation can fall back to its default without observers receiving any notification, leaving cached UI or backend state stale. |
| 3 | Actionable robustness bug | A valid JSON document can contain the same relative schema key under multiple top-level categories. After category stripping, both entries target one `m_values` key and `QHash` iteration order determines the winner. Normal save output cannot create this ambiguity, but the loader should reject duplicate category-free keys or validate each key's expected category. |
| 4 | Actionable key-validation bug | Save removes leading empty path components, so keys such as `/editor/tab_size` are persisted as `editor/tab_size`; a key made only of separators is not written at all. Because `set()` and `registerSchema()` accept these keys, persistence does not preserve their identity. |
| 5 | Out of scope | Concurrent access would race, but the locked requirements explicitly state that thread safety is not required. Normal `QObject` thread-affinity rules are the intended usage contract. |

**Result:** Two findings identify direct runtime consistency bugs (1 and 2), two identify actionable persistence robustness defects (3 and 4), and one is explicitly out of scope (5).