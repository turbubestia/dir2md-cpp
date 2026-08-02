# Settings Manager

## Source Pair

- Header: `src/backend/core/settings_manager.hpp`
- Implementation: `src/backend/core/settings_manager.cpp`

Both counterparts were found and analyzed.

## Purpose and Responsibilities

`dir2md::backend::SettingsManager` is a `QObject`-based, in-memory settings store. It keeps registered `SettingSchema` metadata separately from active values, validates schema-backed writes, emits change notifications, and serializes active values to JSON.

`SettingSchema` describes a setting key, display metadata, a default value, the expected `QMetaType`, optional numeric bounds, and optional string enumeration values.

## Public Behavior

### Schema registration and retrieval

`registerSchema()` inserts or replaces a schema using `schema.key` as the hash key. `schema()` returns a copy wrapped in `std::optional`; unknown keys produce `std::nullopt`. `schemas()` exposes a const reference to the full schema registry.

Registering a schema does not populate `m_values` with its default. `get()` returns an active value first, then the registered default, then an invalid `QVariant` for an unknown key.

### Value writes and validation

`set()` validates a value only when its key has a registered schema. Unknown keys are accepted. A rejected schema-backed write returns `false` without changing the active store. A successful write inserts into `m_values` and emits `settingChanged` only when the stored `QVariant` differs from the supplied one.

`SettingSchema::isValid()` first requires `val.canConvert(type)`. For `int` and `double` schemas it compares `val.toDouble()` with valid `min` and `max` bounds. For schemas with non-empty `enumOptions`, it requires `val.toString()` to be one of those options.

`activeValues()` returns a const reference to the active-value hash. Consumers must not retain that reference beyond the manager's lifetime and should treat it as invalidated by subsequent non-const manager operations.

### JSON persistence

`save_to_file()` writes only active values, not defaults or schema metadata. It groups each active value below its schema's trimmed `category`, defaulting to `General`, then splits the setting key on `/` to create nested JSON objects. Leading slash components are removed. The JSON is indented and committed through `QSaveFile`; a successful commit emits `settingsSaved`.

`load_from_file()` reads a JSON object, flattens all nested object paths, removes the first path component as a category, and considers the remainder to be the schema key. It accepts only registered keys whose values pass `isValid()`, converts accepted values to the schema type, inserts them into `m_values`, and emits `settingChanged` for each accepted entry. Unknown and invalid entries are skipped. Missing or unreadable files, parse failures, and an empty root object return `false`.

## State and Lifecycle Assumptions

- The manager owns both `QHash` instances directly; no dynamic ownership is exposed by this pair.
- The optional parent follows normal Qt `QObject` ownership rules.
- The implementation has no synchronization. Reads and mutations, including save/load, must be externally serialized and used consistently with the `QObject` thread-affinity model.
- `load_from_file()` clears active values only after it has opened, parsed, and flattened a non-empty JSON object. It does not restore defaults into `m_values`; defaults remain a `get()` fallback.
- Schema registration and value persistence assume schema keys are globally unique regardless of category.

## Usage Pattern

Register schemas before loading or setting values. Use the same schema keys and categories when writing and reading a file. Call `get()` to obtain configured values with default fallback, and connect `settingChanged` or `settingsSaved` where observers need updates.

```cpp
SettingsManager settings;
settings.registerSchema({"editor/tab_size", "Tab Size", {}, "Editor", 4,
                         QMetaType::fromType<int>(), 1, 16, {}});

settings.set("editor/tab_size", 2);
const QVariant tab_size = settings.get("editor/tab_size");
settings.save_to_file("settings.json");
```

## Static Analysis and Security

### Schema type is validated but not normalized by `set()`

