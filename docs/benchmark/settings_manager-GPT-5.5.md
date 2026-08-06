# settings_manager

## Source Pair

- Header: `src/backend/core/settings_manager.hpp`
- Implementation: `src/backend/core/settings_manager.cpp`
- Counterpart status: matching `.cpp` and `.hpp` files were both found.

## Purpose and Role

`SettingsManager` is a QtCore backend component for storing application settings as flat string keys and `QVariant` values. It supports optional schema registration for metadata, default values, type validation, numeric bounds, and enumerated string options. The manager can persist active values to a JSON file and load schema-recognized values back from JSON.

The implementation is backend-only: it uses QtCore types and `QObject` signals, with no QWidget or QML dependencies.

## Main Types and Responsibilities

### `SettingSchema`

`SettingSchema` describes one setting key. Its fields provide:

- `key`: the canonical flat key, such as `editor/tab_size`.
- `title`, `description`, and `category`: metadata for display or grouping.
- `defaultValue`: fallback returned by `SettingsManager::get` when no active value exists.
- `type`: expected Qt metatype for validation and load-time conversion.
- `min` and `max`: optional numeric constraints for `int` and `double` schemas.
- `enumOptions`: optional accepted string values.

`SettingSchema::isValid` first checks `QVariant::canConvert(type)`. For `int` and `double` schemas, it compares `val.toDouble()` against any configured `min` and `max`. If `enumOptions` is not empty, it accepts only values whose `toString()` is present in the option list.

### `SettingsManager`

`SettingsManager` owns two in-memory hash tables:

- `m_values`: active setting values explicitly set or loaded.
- `m_schemaRegistry`: registered schemas keyed by schema key.

The public API provides value access, mutation, schema registration and lookup, and JSON save/load operations. It emits `settingChanged` when `set` changes a stored value and for each valid value loaded from disk. It emits `settingsSaved` after a successful save commit.

## Behavior and Data Flow

### Getting and Setting Values

`get(key)` returns values in this order:

1. The active value from `m_values`, when present.
2. The schema default from `m_schemaRegistry`, when a schema is registered.
3. An invalid `QVariant` for unknown keys.

`set(key, value)` validates against the registered schema when one exists. If no schema is registered for the key, the value is accepted without validation. If the new value differs from the currently stored active value, it is inserted into `m_values` and `settingChanged(key, value)` is emitted. Re-setting the same value returns `true` without emitting.

### Schema Registration

`registerSchema(schema)` inserts or replaces the schema under `schema.key`. Replacement does not revalidate or normalize any existing active value for the same key.

`schema(key)` returns a copied `SettingSchema` in `std::optional` form. `schemas()` exposes the schema registry as a const reference.

### Saving JSON

`save_to_file(filePath)` serializes only active values from `m_values`; schema defaults are not saved unless they were explicitly set or loaded into active values.

For each active value:

1. The JSON category is taken from the registered schema's trimmed `category` if present and non-empty; otherwise it defaults to `General`.
2. The flat key is split on `/`, preserving empty parts.
3. Leading empty path parts are removed, so a key such as `/a/b` is saved as `a/b` under the selected category.
4. `insertNestedValue` recursively creates nested JSON objects under the category.

The final `QJsonDocument` is written with indentation through `QSaveFile`. A successful `commit()` emits `settingsSaved(filePath)` and returns `true`; open, write, or commit failures return `false`.

### Loading JSON

`load_from_file(filePath)` reads and parses a JSON document. Missing or unreadable files return `false`. Null or empty parse results return `false` after logging a warning. An empty root object also returns `false`.

For non-empty root objects, `flattenJsonObject` recursively converts nested JSON into flat slash-delimited keys. Each flattened key is interpreted as `category/path/to/schema_key`; the first path component is removed as the category. The remaining path is joined and looked up in the schema registry.

Only values with registered schemas are loaded. Unknown keys are skipped. Values failing `SettingSchema::isValid` are skipped with a warning. Valid values are converted to the schema metatype when `canConvert` reports true, inserted into `m_values`, and emitted through `settingChanged`.

Before applying loaded values, the implementation clears all existing active values.

## Usage Patterns

A typical caller registers schemas before loading persisted settings, then reads settings through `get`:

```cpp
SettingsManager manager;
manager.registerSchema({
    .key = "editor/tab_size",
    .title = "Tab Size",
    .description = "Number of spaces per tab",
    .category = "Editor",
    .defaultValue = 4,
    .type = QMetaType::fromType<int>(),
    .min = 1,
    .max = 16,
});

manager.load_from_file(path);
QVariant tab_size = manager.get("editor/tab_size");
```

Callers that need persistence should use schema-backed keys. Unregistered keys can be stored with `set` during the current process, but the load path will skip them later because it only accepts schema-recognized values.

## Invariants and Preconditions

- Schema keys are expected to be stable slash-delimited strings.
- Persistence round-trips depend on schemas being registered before `load_from_file` is called.
- Categories are storage grouping metadata; they are not part of the schema key after loading.
- `m_values` contains only active overrides, not the full effective settings set including defaults.
- `activeValues()` and `schemas()` return const references that remain valid only while the manager object and the corresponding internal hash remain alive and unmodified.
- The class has QObject thread affinity but no explicit synchronization; concurrent access from multiple threads is not protected by this implementation.

## Error Handling and Signals

