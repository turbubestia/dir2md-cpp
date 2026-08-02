# SettingsManager

## Purpose and role

The `SettingsManager` class is a QObject-backed settings container for backend configuration values. It stores active values in a flat hash keyed by slash-delimited strings (e.g., `"editor/tab_size"`) and uses registered `SettingSchema` instances to validate values, provide defaults, and describe metadata such as categories, numeric ranges, and enum constraints.

The companion `SettingSchema` struct holds per-key metadata: key name, title, description, category, default value, Qt meta type, and optional min/max/enum constraints. Its `isValid` method checks convertibility, numeric bounds, and enum membership.

## Major classes, functions, data structures, and responsibilities

### SettingSchema
- Holds metadata for a single setting key.
- `isValid(const QVariant&) const` — validates a candidate value against the schema's type, min/max range, and enum options.

### SettingsManager (QObject subclass)
- **Accessors**
  - `get(key)` — returns the user-configured value if present; falls back to the schema default; returns an invalid `QVariant` for unknown keys.
  - `set(key, value)` — validates against the registered schema (if any), stores the value only when it differs from the current one, and emits `settingChanged`. Returns `false` on validation failure.
- **Schema management**
  - `registerSchema(schema)` — inserts a schema into the registry.
  - `schema(key)` / `schemas()` — query individual or all registered schemas.
- **Persistence**
  - `save_to_file(filePath)` — groups values by category (from schema, defaulting to `"General"`), reconstructs nested JSON from slash-delimited keys, and writes atomically via `QSaveFile`. Emits `settingsSaved` on success.
  - `load_from_file(filePath)` — reads and parses JSON, flattens nested objects into slash-delimited keys, strips the leading category segment, validates each entry against the schema, type-coerces values, clears existing state, populates `m_values`, and emits `settingChanged` for every loaded key.

### Internal helpers (anonymous namespace)
- `insertNestedValue(QJsonObject&, QStringList, QVariant)` — recursively builds a nested JSON object from a list of path segments.
- `flattenJsonObject(const QJsonObject&, QString, QHash<QString,QVariant>&)` — recursively flattens a nested JSON object into a flat hash with slash-delimited keys.

## Public or expected usage patterns

1. **Registration** — Call `registerSchema` for each setting key before any get/set operations.
2. **Read/write** — Use `get` and `set` for runtime access. `set` validates against the schema and emits a signal on change.
3. **Persistence** — Call `save_to_file` to serialize all active values; call `load_from_file` to restore them from disk.

## Important control flow, state transitions, algorithms, or data flow

- **Save path**: Values are iterated in insertion order (QHash). For each value, the category is resolved from the schema (defaulting to `"General"`). The key is split on `"/"` into path segments, and `insertNestedValue` builds a nested JSON object under that category. The final structure is `{ "Category": { "nested": { ... } } }`.
- **Load path**: The root JSON object is flattened into flat keys like `"Category/nested/key"`. Leading empty segments (from leading `/`) are stripped, then the first segment is removed as the category prefix. The remaining path is joined back to form the schema key. Each entry is validated against the registered schema; unknown or invalid entries are skipped. Valid entries are type-coerced and inserted into `m_values`, with `settingChanged` emitted for each.
- **Default resolution**: `get` checks `m_values` first, then falls back to `schema.defaultValue`. Unknown keys return an invalid `QVariant`.

## Ownership, lifetime, nullability, thread-safety, and exception-safety assumptions

- **Ownership** — All data is owned by value inside `QHash` members. No raw pointers or heap-allocated objects are shared externally.
- **Lifetime** — The class follows standard QObject parent-child semantics. Schemas are copied into the registry; values are stored by value in `QVariant`.
- **Nullability** — Keys are `QString`; an empty key would be a valid hash key but has no documented meaning. Values default to invalid `QVariant` when absent.
- **Thread-safety** — Not thread-safe. The class relies on QObject's signal/slot mechanism, which is safe only within a single-threaded event loop or with explicit connection types. No mutexes or atomic operations are used.
- **Exception-safety** — Qt containers and `QVariant` do not throw on normal operations, but exception safety is not explicitly guaranteed.

## Input validation and error-handling behavior

- `set` rejects values that fail schema validation (type mismatch, out-of-range, or wrong enum option) by returning `false` without modifying state.
- `save_to_file` returns `false` on file open failure, write failure, or commit failure. Warnings are logged via `qWarning`.
- `load_from_file` returns `false` when the file cannot be opened, JSON parsing fails, or the root object is empty. Unknown keys are silently skipped; invalid values produce a `qWarning` but do not cause the method to fail. **Critically, `m_values` is cleared before loading**, so a partial or failed load can result in total data loss of previously loaded settings.

## Important invariants and preconditions

- A setting key must be registered via `registerSchema` before it can be meaningfully loaded from file; otherwise the key is treated as unknown and dropped.
- The persistence format assumes slash-delimited keys where the first segment represents the category. This convention is enforced by the serialization/deserialization logic but not validated at registration time.
- `get` returns a schema default only when no explicit value has been set in `m_values`. Once a value matching the default is explicitly set via `set`, it occupies an entry in `m_values` and shadows the schema default.

## Contextual dependencies

- Relies on Qt's JSON infrastructure (`QJsonObject`, `QJsonDocument`, `QSaveFile`) for persistence.
- Depends on `QMetaType` registration for the types stored in schemas; `isValid` uses `val.canConvert(type)` which requires the target type to be known to Qt's meta-type system.