- Evidence: `SettingSchema::isValid()` accepts any `QVariant` for which `canConvert(type)` is true. `SettingsManager::set()` stores the original `value` directly, whereas `load_from_file()` explicitly converts accepted values to `sch->type` before storing them.
- Risk: An active value inserted through `set()` can have a different runtime type than the schema declares. For numeric schemas, a convertible string is range-checked through `toDouble()` but the string itself is retained. Conversion semantics can also permit unexpected text-to-number coercion.
- Impact: Callers that rely on the schema type after `get()` or through `activeValues()` can receive inconsistent types depending on whether data came from an API write or a JSON load. This can produce incorrect settings behavior or downstream conversion failures.
- Mitigation: Convert a candidate to the schema type before semantic validation and store the converted value, rejecting unsuccessful conversion. Define whether textual numeric inputs are supported and validate their representation deliberately.
- Follow-up test recommendation: Set an integer schema with a convertible string and verify either rejection or storage as an `int`; compare this with the behavior after saving and loading the same value.

### Loading removals is not observable through `settingChanged`

- Evidence: `load_from_file()` calls `m_values.clear()` after parsing but emits `settingChanged` only for accepted values that it subsequently inserts. No signal identifies values removed by the clear operation.
- Risk: Observers can retain a view of settings that no longer exist after a successful load, especially for keys absent from the input file or whose input values were skipped.
- Impact: UI or dependent backend state can become stale despite the manager's active state having changed.
- Mitigation: Define load replacement semantics in the API and emit explicit removal/change notifications, or expose a dedicated reload signal that consumers must treat as invalidating all cached settings.
- Follow-up test recommendation: Set two values, load a valid file containing only one, and assert the observer receives an event that lets it detect the removed setting.

### Category stripping can merge distinct persisted paths

- Evidence: Save output places values below `SettingSchema::category`, but loading flattens every root object and unconditionally removes its first component before looking up the schema key. It then inserts by that category-free key into `m_values`.
- Risk: JSON containing the same relative key under multiple categories maps those entries to the same active key. Hash iteration order determines which accepted value is stored. A category rename also does not prevent old persisted values from targeting the same schema key.
- Impact: Settings files from different versions or manually edited files can load nondeterministically and silently select an unintended value.
- Mitigation: Preserve category as part of the persistent identity, reject duplicate category-free schema keys during load, or ensure that only the schema's expected category is accepted for each key.
- Follow-up test recommendation: Load a document containing `A/editor/tab_size` and `B/editor/tab_size` with differing valid values and assert that the loader rejects the ambiguity or applies a documented deterministic policy.

### Leading separators and empty key components are lossy

- Evidence: Save removes all leading empty components after splitting a key on `/`; an empty resulting path makes `insertNestedValue()` return without writing a value. Load uses the category-free flattened path rather than recovering the original leading separators.
- Risk: Keys beginning with `/`, and particularly keys consisting only of separators, do not round-trip exactly.
- Impact: Such keys may disappear from persisted settings or fail schema lookup after loading.
- Mitigation: Constrain schema keys to non-empty, normalized relative paths at registration, or encode keys without structural reinterpretation.
- Follow-up test recommendation: Register and save keys such as `/editor/tab_size` and `/`, then verify the chosen normalization or explicit rejection policy.

### Concurrent access is unsafe without external coordination

- Evidence: The class mutates and reads `QHash` members from ordinary methods with no mutex, thread assertions, or queued-operation boundary.
- Risk: Simultaneous access from multiple threads can race during `set()`, schema registration, save, or load.
- Impact: Data races can corrupt observable state, produce inconsistent files, or cause undefined behavior in callers.
- Mitigation: Document single-thread ownership, assert the owning thread where appropriate, or protect state with synchronization and define signal delivery semantics.
- Follow-up test recommendation: Exercise documented cross-thread access only after selecting a synchronization contract; otherwise add a thread-affinity misuse test or assertion.

## Residual Assumptions

This analysis is limited to the requested source pair. It assumes standard Qt behavior for `QVariant`, `QJsonDocument`, `QSaveFile`, `QObject`, and `QHash`; their implementations and callers were not analyzed. File permissions, path trust, and broader application policy for user-supplied settings files are outside this pair.