# SettingsManager

## Purpose

`dir2md::backend::SettingsManager` provides a central registry for application settings with optional schema validation and JSON persistence. It maintains two core stores:

- `m_schemaRegistry`: metadata for known setting keys, including default values, type, numeric ranges, enum options, and UI-related labels.
- `m_values`: currently active user-configured values keyed by flat strings such as `editor/tab_size`.

This manager is responsible for runtime access, mutation, loading from disk, and saving to disk of settings.

## Major types

### `SettingSchema`

`SettingSchema` describes a single setting key and its validation constraints.

Fields:

- `key`: setting identifier, e.g. `editor/tab_size`.
- `title`: human-readable label.
- `description`: explanatory text.
- `category`: top-level JSON category for persistence, e.g. `Editor`.
- `defaultValue`: fallback value when no user value exists.
- `type`: expected `QMetaType` for the setting.
- `min`, `max`: optional numeric bounds.
- `enumOptions`: allowed string values.

Method:

- `isValid(const QVariant &val)`: returns `true` when `val` can convert to `type`, respects numeric bounds for `int`/`double`, and enforces `enumOptions` if populated.

### `SettingsManager`

Public API:

- `get(const QString &key)`: returns the stored value, fallbacks to schema default, or returns an invalid `QVariant` if the key is unknown.
- `set(const QString &key, const QVariant &value)`: validates the value when a schema exists and updates `m_values`; emits `settingChanged` if the stored value changes.
- `activeValues()`: returns a const reference to the current flat value map.
- `registerSchema(const SettingSchema &schema)`: registers schema metadata for a key.
- `schema(const QString &key)`: queries schema metadata with `std::optional` semantics.
- `schemas()`: returns the full schema registry.
- `save_to_file(const QString &filePath)`: writes settings to disk as nested JSON grouped by category.
- `load_from_file(const QString &filePath)`: reads JSON, flattens nested categories, validates loaded values against registered schemas, and populates active values.

Signals:

- `settingChanged(const QString &key, const QVariant &newValue)`.
- `settingsSaved(const QString &path)`.

## Behavior and usage patterns

### Schema registration and lookup

Schemas must be registered before they are useful for validation during `set()` or `load_from_file()`. A schema provides both type semantics and persistence category metadata.

### Getting values

`get()` chooses the stored value first. If no user value exists and schema metadata exists, it returns the schema's `defaultValue`. Unknown keys return an invalid `QVariant`.

### Setting values

`set()` performs schema validation only when a schema is registered for the key. If validation fails, the call returns `false` and the value is not stored. When the value differs from the current stored value, the manager updates `m_values` and emits `settingChanged`.

If no schema exists for a key, `set()` accepts the value unconditionally.

### Persistence format

`save_to_file()` serializes `m_values` into JSON grouped by category. For each stored key:

- the category comes from the registered schema, defaulting to `General` when absent or empty.
- the flat key is split on `/` and nested into JSON objects.
- the result is written atomically using `QSaveFile`.

Example JSON for a key `editor/tab_size` in category `Editor`:

{
  "Editor": {
    "editor": {
      "tab_size": 4
    }
  }
}

`load_from_file()` reads a JSON document, flattens nested objects into entries like `Category/editor/tab_size`, strips the top-level category segment, and validates the remainder against registered schemas. Unknown or invalid keys are ignored.

### State transitions

- `registerSchema()`: updates the schema registry.
- `set()`: may update `m_values` and emit `settingChanged`.
- `save_to_file()`: reads `m_values`, builds nested JSON, writes disk file, emits `settingsSaved`.
- `load_from_file()`: replaces `m_values` with loaded values and emits `settingChanged` for each accepted loaded setting.

## Important implementation details

- `SettingSchema::isValid()` uses `QVariant::canConvert(type)`.
- Numeric range enforcement only occurs for `int` and `double` schemas.
- Enum validation is performed using string conversion and `QStringList::contains()`.
- `save_to_file()` uses nested categories and path components from `/` separators.
- `load_from_file()` ignores unknown keys and invalid values rather than failing the entire load.
- `activeValues()` exposes a const reference to internal storage, meaning callers see the actual manager map without copying.
- `load_from_file()` clears `m_values` before inserting new values.
- `QJsonDocument::fromJson()` treats parse errors and empty content as failure.

## Invariants and preconditions

- Registered schema keys should match the flat setting keys used in `set()` and `get()`.
- A schema should be registered before `load_from_file()` if its values are to be accepted from disk.
- `save_to_file()` expects `m_values` to contain values convertible by `QJsonValue::fromVariant()`.
- JSON input is expected to be an object root; empty or invalid root is treated as load failure.
- Category names are derived from schema metadata; an empty category string becomes `General`.

## Contextual dependencies

- This implementation depends on Qt types: `QObject`, `QHash`, `QVariant`, `QJsonObject`, `QJsonDocument`, `QSaveFile`, and `QFile`.
- Behavior depends on `QVariant` conversion semantics and `QJsonValue::fromVariant()` support for stored types.
- No external thread-safety guarantees are provided; the class is effectively single-threaded unless callers synchronize access.

## Static Analysis and Security

- Evidence: `SettingsManager::set()` only validates values when a schema is registered. Keys without schemas are accepted unconditionally.
  - Risk: invalid or unexpected value types may be stored for unschematized keys.
  - Impact: callers can introduce inconsistent runtime state and break assumptions elsewhere in the app.
  - Mitigation: require schema registration before accepting a value, or clearly document schema-free behavior.
  - Test recommendation: verify that `set()` accepts arbitrary `QVariant` values for unknown keys and that `get()` returns them unchanged.

- Evidence: `load_from_file()` silently skips unknown keys without reporting them and only logs warnings for invalid values.
  - Risk: malformed or misnamed configuration entries are ignored, causing the application to continue with defaults or stale values.
  - Impact: users may not realize settings were dropped, and debug sessions may miss root causes.
  - Mitigation: return a richer load result, report skipped keys, or fail on unknown keys when strict loading is desired.
  - Test recommendation: load JSON containing unknown, invalid, and valid values and assert only valid schema-backed settings are populated.

- Evidence: `save_to_file()` uses `QJsonValue::fromVariant(value)` for arbitrary `QVariant` values.
  - Risk: unsupported QVariant types may convert to null or produce unexpected JSON output.
  - Impact: persisted settings may lose data or corrupt the saved file format.
  - Mitigation: restrict persisted value types, or normalize values before serialization.
  - Test recommendation: save values of unsupported QVariant types and verify the resulting JSON behavior.

- Evidence: `load_from_file()` emits `settingChanged()` while populating values.
  - Risk: observers may see partially loaded state or trigger dependent updates during load.
  - Impact: initialization order bugs or inconsistent reactions in connected listeners.
  - Mitigation: batch load without emitting signals until completion, or document that listeners may observe incremental load.
  - Test recommendation: register a slot for `settingChanged()` and ensure it is called for each accepted loaded setting during `load_from_file()`.

- Evidence: `load_from_file()` returns false for an empty JSON object root, even though `{}` is valid JSON.
  - Risk: empty configuration files are rejected as invalid load operations.
  - Impact: a fresh or intentionally blank config file may fail to load unexpectedly.
  - Mitigation: allow an empty object root as a successful empty load.
  - Test recommendation: load an empty JSON object and confirm whether it succeeds or fails according to intended behavior.
