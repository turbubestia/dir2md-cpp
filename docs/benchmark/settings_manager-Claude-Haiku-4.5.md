# SettingsManager — Settings Storage and Schema Validation

## Purpose and Role

`SettingsManager` is a Qt-based application configuration system that provides:

- **Schema-driven validation**: Settings are registered with metadata (type, constraints, defaults, categories) before use.
- **Nested key-value storage**: Keys use "/" as a path separator (e.g., `editor/tab_size`, `display/theme/dark_mode`).
- **Atomic persistence**: Settings are saved to and loaded from a hierarchical JSON file with atomic writes via `QSaveFile`.
- **Change notifications**: Qt signals emit when settings change or are saved, enabling reactive UI updates.

The SettingsManager is part of the backend module (pure QtCore, no UI dependencies) and is intended to be shared between the frontend (QtQuick) and CLI applications.

## Architecture and Data Structures

### SettingSchema

Metadata descriptor for a single setting:

```
struct SettingSchema {
    QString key;              // Identifier: "category/subcategory/name"
    QString title;            // Display name
    QString description;      // User-facing description
    QString category;         // Grouping for serialization
    QVariant defaultValue;    // Fallback if not set by user
    QMetaType type;           // Type constraint (Int, Bool, String, etc.)
    QVariant min, max;        // Numeric range constraints
    QStringList enumOptions;  // Allowed string values
};
```

The `isValid()` method performs three-layer validation:
1. Type convertibility check
2. Numeric range enforcement (if min/max are set)
3. Enum constraint check (if enumOptions is non-empty)

### SettingsManager

A QObject that manages the lifecycle of settings:

- **m_values**: `QHash<QString, QVariant>` — flat, in-memory store of user-set values.
- **m_schemaRegistry**: `QHash<QString, SettingSchema>` — schema metadata for all registered settings.

Signals:
- `settingChanged(const QString &key, const QVariant &newValue)` — emitted when a setting is modified via `set()`.
- `settingsSaved(const QString &path)` — emitted when `save_to_file()` succeeds.

## Public API and Usage Patterns

### Registration and Querying

```cpp
SettingSchema schema{
    .key = "editor/tab_size",
    .title = "Tab Size",
    .description = "Number of spaces per tab",
    .category = "Editor",
    .defaultValue = 4,
    .type = QMetaType::fromType<int>(),
    .min = 1,
    .max = 16
};
manager.registerSchema(schema);

// Query
auto setting = manager.schema("editor/tab_size");
if (setting.has_value()) {
    // Use setting->min, setting->max, etc.
}
```

### Getting and Setting Values

```cpp
// Get with fallback chain: user value → schema default → QVariant()
QVariant val = manager.get("editor/tab_size");  // Returns 4 by default

// Set with schema validation
bool success = manager.set("editor/tab_size", 2);  // true if valid
bool failed = manager.set("editor/tab_size", 100); // false (exceeds max)
```

### Persistence

```cpp
// Atomic write to disk with QJsonDocument (indented format)
if (manager.save_to_file("/path/to/settings.json")) {
    // File written atomically; settingsSaved signal emitted
}

// Load from disk, silently skipping unknown keys
if (manager.load_from_file("/path/to/settings.json")) {
    // All loaded values are validated against registered schemas
}
```

### Inspecting All Active Settings

```cpp
const auto &allValues = manager.activeValues();  // O(1) access to all user-set values
const auto &allSchemas = manager.schemas();      // O(1) access to all schemas
```

## Serialization Format

Settings are nested by category in JSON:

```json
{
  "Editor": {
    "tab_size": 4,
    "use_spaces": true,
    "line_ending": "lf"
  },
  "Display": {
    "theme": {
      "dark_mode": true,
      "font_size": 12
    }
  },
  "General": {
    "auto_save": true
  }
}
```

On load, this nested structure is flattened to keys like:
- `Editor/tab_size` → `editor/tab_size` (category stripped, case preserved from schema key)
- `Display/theme/dark_mode` → `display/theme/dark_mode`

## State and Control Flow

### Initialization

1. Create `SettingsManager` instance.
2. Call `registerSchema()` for each application setting (typically at startup).
3. Optionally call `load_from_file()` to populate user-saved values.