Most API errors are represented as boolean return values. `save_to_file` logs write-related failures with `qWarning`; `load_from_file` logs parse failures and invalid loaded values, but silently treats missing files, empty roots, and unknown keys as non-loaded settings.

`settingChanged` is emitted when `set` changes one key and once per valid loaded key during `load_from_file`. Clearing previous active values during load does not emit removal notifications for keys that disappear from the new file.

## Static Analysis and Security

### Finding: `load_from_file` clears existing state before knowing whether any usable settings will be loaded

- Evidence: after parsing and flattening the root object, `load_from_file` calls `m_values.clear()` before iterating loaded entries. Unknown keys are skipped, invalid values are skipped, and the function still returns `true` after the loop.
- Risk: a readable JSON file with only unknown or invalid settings can erase all active in-memory overrides while reporting success.
- Impact: callers may treat the load as successful even though the effective active configuration was reset. This can cause surprising runtime behavior and makes failed migrations or malformed configuration harder to diagnose.
- Mitigation: validate and stage accepted values in a temporary hash first, then replace `m_values` only when the result satisfies the intended success criteria. Alternatively, document that a successful load means the file was processed, not that any value was accepted.
- Follow-up test recommendation: start with an active valid setting, load a non-empty JSON file containing only unknown or invalid keys, and assert the expected state-retention or state-clear contract explicitly.

### Finding: removed settings during load do not emit change notifications

- Evidence: `load_from_file` clears `m_values`, then emits `settingChanged` only for inserted valid loaded values. It does not emit for keys that existed before the clear but are absent from the loaded file.
- Risk: observers can retain stale UI or cached state for settings that were removed by a load operation.
- Impact: frontend or CLI code connected to `settingChanged` may not be able to distinguish a full reload from per-key updates, which can cause inconsistent effective settings views.
- Mitigation: emit a batch reload signal, emit changes for removed keys, or document that callers must refresh all settings after `load_from_file` returns `true`.
- Follow-up test recommendation: connect a signal spy, prepopulate two settings, load a file with only one of them, and assert the notification contract for the missing key.

### Finding: schema validation relies on permissive `QVariant` conversions

- Evidence: `SettingSchema::isValid` accepts any value for which `val.canConvert(type)` returns true. Numeric bounds are checked through `val.toDouble()`, and `set` stores the original `QVariant` without converting it to the schema type.
- Risk: values that Qt considers convertible may pass validation while preserving a different active variant type when set directly. For numeric settings, conversion behavior can be surprising for string or floating-point inputs, depending on Qt's conversion rules.
- Impact: callers of `activeValues()` or code that serializes active values can observe non-canonical types for schema-backed settings set through `set`, while loaded settings are converted to the schema type.
- Mitigation: convert into a temporary `QVariant`, check conversion success according to the Qt API available in the project, validate the converted value, and store the canonical converted value for schema-backed keys.
- Follow-up test recommendation: set an `int` schema key with string and floating-point variants and assert both acceptance/rejection and the stored active `QVariant` type.

### Finding: partial writes are not distinguished from complete writes

- Evidence: `save_to_file` checks `saveFile.write(jsonBytes) < 0` but does not compare the returned byte count with `jsonBytes.size()`.
- Risk: a short positive write would proceed to `commit()` even though not all JSON bytes were written.
- Impact: the persisted settings file could be truncated or malformed while `save_to_file` may still report success if the underlying device accepts the commit.
- Mitigation: require `write(jsonBytes) == jsonBytes.size()` before committing, and log the expected and actual byte counts on mismatch.
- Follow-up test recommendation: use a controlled failing or short-writing `QIODevice` abstraction if the save logic is refactored for dependency injection; otherwise add coverage around write failure behavior that can be simulated with invalid paths or permissions.

### Finding: unregistered keys can be saved but cannot be loaded back

- Evidence: `set` accepts keys without registered schemas. `save_to_file` writes every active value, defaulting the category to `General` when no schema exists. `load_from_file` strips the category and skips any key for which `schema(schemaKey)` has no value.
- Risk: callers may believe unregistered settings are persisted because saving succeeds and the JSON contains them, but those values are discarded on load.
- Impact: this is a correctness and API-contract hazard for any caller that uses `SettingsManager` as a general key-value store instead of a schema-backed settings store.
- Mitigation: either reject unregistered keys in `set`, add an explicit unschematized load policy, or document that persistence requires prior schema registration.
- Follow-up test recommendation: set an unregistered key, save, load into a fresh manager without a schema, and assert the documented behavior.

### Finding: no explicit synchronization for shared mutable state

- Evidence: `m_values` and `m_schemaRegistry` are mutable `QHash` members accessed without locks. Methods expose const references to both hashes.
- Risk: concurrent reads and writes from different threads can race if callers use the manager outside normal QObject thread-affinity discipline.
- Impact: data races can cause inconsistent reads or undefined behavior in multithreaded use.
- Mitigation: document single-threaded QObject-affinity usage, or add synchronization and avoid exposing direct references if cross-thread access is required.
- Follow-up test recommendation: no simple deterministic unit test is recommended without introducing a defined threading contract; instead, document the thread-affinity contract and keep tests single-threaded unless synchronization is added.

## Residual Assumptions and Limits

This analysis is based only on `settings_manager.hpp` and `settings_manager.cpp`. It does not inspect callers, tests, build configuration, or existing markdown documentation. Risks involving expected UI behavior, configuration file trust boundaries, and project-wide threading practices are therefore limited to what is visible from this source pair.
