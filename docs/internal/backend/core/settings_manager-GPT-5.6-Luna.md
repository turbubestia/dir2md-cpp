# Settings Manager

## Source Pair

- Header: `src/backend/core/settings_manager.hpp`
- Implementation: `src/backend/core/settings_manager.cpp`
- The direct header/implementation counterpart was found and analyzed together.

## Purpose and Role

`dir2md::backend::SettingsManager` owns runtime setting values and the metadata used to validate and persist them. It is a Qt `QObject`, so consumers can observe accepted value changes through `settingChanged` and successful file saves through `settingsSaved`.

The manager stores values in a flat `QHash<QString, QVariant>`. Schemas are stored separately in another hash, keyed by `SettingSchema::key`. A value can be read from the active store, or from its schema default when no active value has been set.

## Data Structures

### `SettingSchema`

A schema describes one setting:

- `key` identifies the setting and is also the registry key.
- `title`, `description`, and `category` provide metadata for consumers and persistence grouping.
- `defaultValue` is returned by `SettingsManager::get` when no active value exists.
- `type` is the target `QMetaType` used by `isValid` and load-time conversion.
- `min` and `max` optionally constrain numeric values.
- `enumOptions` optionally constrains the string representation of a value.

`isValid` first requires `QVariant::canConvert(type)`. For `int` and `double` schemas, it converts the candidate and compares the resulting number with the optional numeric bounds. When `enumOptions` is non-empty, the candidate's `toString()` result must be present in the list.

### `SettingsManager`

The manager contains:

- `m_values`: explicitly set or loaded values, keyed by flat setting key.
- `m_schemaRegistry`: registered schemas, keyed by schema key.

Both hashes provide expected constant-time lookup. The manager does not declare synchronization or locking, so access is expected to follow normal QObject thread-affinity rules and external synchronization requirements.

## Usage and Control Flow

### Registering and reading settings

A caller registers schemas with `registerSchema`. Registration uses `QHash::insert`, so a later schema with the same key replaces the earlier schema.

`get(key)` follows this order:

1. Return the active value from `m_values` when present.
2. Otherwise return the registered schema's `defaultValue`.
3. Otherwise return an invalid `QVariant` for an unknown key.

`schema(key)` returns a copy wrapped in `std::optional`; an unknown key produces `std::nullopt`. `schemas()` and `activeValues()` return const references to the internal hashes for inspection.

### Setting a value

`set(key, value)` validates the value only when a schema for `key` is registered. An unknown key is therefore accepted and stored without type or range validation. For a registered key, a failed `SettingSchema::isValid` check returns `false` and leaves the existing value unchanged.

If the candidate differs from the current active value, it replaces the stored value and emits `settingChanged(key, value)`. Reapplying an equal value returns `true` without emitting the signal. Values are stored as supplied; `set` does not convert an accepted value to the schema's declared type.

### Saving

`save_to_file(filePath)` serializes only `m_values`, not schema defaults. Each active key is grouped under its schema category, or under `General` when the key is unregistered or its schema category is blank after trimming.

The key is split on `/` and written as nested JSON objects. Leading empty path components are removed, while other empty path components remain. The result is formatted with `QJsonDocument::Indented` and written through `QSaveFile`, providing an atomic commit attempt. File open, write, and commit failures return `false` and log a warning. `settingsSaved(filePath)` is emitted only after a successful commit.

### Loading

`load_from_file(filePath)` treats a missing or unreadable file as a normal failure and returns `false` without logging. It reads the complete file, parses JSON, and requires a non-null, non-empty document and a non-empty root object.

The implementation recursively flattens nested JSON objects into slash-separated paths. It interprets the first path component as the persistence category, removes that component, and treats the remainder as a schema key. Only keys with registered schemas are considered. Values failing schema validation are skipped with a warning; unknown keys are skipped silently. Accepted values are converted to the schema type when possible, inserted into `m_values`, and reported through `settingChanged`.

The existing active-value hash is cleared before the flattened entries are processed. The method returns `true` after parsing and processing a non-empty root object, even if no entry survives schema lookup or validation.

## Invariants and Preconditions

- Schema keys must match the flat keys used by callers of `get` and `set`.
- Persistence expects the first JSON path component to be a category and the remaining components to reconstruct a registered schema key.
- `set` validation is conditional on schema registration; callers that require validation must register the schema before setting values.
- `defaultValue` is not validated when a schema is registered. Correctness therefore depends on schema construction providing a value compatible with `type` and its constraints.
- The references returned by `activeValues()` and `schemas()` refer to internal storage. They remain usable only while the manager is alive and while operations that may mutate the corresponding hash do not invalidate the caller's assumptions.
- No thread-safety guarantee is expressed by the class. Calls and signal handling should respect QObject ownership and thread-affinity rules.

