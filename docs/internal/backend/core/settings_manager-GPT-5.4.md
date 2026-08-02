# SettingsManager

## Scope

This document is based only on the following source pair:

- `src/backend/core/settings_manager.hpp`
- `src/backend/core/settings_manager.cpp`

The direct counterpart was found, so this analysis covers both the declaration and implementation.

## Purpose and Role

`dir2md::backend::SettingsManager` is a Qt-based settings store for backend configuration data. It combines:

- a schema registry that describes valid keys, expected types, default values, and optional constraints
- a runtime value store for currently active user-provided values
- JSON persistence for saving and restoring active settings
- Qt signals so callers can observe changes and successful saves

The design keeps defaults in schema metadata and stores only explicit runtime values in `m_values`.

## Main Types and Responsibilities

### `SettingSchema`

`SettingSchema` describes one logical setting key.

- `key`: flat slash-delimited identifier such as `editor/tab_size`
- `title` and `description`: descriptive metadata for callers or UI layers
- `category`: top-level JSON grouping used during persistence
- `defaultValue`: fallback returned by `get()` when no active value exists
- `type`: required `QMetaType` for validation and conversion
- `min` and `max`: optional numeric bounds
- `enumOptions`: optional whitelist enforced through string comparison

`isValid(const QVariant &val)` applies three checks:

1. `val` must be convertible to the schema `type`.
2. For schemas whose `type` is `int` or `double`, the numeric value must satisfy `min` and `max` when those bounds are set.
3. If `enumOptions` is not empty, `val.toString()` must be one of the allowed options.

### `SettingsManager`

`SettingsManager` owns two in-memory registries:

- `m_values`: `QHash<QString, QVariant>` containing explicit active values
- `m_schemaRegistry`: `QHash<QString, SettingSchema>` containing registered schema metadata

Public responsibilities:

- `get(key)`: resolve active value first, then schema default, otherwise return an invalid `QVariant`
- `set(key, value)`: validate against schema when present, update stored value if it changed, and emit `settingChanged`
- `registerSchema(schema)`: insert or replace schema metadata by key
- `schema(key)`: expose an optional copy of one schema
- `schemas()` and `activeValues()`: expose const references to internal registries
- `save_to_file(filePath)`: serialize current active values into hierarchical JSON and write atomically
- `load_from_file(filePath)`: read JSON, flatten nested objects into slash-delimited keys, validate against schema, convert values, and repopulate `m_values`

Signals:

- `settingChanged(const QString &key, const QVariant &newValue)`
- `settingsSaved(const QString &path)`

## Behavior and Data Flow

### Value lookup and mutation

`get()` uses a strict fallback order:

1. stored runtime value in `m_values`
2. registered schema default in `m_schemaRegistry`
3. invalid `QVariant` for unknown keys

`set()` validates only when a schema exists for the key. If validation fails, the method returns `false` and leaves state unchanged. If the value is accepted and differs from the currently stored one, `m_values` is updated and `settingChanged` is emitted.

If no schema exists for a key, `set()` accepts the value without validation.

### Save flow

`save_to_file()` iterates over `m_values` and builds a nested `QJsonObject`.

For each stored entry:

1. Resolve the top-level category from the schema if available.
2. Fall back to `"General"` when no schema exists or the schema category is empty after trimming.
3. Split the flat key on `/`.
4. Remove leading empty path segments.
5. Recursively build nested JSON objects under the selected category through `insertNestedValue()`.

The final JSON document is pretty-printed and written through `QSaveFile`, so the write is intended to be atomic. On success, `settingsSaved(filePath)` is emitted.

### Load flow

`load_from_file()` reads a JSON file into a document, rejects unreadable files, rejects null or empty parse results, and rejects an empty root object.

If the root object is non-empty, the method:

1. Flattens nested JSON objects into slash-delimited keys such as `Category/editor/tab_size`.
2. Clears the current `m_values` map.
3. For each flattened key, strips leading empty segments and then removes the first segment as the category prefix.
4. Joins the remaining segments into the schema key.
5. Looks up the schema for that key.
6. Skips unknown keys.
7. Validates known values through `SettingSchema::isValid()`.
8. Converts the variant to the schema `QMetaType` when possible.
9. Inserts the converted value into `m_values` and emits `settingChanged`.

Successful loading therefore depends on schema registration matching the persisted key layout.

## Persistence Format Assumptions

The serializer and loader both assume slash-delimited keys and a separate top-level category object. A key such as `editor/tab_size` with category `Editor` is written conceptually as:

```json
{
  "Editor": {
    "editor": {
      "tab_size": 4
    }
  }
}
```

During load, the first path segment is always treated as the category and removed before schema lookup. That means the schema registry must expect keys without the category prefix.

## Ownership, Lifetime, and Safety Assumptions

- All stored state is held by value in Qt containers and `QVariant`.
- `SettingsManager` lifetime follows normal `QObject` parent ownership rules.
- `activeValues()` and `schemas()` return const references to internal storage, so callers must not retain those references past manager lifetime.
- No synchronization is present; the class should be treated as single-thread-affine unless a higher layer provides external coordination.
- Error reporting is boolean plus selective `qWarning()` logging. Callers do not receive structured diagnostics about partial load failures.