## Missing counterpart note

Both `.hpp` and `.cpp` files are present, so this analysis covers the complete source pair.

## Static Analysis and Security

### 1. Dead code in `insertNestedValue` — identical branches

- **Evidence**: In `settings_manager.cpp`, the function `insertNestedValue` contains:
  ```cpp
  if (!obj[first].isObject()) {
      nested = obj[first].toObject();
  } else {
      nested = obj[first].toObject();
  }
  ```
  Both branches execute the exact same statement. The conditional is dead code.
- **Risk**: While functionally harmless (calling `toObject()` on a non-object returns an empty object), this indicates unclear intent and makes future refactoring error-prone. A developer might assume the branches differ and introduce divergent logic, creating a bug.
- **Impact**: Maintainability risk; potential for subtle bugs if someone "fixes" one branch to do something different without understanding the original intent.
- **Mitigation**: Remove the redundant conditional. Since `toObject()` safely returns an empty object when called on a non-object, a single assignment is sufficient: `nested = obj[first].toObject();`.
- **Follow-up test recommendation**: Add a unit test that saves and reloads a setting with a nested key path to verify the round-trip produces correct JSON structure.

### 2. State loss in `load_from_file` when load fails after clearing

- **Evidence**: In `settings_manager.cpp`, `m_values.clear()` is called before iterating over loaded entries. If the file cannot be opened, parsing fails, or the root object is empty, the method returns `false` — but `m_values` has already been cleared.
- **Risk**: A failed load operation destroys all previously loaded settings, leaving the application in an empty state with no indication of what went wrong beyond a boolean return value.
- **Impact**: Correctness and stability risk. Users may lose their configuration on every failed load attempt (e.g., corrupted file, permission change, disk error).
- **Mitigation**: Defer `m_values.clear()` until after successful parsing and validation. Alternatively, use a temporary buffer to accumulate loaded values and only swap into `m_values` upon full success.
- **Follow-up test recommendation**: Test that loading a malformed or empty JSON file does not clear previously set values.

### 3. Silent dropping of unknown and invalid entries during load

- **Evidence**: In `load_from_file`, entries with no matching schema are skipped with `continue`. Entries failing `isValid` produce a `qWarning` but also use `continue`. The method returns `true` regardless.
- **Risk**: A settings file containing forward-incompatible keys (from a newer version) or corrupted values is silently accepted, giving the caller no way to detect data loss.
- **Impact**: Maintainability and correctness risk. Schema evolution or file corruption goes undetected, making debugging difficult.
- **Mitigation**: Return a richer result type (e.g., a struct with success flag plus counts of skipped/invalid entries), or preserve unknown keys in a separate bucket rather than dropping them entirely.
- **Follow-up test recommendation**: Load a file containing one valid entry, one key with no registered schema, and one entry with an invalid value; verify the outcome exposes the dropped entries.

### 4. Category/key round-trip fragility

- **Evidence**: `save_to_file` splits keys on `"/"` to build nested JSON under category objects. `load_from_file` flattens the JSON and strips the first path segment as the category prefix. The category is resolved from the schema during save (defaulting to `"General"`) but reconstructed from the file structure during load.
- **Risk**: If a key's schema category differs from what was written, or if the file format changes, the round-trip will produce incorrect keys. Keys without a registered schema default to `"General"` on save but may not round-trip correctly if loaded before schemas are registered.
- **Impact**: Correctness risk during schema evolution or when loading settings before full schema registration.
- **Mitigation**: Validate key format at registration time, document the serialization contract explicitly, and consider storing category metadata in the file format itself rather than relying on path structure alone.
- **Follow-up test recommendation**: Register schemas with specific categories, save, reload, and verify all keys match their original values.

### 5. No thread-safety guarantees

- **Evidence**: The class uses `QHash` members without any synchronization primitives. Signals are emitted from `set` and persistence methods without specifying connection types.
- **Risk**: Concurrent access from multiple threads (e.g., a background worker loading settings while the UI thread modifies them) can cause data races, corrupted hashes, or missed signals.
- **Impact**: Stability risk in multi-threaded applications. Data corruption or undefined behavior under concurrent access.
- **Mitigation**: Document the single-threaded assumption explicitly. If cross-thread access is needed, add mutex protection or use `QMutexLocker` around shared state mutations. Alternatively, restrict all access to the thread owning the QObject's event loop.
- **Follow-up test recommendation**: No immediate test is feasible without introducing concurrency; this is a design-level finding best addressed through API documentation and usage guidelines.

### 6. Type coercion in `load_from_file` may silently alter values

- **Evidence**: In `load_from_file`, after schema validation, the code calls `typedValue.convert(sch->type)`. The `QVariant::convert` method can silently change a value's representation (e.g., truncating a double to an int).
- **Risk**: A valid numeric value that passes `isValid` but loses precision during conversion may be stored differently than what was in the file.
- **Impact**: Correctness risk for settings involving floating-point values converted to integer types.
- **Mitigation**: Verify the converted value matches the original after conversion, or use `QVariant::value<T>()` which performs a direct conversion with well-defined behavior.
- **Follow-up test recommendation**: Load a file containing a numeric value at the boundary of type conversion (e.g., `3.9` stored as an `int` schema) and verify the resulting value.