### Runtime

1. Query via `get()` (returns user value or default).
2. Modify via `set()` with automatic validation.
3. Listen to `settingChanged()` signal for reactive updates.

### Persistence

1. Call `save_to_file()` when user modifies settings (or periodically).
2. Atomic write ensures file integrity; partial writes are rolled back.
3. Future loads skip unknown keys silently (forward/backward compatibility).

## Important Invariants and Preconditions

- **Schema Must Be Registered Before Use**: Calling `get()` or `set()` on an unregistered key will not validate or enforce constraints.
- **Default Values Are Not Validated**: `SettingSchema::isValid()` is not called on `defaultValue` during registration; invalid defaults can be returned by `get()`.
- **Type Conversion Is Lossy**: `QVariant::convert()` may silently lose information (e.g., double → int truncation).
- **No Thread Safety**: Concurrent `get()`, `set()`, or `save_to_file()` calls are not synchronized.
- **Categories Are Inferred on Save, Not Stored**: Schema `category` field is used during `save_to_file()` to organize JSON; on load, the category is stripped based on path depth, so misalignment between schema category and actual nesting can cause issues.
- **QMetaType Comparison**: The `type` field uses `QMetaType` instances. Comparisons (e.g., `type == QMetaType::fromType<int>()`) are safe, but `QMetaType` creation must match the actual underlying type.

## Ownership and Lifetime

- **SettingsManager is a QObject**: Parent–child ownership follows Qt conventions; destructor cleans up `m_values` and `m_schemaRegistry`.
- **No External Ownership Assumed**: Caller is responsible for ensuring `SettingsManager` outlives any code that holds or listens to its signals.
- **Signal Listeners**: Connected slots should not delete the `SettingsManager` while processing signals.

## Error Handling

- **`set()` Returns bool**: Returns `false` if validation fails; value is not changed.
- **`save_to_file()` Returns bool**: Returns `false` on file I/O error; reasons are logged to `qWarning()`.
- **`load_from_file()` Returns bool**: Returns `false` on parse or I/O error; unknown keys are silently skipped; missing file is not treated as an error (expected on first run).
- **No Exceptions Thrown**: All errors are reported via return codes or Qt warning logs.

## Static Analysis and Security

### Finding 1: Nested JSON Insertion Bug in `insertNestedValue()`

**Evidence:**
Lines 14–37 in `settings_manager.cpp`. The function recursively nests values but does not properly handle pre-existing non-object values when nesting:

```cpp
QString first = pathParts[0];
QJsonObject nested;
if (!obj[first].isObject()) {   // ← condition is backwards
    nested = obj[first].toObject();
} else {
    nested = obj[first].toObject();
}
insertNestedValue(nested, pathParts.mid(1), value);
```

If `obj[first]` exists but is not an object (e.g., a string or number), the condition `!obj[first].isObject()` is true, and the existing value is lost when converted to a default `QJsonObject`.

**Risk:** When saving settings with hierarchical keys (e.g., first saving `display/theme = true`, then `display/theme/dark_mode = false`), the nested assignment overwrites the parent scalar value without warning, leading to silent data loss.

**Impact:** Users may lose settings or encounter unexpected JSON structure if key paths overlap (scalar parent and nested child).

**Mitigation:**
- Add a check: if `obj[first]` exists and is not an object, log a warning and skip nesting, or reject the key as malformed.
- Document the constraint that keys with scalar ancestors (e.g., `display/theme = true` followed by `display/theme/dark_mode`) are not supported.
- Consider validating schema keys at registration time to prevent overlapping paths.

**Follow-up Test Recommendation:**
```
Test overlapping hierarchical keys:
- registerSchema("display/theme", true)
- registerSchema("display/theme/dark_mode", false)
- save_to_file() and inspect the JSON output
- Verify that the scalar value is preserved or an error is raised
```

---

### Finding 2: Ambiguous Category Stripping in `load_from_file()`

**Evidence:**
Lines 199–210 in `settings_manager.cpp`:

```cpp
// Strip the category prefix: "Category/nested/key" → "nested/key"
QStringList flatParts = flatKey.split("/", Qt::KeepEmptyParts);
while (!flatParts.isEmpty() && flatParts.first().isEmpty()) {
    flatParts.removeFirst();
}
if (flatParts.isEmpty()) {
    continue;
}
flatParts.removeFirst();  // ← Remove category
QString schemaKey = flatParts.join("/");
```