## Invariants and Preconditions

- Defaults are not copied into `m_values`; they remain in schema metadata and are surfaced only through `get()`.
- Schema validation occurs only for registered keys.
- Numeric bounds in `SettingSchema::isValid()` are applied only when the schema type is exactly `int` or `double`.
- Save and load behavior assumes a stable contract between schema keys, slash-delimited persistence paths, and category stripping.
- `load_from_file()` emits `settingChanged` for every accepted loaded value after clearing previous state.

## Static Analysis and Security

### Finding 1: Existing runtime settings are discarded before the incoming file is fully validated

- Evidence: `load_from_file()` clears `m_values` immediately after flattening the JSON, before it has confirmed that any individual entry maps to a known schema or passes validation.
- Risk: A file that is syntactically valid JSON but semantically unusable for the current schema set can erase previously active settings and still return success.
- Impact: This can cause silent configuration loss and make recovery difficult when schema names change or the file contains only stale keys.
- Mitigation: Stage accepted entries in a temporary map and replace `m_values` only after the load pass succeeds according to a defined policy. If partial loads are acceptable, return richer status information that exposes dropped entries.
- Follow-up test recommendation: Load a valid file containing only unknown or invalid keys into a manager that already has active values and verify whether existing settings are preserved or intentionally replaced.

### Finding 2: Unregistered keys are accepted at runtime but dropped during deserialization

- Evidence: `set()` stores a value even when no schema exists, but `load_from_file()` skips any key whose schema lookup fails.
- Risk: A caller can believe an ad hoc setting is persisted because saving succeeds, but the same value will not be restored on the next load.
- Impact: Behavior differs across sessions and can produce hard-to-diagnose data loss for keys that were never registered.
- Mitigation: Either require registration for `set()`, or explicitly support schema-less round-tripping by preserving unknown keys during load.
- Follow-up test recommendation: Save a manager containing one registered key and one unregistered key, then reload into a fresh manager and assert the documented behavior for the unregistered entry.

### Finding 3: Loader success does not distinguish complete loads from partial loads

- Evidence: `load_from_file()` returns `true` once parsing succeeds and the root object is non-empty, even if some or most entries are skipped as unknown or invalid.
- Risk: Callers cannot tell whether the loaded configuration was complete, partially applied, or mostly discarded.
- Impact: Applications may continue with incomplete configuration while assuming a full restore occurred.
- Mitigation: Return structured status, accumulate warnings for skipped keys, or expose a callback/report describing rejected entries.
- Follow-up test recommendation: Load a file mixing valid, unknown, and invalid entries and assert that the API communicates partial acceptance explicitly.

### Finding 4: Numeric validation relies on permissive QVariant conversion semantics

- Evidence: `SettingSchema::isValid()` first checks `canConvert(type)` and then, for `int` and `double`, validates bounds using `toDouble()`. This accepts whatever Qt considers convertible rather than requiring the original variant type to already match the schema.
- Risk: String or loosely typed inputs may be accepted unexpectedly, and conversion corner cases can differ from caller expectations.
- Impact: Configuration validation becomes dependent on Qt conversion rules instead of a stricter schema contract, which can make UI, CLI, and persisted input behave inconsistently.
- Mitigation: Decide whether the API should accept coercion or exact types. If exact types are intended, validate `val.metaType()` directly or constrain accepted source types more explicitly.
- Follow-up test recommendation: Exercise integer and double schemas with string inputs, boolean inputs, and fractional values to lock down the intended acceptance rules.

### Finding 5: Empty or malformed path segments can produce weakly defined persistence behavior

- Evidence: Save and load both strip leading empty segments, but neither path registration nor serialization rejects empty keys, trailing separators, or repeated separators inside a key.
- Risk: Keys such as `/editor/tab_size`, `editor//tab_size`, or an empty string may serialize into surprising shapes and may not round-trip cleanly.
- Impact: The persistence contract is fragile and can drift if callers do not normalize keys consistently.
- Mitigation: Validate schema keys and `set()` keys up front, rejecting empty keys and malformed separators before they reach persistence.
- Follow-up test recommendation: Add round-trip cases for keys with leading, trailing, and doubled separators and verify the intended normalization or rejection policy.

### Finding 6: Array values are flattened as terminal variants with no schema-specific structural validation

- Evidence: `flattenJsonObject()` recurses only into JSON objects. Non-object values, including arrays, are inserted directly as variants and later validated only through the target `QMetaType` and optional enum or numeric checks.
- Risk: Complex JSON values can enter the load path without structural validation beyond QVariant conversion.
- Impact: This is not necessarily incorrect, but it shifts responsibility for collection-type safety entirely to schema type registration and caller discipline.
- Mitigation: Document whether array and structured non-object values are supported. If not, reject them explicitly during load.
- Follow-up test recommendation: Load array-valued entries against both supported and unsupported schema types to verify the intended behavior.

No obvious memory ownership defects are visible in this source pair. The main risks are configuration consistency, silent partial acceptance, and dependence on key-format and type-conversion conventions.