## Ownership, Lifetime, and Exception Safety

The manager is a `QObject` with an optional parent; Qt parent ownership controls destruction when a parent is supplied. It stores values and schemas by value, so it does not own external objects referenced by `QVariant` payloads.

The implementation uses Qt value types and does not expose an exception-based error contract. Persistence failures are represented by boolean returns. `QSaveFile` is used to avoid replacing the destination until commit succeeds, but loading is not transactional: the in-memory store is cleared before all input entries have been accepted.

## Static Analysis and Security

### Load is destructive before full validation

- **Evidence:** `load_from_file` calls `m_values.clear()` before iterating over flattened entries. Unknown and invalid entries are then skipped, while the function can still return `true`.
- **Risk:** A syntactically valid file containing only unknown or invalid settings can erase all currently active values and leave the manager partially or completely empty.
- **Impact:** Runtime behavior can unexpectedly fall back to defaults, and callers cannot distinguish successful replacement from a load that discarded all usable settings using the boolean result alone.
- **Mitigation:** Parse and validate into a temporary map first, then replace `m_values` only after the intended load policy succeeds. Consider rejecting a file with no accepted entries when that is invalid for the application.
- **Follow-up test recommendation:** Seed active values, load a valid JSON object containing only unknown or invalid entries, and assert the documented replacement or preservation policy and return value.

### Conversion acceptance may be broader than intended

- **Evidence:** `isValid` relies on `QVariant::canConvert(type)` and numeric checks use `toDouble`; load then calls `convert(sch->type)` when possible. `set` stores the original candidate without conversion.
- **Risk:** Values that are merely convertible, including values subject to coercion or precision loss, may pass validation. The same schema can therefore hold different runtime QVariant types depending on whether the value came from `set` or loading.
- **Impact:** Consumers expecting the declared schema type may observe inconsistent behavior, and numeric constraints may be evaluated on a converted value that is not the value ultimately stored by `set`.
- **Mitigation:** Validate exact or explicitly permitted source types, normalize accepted values to the schema type before storing, and define handling for conversion failure or lossy conversion.
- **Follow-up test recommendation:** Exercise string, floating-point, boolean, and boundary inputs for integer and double schemas through both `set` and `load_from_file`, then assert stored type and value.

### Unknown keys bypass validation

- **Evidence:** `set` validates only when `m_schemaRegistry.contains(key)` is true; otherwise it stores the candidate and returns `true`.
- **Risk:** Typos or unregistered keys can silently become active settings and will be persisted under `General`.
- **Impact:** Configuration errors are difficult to detect and can create data that later cannot be loaded because `load_from_file` silently skips unknown keys.
- **Mitigation:** Decide and document whether dynamic keys are supported. If they are not, reject unknown keys and optionally report the reason; if they are, define a separate schema or persistence policy for them.
- **Follow-up test recommendation:** Set an unregistered key, save it, reload it with the same and with an empty registry, and assert the intended behavior.

### Persistence schema/category assumptions are not validated

- **Evidence:** Saving uses the schema category as a JSON top-level key, while loading always removes the first flattened path component as a category. Categories and setting keys are not checked for empty or conflicting path components.
- **Risk:** A category containing `/`, an empty key segment, or a schema key whose structure conflicts with another value can produce a file that does not round-trip to the original key.
- **Impact:** Settings may be silently omitted, overwritten in the JSON object, or reconstructed under a different key.
- **Mitigation:** Validate key and category syntax at schema registration, define escaping or a structured serialization format, and detect path collisions during save.
- **Follow-up test recommendation:** Add round-trip cases for leading slashes, repeated slashes, empty segments, slash-containing categories, and parent/child key collisions.

### Mutable internal state is exposed by const reference

- **Evidence:** `activeValues()` and `schemas()` return const references to `m_values` and `m_schemaRegistry`.
- **Risk:** A caller can retain a reference across mutations or use it after manager destruction, and assumptions about iterator/reference stability can be violated by later hash operations.
- **Impact:** This creates API lifetime and invalidation hazards even though direct mutation through the returned reference is prevented.
- **Mitigation:** Prefer value returns or explicit snapshot APIs for external inspection, or document lifetime and invalidation guarantees precisely.
- **Follow-up test recommendation:** Add API-usage tests that obtain snapshots/references, mutate or destroy the manager, and verify the supported contract.

No raw-pointer ownership bugs or unsafe casts are visible in this source pair. Thread safety, QVariant conversion details, JSON conversion behavior, and QSaveFile failure semantics beyond the directly used Qt APIs remain contextual dependencies and should be verified against the Qt version used by the project.