The code assumes that the first path segment is always the category. However:
1. Schema keys may intentionally not follow a category prefix (e.g., a key registered as `"tab_size"` without a leading category).
2. If the schema category field doesn't match the on-disk category prefix, the stripping will be incorrect.

**Risk:** Loaded settings may map to the wrong schema keys, causing validation failures and silent skipping, or worse, applying settings to unintended keys.

**Impact:** Loading a settings file may silently omit settings if the on-disk structure doesn't match the expected category nesting. Forward/backward compatibility is compromised if schema categories change.

**Mitigation:**
- Document the constraint: all schema keys must be of the form `"Category/path/to/key"` and must match the `category` field in the schema.
- Add validation: during `load_from_file()`, log a warning if the stripped key does not match a registered schema with the same category.
- Consider embedding category information in the JSON (e.g., a top-level `"_metadata"` field) to decouple serialization format from schema category assumptions.

**Follow-up Test Recommendation:**
```
Test category stripping edge cases:
- Define a schema with key "tab_size" (no category prefix) and category "Editor"
- Save it with category nesting: "Editor/tab_size"
- Load the file and verify the key maps correctly
- Test with category mismatches: schema category "Editor" but on-disk "Preferences"
```

---

### Finding 3: No Validation of Default Values

**Evidence:**
Line 102 in `settings_manager.hpp` defines `defaultValue` as a `QVariant` field in `SettingSchema`. The constructor `SettingsManager::SettingsManager()` and `registerSchema()` (lines 103–105) do not call `isValid(defaultValue)` before storing the schema.

**Risk:** A schema can be registered with an invalid default value (e.g., `defaultValue = 100` with `max = 50`). When `get()` is called on an unset key, the invalid default is returned without validation.

**Impact:** The public API contract of `SettingSchema::isValid()` is not enforced for defaults, leading to inconsistent state where defaults can violate their own constraints.

**Mitigation:**
- Add validation in `registerSchema()`: assert or warn if `schema.isValid(schema.defaultValue)` is false.
- Document the constraint: all default values must satisfy their schema's validation rules.

**Follow-up Test Recommendation:**
```
Test invalid defaults:
- Register a schema with defaultValue = 100, max = 50
- Call get() on the unset key
- Verify that a warning is logged and/or the invalid default is rejected
```

---

### Finding 4: Unchecked Type Conversion in `load_from_file()`

**Evidence:**
Lines 256–258 in `settings_manager.cpp`:

```cpp
QVariant typedValue = value;
if (typedValue.canConvert(sch->type)) {
    typedValue.convert(sch->type);  // ← Return value ignored
}
```

The `QVariant::convert()` method returns a `bool` indicating success, but the code ignores it. If conversion fails, the unconverted (possibly type-mismatched) value is inserted into `m_values`.

**Risk:** A value that passes `canConvert()` but fails `convert()` will be stored in the wrong type, potentially causing crashes or undefined behavior in type-expecting code downstream.

**Impact:** Type safety is compromised for loaded settings; unexpected type mismatches may propagate silently.

**Mitigation:**
- Check the return value: `if (!typedValue.convert(sch->type)) { qWarning() << ...; continue; }`.
- Add a post-insert check: verify `m_values[key].type() == sch->type`.

**Follow-up Test Recommendation:**
```
Test failed type conversions:
- Create a schema expecting QMetaType::Int
- Save a non-convertible value (e.g., complex object) to the JSON file manually
- Load the file and verify a warning is logged and the setting is skipped
```

---

### Finding 5: Silent Skipping of Unknown Keys on Load

**Evidence:**
Lines 213–216 in `settings_manager.cpp`:

```cpp
auto sch = schema(schemaKey);
if (!sch.has_value()) {
    // Unknown key — silently skip
    continue;
}
```

When loading from a file, any key that is not registered in `m_schemaRegistry` is silently ignored with no warning.

**Risk:** Typos or obsolete settings in the config file will be silently discarded, making it difficult to debug configuration issues or detect file corruption.

