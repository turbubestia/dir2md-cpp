# settings_manager (`src/backend/core/settings_manager.hpp` + `src/backend/core/settings_manager.cpp`)

## Scope
This document covers only:
- `SettingsManager` and `SettingSchema` in `src/backend/core/settings_manager.hpp`
- Their implementation in `src/backend/core/settings_manager.cpp`

The direct `.cpp`/`.hpp` counterpart pair was found and analyzed together.

## Purpose and Role
`SettingsManager` is a schema-aware settings store built on Qt types (`QHash<QString, QVariant>`) with JSON persistence.

Primary responsibilities:
- Register per-key metadata (`SettingSchema`)
- Validate values against type/range/enum constraints
- Store active (runtime) values
- Resolve values with schema-default fallback
- Save active values to JSON and load them back
- Emit change and save signals for observers

## Main Types and Responsibilities

### `SettingSchema`
Holds metadata for one setting key:
- Identity and UI metadata: `key`, `title`, `description`, `category`
- Validation metadata: `type`, `defaultValue`, optional `min`/`max`, optional `enumOptions`

Validation path:
- `isValid(const QVariant &val)` checks `val.canConvert(type)` first.
- For `int`/`double` schemas, compares numeric bounds via `toDouble()`.
- If `enumOptions` is non-empty, candidate string value must be contained.

### `SettingsManager`
Internal state:
- `m_values`: active value overrides
- `m_schemaRegistry`: schema metadata by key

Read/write API:
- `get(key)`:
1. Returns active value if present.
2. Else returns schema default when schema exists.
3. Else returns invalid `QVariant`.
- `set(key, value)`:
1. Validates only when schema exists.
2. Stores value and emits `settingChanged` only when value changed.

Schema API:
- `registerSchema(schema)` inserts/overwrites by `schema.key`.
- `schema(key)` returns `std::optional<SettingSchema>`.
- `schemas()` and `activeValues()` expose const references to internal hashes.

Persistence API:
- `save_to_file(filePath)` writes current `m_values` as pretty JSON via `QSaveFile`.
- `load_from_file(filePath)` parses JSON, flattens nested objects, validates per schema, converts type, and repopulates `m_values`.

## Persistence Format and Data Flow

### Save flow
1. Iterate active values (`m_values`).
2. Determine category from schema; default to `"General"`.
3. Split key by `/` and create nested JSON objects.
4. Write full object atomically through `QSaveFile`.
5. Emit `settingsSaved(path)` on successful commit.

Result shape (conceptual):
```json
{
  "Editor": {
    "tab_size": 4
  },
  "General": {
    "feature": {
      "enabled": true
    }
  }
}
```

### Load flow
1. Open and parse JSON document.
2. Flatten nested object keys into slash-delimited paths (`Category/nested/key`).
3. Clear existing `m_values`.
4. For each flattened entry:
- Strip first path segment as category.
- Look up schema by remaining path.
- Validate and convert to schema type.
- Insert and emit `settingChanged`.

## Usage Expectations
Typical usage pattern:
1. Register all schemas early in app startup.
2. Optionally call `load_from_file()`.
3. Read with `get(key)` and write with `set(key, value)`.
4. Subscribe to `settingChanged` and `settingsSaved` as needed.
5. Persist with `save_to_file()`.

Important behavioral note:
- Unknown keys can still be written through `set()` (no schema required), but unknown keys from JSON load are skipped.

## Invariants, Preconditions, and Safety Assumptions
- Keys are treated as slash-delimited paths for persistence nesting.
- `m_values` is intended to contain active runtime overrides; defaults remain in schema.
- Validation guarantees are only as strong as schema registration completeness.
- No explicit thread-synchronization is provided; use from one thread (typically the object's thread).
- Signal handlers are assumed not to violate manager invariants during callbacks.

## Static Analysis and Security

### Finding 1: Unregistered keys are accepted by `set()` but dropped on load
Evidence:
- `set()` validates only when `m_schemaRegistry.contains(key)` is true, otherwise accepts/stores value.
- `load_from_file()` skips entries without matching schema.

Risk:
- Runtime can hold unschematized settings that cannot be restored from disk.

Impact:
- Inconsistent behavior across sessions and silent data loss for unschematized keys.

Mitigation:
- Decide one policy and enforce it consistently:
- Reject `set()` for unknown keys, or
- Allow unknown keys and persist/load them symmetrically.
- Document chosen behavior explicitly.

Follow-up test recommendation:
- Add a test that writes an unknown key via `set()`, saves, reloads, and asserts either explicit rejection or round-trip retention (per intended policy).

### Finding 2: `set()` stores original QVariant without type normalization
Evidence:
- `set()` calls `schema.isValid(value)` but inserts `value` directly into `m_values`.
- `load_from_file()` does convert to `sch->type` before storing.

Risk:
- In-memory type may differ from schema type after `set()` (e.g., `double` accepted for `int` schema via conversion checks).

Impact:
- Type-dependent consumers may observe inconsistent value types depending on whether values were set in-memory or loaded from disk.

Mitigation:
- Normalize in `set()` similarly to load path: convert to schema type before storing and emitting.

Follow-up test recommendation:
- For an `int` schema, call `set(key, 3.0)` and assert stored/emitted type matches exact schema type.

### Finding 3: Destructive clear occurs before full semantic validation
Evidence:
- `load_from_file()` clears `m_values` before finishing per-entry schema lookup/validation.
- Invalid/unknown entries are skipped individually.

Risk:
- A partially invalid file can erase prior valid runtime state and replace it with only a subset.

Impact:
- Configuration loss and hard-to-recover runtime state after malformed or drifted config files.

Mitigation:
- Stage into a temporary map, validate all intended entries first, then commit swap only if load passes policy checks.
- Optionally support partial-load mode explicitly with diagnostics.

Follow-up test recommendation:
- Seed manager with valid values, then load a file containing mixed valid and invalid entries; verify all-or-nothing or documented partial behavior.

### Finding 4: Thread-safety is implicit and unenforced
Evidence:
- `m_values` and `m_schemaRegistry` are plain `QHash` containers with no mutex.
- Public mutators (`set`, `registerSchema`, `load_from_file`) can be called from any thread unless caller enforces affinity.

Risk:
- Concurrent access can cause data races or undefined behavior.

Impact:
- Intermittent crashes, corrupted settings state, or nondeterministic signal sequencing.

Mitigation:
- Enforce single-thread affinity in API contract and assert thread affinity in mutators, or add synchronization strategy.

Follow-up test recommendation:
- Add a stress test invoking mutators from worker threads; expect assertion/guard behavior or verify synchronized correctness.

### Finding 5: JSON path handling permits ambiguous/edge-case keys
Evidence:
- Save/load rely on splitting keys by `/` and manual segment trimming.
- Empty segments and unusual keys are not explicitly rejected.

Risk:
- Keys with leading/trailing/repeated separators can serialize into ambiguous structures or reload unexpectedly.

Impact:
- Hard-to-debug setting collisions, skipped keys, or mismatch between logical key and persisted representation.

Mitigation:
- Validate key format at schema registration and/or set-time (e.g., non-empty segments, no leading slash unless explicitly supported).

Follow-up test recommendation:
- Add tests for keys such as `/a`, `a//b`, and `a/` to verify deterministic accepted/rejected behavior.

## Contextual Assumptions and Limits
- This analysis is source-local to the settings manager pair.
- Behavior of downstream consumers of `settingChanged` and `settingsSaved` is not analyzed here.
- Qt conversion semantics (`QVariant::canConvert/convert`) are assumed per Qt6 behavior.