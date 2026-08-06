# settings_manager

**Source pair analyzed:**
- [src/backend/core/settings_manager.hpp](../../../src/backend/core/settings_manager.hpp)
- [src/backend/core/settings_manager.cpp](../../../src/backend/core/settings_manager.cpp)

## Purpose and Role

`SettingsManager` is a Qt-based settings store used by the `dir2md::backend` library. It maintains a flat registry of active setting values and a registry of schemas that describe each setting's metadata, default value, type, and constraints. The class is intended to be consumed by both the CLI and the QtQuick frontend.

## Major Types and Classes

### `SettingSchema`

A plain data structure that defines metadata for a single setting key:

| Member | Meaning |
|--------|---------|
| `key` | Flat dotted/slashed identifier, e.g. `"core/max_threads"`. |
| `title` | Human-readable short name. |
| `description` | Human-readable explanation. |
| `category` | Grouping category used during serialization, e.g. `"General"` or `"Performance"`. |
| `defaultValue` | Value returned by `get()` when no active value exists. |
| `type` | Target `QMetaType` for validation and conversion. |
| `min` / `max` | Optional numeric bounds. |
| `enumOptions` | Optional list of allowed string values. |

`isValid(const QVariant&)` checks:
1. Whether `val.canConvert(type)` succeeds.
2. For `int` and `double` types, whether the numeric value lies inside `min`/`max` (if set).
3. Whether `val.toString()` is contained in `enumOptions` (if any are defined).

### `SettingsManager`

Public interface:

- `get(key)` — returns the active value, the schema's default value, or an invalid `QVariant`.
- `set(key, value)` — validates the value when a schema exists, stores it if it changed, and emits `settingChanged()`.
- `activeValues()` / `schemas()` — const references to the internal registries.
- `registerSchema(schema)` — inserts or overwrites a schema entry.
- `schema(key)` — returns `std::optional<SettingSchema>`.
- `save_to_file(path)` — serializes active values to indented JSON using atomic writes via `QSaveFile`.
- `load_from_file(path)` — reads JSON, flattens nested objects, validates against schemas, and repopulates active values.

Signals:
- `settingChanged(key, newValue)` — emitted on `set()` when the value changes, and for every key loaded from file.
- `settingsSaved(path)` — emitted after a successful `save_to_file()`.

## Save/Load File Format

`save_to_file()` groups values by schema `category` (defaulting to `"General"` when no schema exists). Within each category object, keys are split on `"/"` and nested. Example:

```json
{
    "General": {
        "core": {
            "tool_path": "/usr/bin/tool"
        }
    },
    "Performance": {
        "core": {
            "max_threads": 4
        }
    }
}
```

`load_from_file()` flattens the JSON, strips the first path segment as the category, and uses the remainder as the schema key. Values are validated and, if possible, converted to the schema's `QMetaType` before storage.

## Public Usage Patterns

```cpp
dir2md::backend::SettingsManager manager;
CoreSchema::registerSchemas(manager);

manager.set("core/max_threads", 8);          // validated, emits signal
auto threads = manager.get("core/max_threads"); // 8
manager.save_to_file("settings.json");
manager.load_from_file("settings.json");
```

## Important Behavior and Assumptions

- **Flat key namespace.** All active values are stored in a single-level `QHash<QString, QVariant>` keyed by the full schema key, even though the serialized form is nested under a category.
- **Schema-less values accepted.** `set()` validates only when a schema is registered; unregistered keys are accepted without validation.
- **Default fallback.** `get()` returns the schema default before falling back to an invalid `QVariant`. There is no explicit "key missing" signal.
- **Overwrite semantics.** `registerSchema()` silently overwrites any existing schema with the same key.
- **Signal on load.** Every successfully loaded value emits `settingChanged()`, even during initial load.
- **Atomic save.** `QSaveFile` is used for crash-safe writes; failures are logged with `qWarning()` and return `false`.
- **Silent load failures.** A missing or unreadable file returns `false` without warning; parse errors and invalid per-key values are logged and skipped.
- **Threading.** No synchronization is provided. `SettingsManager` is a `QObject` and should be used from the thread that owns it.
- **Exception safety.** The implementation relies on Qt containers and value types. No exceptions are caught and none are expected from Qt APIs used here.
- **Category stripping on load.** The loader assumes exactly one category segment at the root of the JSON object. Deeper or mismatched nesting may produce unexpected keys or be dropped.

## Static Analysis and Security

### 1. Unregistered keys are lost on save/load round-trip

- **Evidence:** `save_to_file()` writes unregistered keys under the default category `"General"`. `load_from_file()` strips the category segment, looks up the remaining key in `m_schemaRegistry`, and `continue`s if the key is unknown.
- **Risk:** Values set without a matching schema disappear after a save/load cycle.
- **Impact:** Silent data loss for callers that `set()` arbitrary keys without registering schemas.
- **Mitigation:** Reject `set()` for unregistered keys, or register schemas for every key that must persist. Document that persistence requires a schema.
- **Follow-up test:** Set an unregistered key, save, load, and assert the key is still present.

### 2. `insertNestedValue` has a misleading/dead branch and can clobber non-object values