**Impact:** Users may not realize that settings are missing or corrupted; troubleshooting configuration problems becomes harder.

**Mitigation:**
- Log a debug or info message when skipping unknown keys: `qDebug() << "Skipping unknown setting key:" << schemaKey;`.
- Add an optional strict mode that returns `false` if any unknown keys are encountered.

**Follow-up Test Recommendation:**
```
Test unknown key handling:
- Save a valid settings file
- Manually add an unregistered key to the JSON
- Load the file and verify a warning is logged
- Verify that the unknown key is not stored in m_values
```

---

### Finding 6: No Thread Safety Guarantees

**Evidence:**
The class has no internal synchronization (locks, atomics). All public methods (`get()`, `set()`, `save_to_file()`, `load_from_file()`) directly access `m_values` and `m_schemaRegistry` without mutual exclusion.

**Risk:** If `SettingsManager` is accessed from multiple threads concurrently (e.g., UI thread calling `set()` while a save thread calls `save_to_file()`), data races and undefined behavior can occur.

**Impact:** In multithreaded applications, crashes, data corruption, or inconsistent state are possible.

**Mitigation:**
- Document that `SettingsManager` is not thread-safe and must be accessed from a single thread (typically the main/Qt event loop thread).
- If multithreaded access is required, add a `QMutex` and protect all access paths.
- Consider using Qt's thread-safe signal mechanism: emit signals on the creating thread's event loop.

**Follow-up Test Recommendation:**
```
Stress test with concurrent access (if multithreading is planned):
- Create SettingsManager on the main thread
- Spawn worker threads that call get(), set(), save_to_file() concurrently
- Run under ThreadSanitizer or Helgrind to detect race conditions
- Verify that either no crashes occur or appropriate warnings are documented
```

---

### Finding 7: Ambiguous Enum Option Validation

**Evidence:**
Lines 65–66 in `settings_manager.cpp`:

```cpp
if (!enumOptions.isEmpty() && !enumOptions.contains(val.toString())) {
    return false;
}
```

If `enumOptions` is a `QStringList`, the code converts the value to a string and checks membership. However:
1. If the value is an integer (e.g., 0, 1, 2) but `enumOptions` contains strings ("on", "off"), `val.toString()` produces `"0"`, not `"on"`, causing a false rejection.
2. Conversely, if a numeric enum is intended but strings are stored, type mismatches occur.

**Risk:** Enum validation may incorrectly reject valid values or accept invalid ones depending on the underlying type mismatch.

**Impact:** Schemas with enum constraints may not work as intended if the stored value type does not match the enum option type.

**Mitigation:**
- Document the constraint: `enumOptions` values must match the string representation of expected values for the given `type`.
- Consider using an `enum` type in `QMetaType` to make intent clearer, or add a separate field for numeric enum ranges.
- Add type-aware enum validation: for numeric types, convert enum options to numbers before comparison.

**Follow-up Test Recommendation:**
```
Test enum validation with type mismatches:
- Register a schema with type=QMetaType::Int and enumOptions={"0", "1", "2"}
- Call set(key, 0) and set(key, "0")
- Verify both succeed or both fail, depending on intent
```

---

### Residual Risks and Assumptions

- **JSON File Integrity**: The code assumes that the JSON file is not corrupted outside the application. Manual edits or partial writes by external processes are not detected.
- **Disk Space**: No check for disk space before `save_to_file()` writes; could result in truncated files if the disk is full.
- **File Permissions**: No recovery if the file becomes read-only or unwritable after initialization.
- **Locale and Encoding**: JSON is serialized with `QJsonDocument::toJson()`, which uses UTF-8; non-UTF-8 files may fail to parse silently.

## Summary

`SettingsManager` provides a robust, schema-driven settings system with atomic persistence and change notifications. The main risks are:

1. Nested JSON insertion overwriting scalar values silently.
2. Ambiguous category stripping during load.
3. Invalid default values not validated at registration.
4. Unchecked type conversions on load.
5. Silent skipping of unknown keys.
6. No thread-safety guarantees.
7. Ambiguous enum validation for type mismatches.

**Counterpart Status**: Both `settings_manager.hpp` and `settings_manager.cpp` were analyzed as a complete source pair.