- **Evidence:** Both branches of `if (!obj[first].isObject())` and `else` perform `nested = obj[first].toObject()`. `QJsonValue::toObject()` returns an empty object when the value is not an object, so any existing scalar at `first` is discarded.
- **Risk:** If the JSON object passed in already contains mixed types, nested insertion silently replaces them. In current usage the object is built fresh, but the helper is fragile if reused.
- **Impact:** Potential data corruption or unexpected serialization behavior for callers that reuse the helper with pre-existing objects.
- **Mitigation:** Rewrite the branch to preserve existing objects and explicitly handle conflicts (e.g., log a warning or assert that scalar-vs-object collisions do not occur).
- **Follow-up test:** Serialize two values whose paths share a prefix where one is a leaf and the other is nested (e.g. `"a"` and `"a/b"`).

### 3. `load_from_file` ignores `QVariant::convert` failure

- **Evidence:** `if (typedValue.canConvert(sch->type)) { typedValue.convert(sch->type); }` discards the boolean result of `convert()` and inserts `typedValue` regardless.
- **Risk:** A value that passes `canConvert()` but fails conversion can be stored with the wrong type.
- **Impact:** Type invariant of `m_values` is violated, which may cause runtime misbehavior in consumers that rely on the schema type.
- **Mitigation:** Check the `convert()` result and skip or warn on failure.
- **Follow-up test:** Load a JSON value that claims to be convertible to the target type but fails conversion (e.g., an out-of-range integer).

### 4. Empty and duplicate path segments produce ambiguous keys

- **Evidence:** `key.split("/", Qt::KeepEmptyParts)` is used in both save and load without filtering empty middle or trailing segments. Keys such as `"a//b/"` round-trip through JSON objects with empty-string keys.
- **Risk:** Ambiguous or malformed keys can serialize successfully but deserialize differently, or collide with legitimate keys.
- **Impact:** Potential for setting leaks, lookup failures, or unexpected nesting.
- **Mitigation:** Validate keys during `registerSchema()` and `set()` to reject empty segments, leading/trailing slashes, and consecutive slashes.
- **Follow-up test:** Register and save keys containing `"//"` or trailing `"/"`, then load and verify the key matches.

### 5. `load_from_file` clears active values before validation

- **Evidence:** `m_values.clear()` is called before the validation loop. If the file contains only invalid values, the manager ends up empty.
- **Risk:** A corrupted or partial file can wipe out previously valid in-memory settings.
- **Impact:** Loss of user configuration when a load operation partially fails.
- **Mitigation:** Load into a temporary map first; only replace `m_values` after all entries are validated, or provide an atomic "replace on success" guarantee.
- **Follow-up test:** Load a file where every value is invalid and assert the previous settings remain unchanged.

### 6. Category mismatch between save and load is not detected

- **Evidence:** The loader strips the first path segment unconditionally and uses the rest as the schema key. It never compares the stripped category to `schema.category`.
- **Risk:** A hand-edited file can place a key under the wrong category and still load successfully.
- **Impact:** Category becomes a serialization hint rather than a trusted invariant, which may confuse UI consumers that group settings by category.
- **Mitigation:** Optionally warn when the stripped category does not match `schema.category`, or make category a pure presentation concern outside the persisted key.
- **Follow-up test:** Move a key under a different category in the JSON file and verify whether the load behavior is intentional.

### 7. `SettingSchema::isValid` uses string comparison for enum validation regardless of type

- **Evidence:** `enumOptions.contains(val.toString())` is applied without regard to the schema's declared `type`.
- **Risk:** An integer value whose string representation is in `enumOptions` passes validation even if the intended comparison was numeric.
- **Impact:** Values may be accepted that do not match the intended enum semantics.
- **Mitigation:** Convert to the target type first, then compare using the typed representation, or document that `enumOptions` must contain string representations and enforce consistent use.
- **Follow-up test:** Validate an integer-typed value against `enumOptions` containing its numeric string and confirm whether this is the desired behavior.

### 8. No size limits on loaded files

- **Evidence:** `file.readAll()` loads the entire file into a `QByteArray`.
- **Risk:** A maliciously large settings file can exhaust memory.
- **Impact:** Denial of service via resource exhaustion.
- **Mitigation:** Enforce a reasonable maximum file size before reading, or stream the JSON parser.
- **Follow-up test:** Attempt to load an oversized settings file and verify graceful failure.

### 9. `m_values` and `m_schemaRegistry` are not thread-safe

- **Evidence:** All member access is unguarded. `QObject` itself is not thread-safe for concurrent modification.
- **Risk:** Concurrent calls to `set()`, `load_from_file()`, or `registerSchema()` from multiple threads can corrupt the hashes or emit signals across thread boundaries unsafely.
- **Impact:** Crashes or inconsistent state in multi-threaded usage.
- **Mitigation:** Document that all access must happen on the object's thread, or protect the maps with a mutex and use `QMetaObject::invokeMethod` for cross-thread signals if needed.
- **Follow-up test:** Exercise concurrent `set()` calls from multiple threads and verify stability.

### Residual notes

- The implementation uses Qt value semantics and does not perform manual memory management; no raw pointer ownership issues were observed in the requested pair.
- `QSaveFile` provides atomic replacement on success, which is a positive security/correctness choice.
- Header-only or source-only concerns do not apply because both `.hpp` and `.cpp` files exist.
