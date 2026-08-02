# SettingsManager Static Analysis and Technical Documentation

## Source Purpose and Role

`SettingsManager` and `SettingSchema` form the centralized configuration backend for the application (`src/backend/core/settings_manager.hpp` and `src/backend/core/settings_manager.cpp`). Situated in the `dir2md::backend` namespace, this subsystem manages application settings by providing:
- Key-value setting storage with optional schema validation.
- Categorization and hierarchical key structure.
- Signal notification upon setting changes (`settingChanged`) and file saves (`settingsSaved`).
- Atomic file persistence (`save_to_file`) and structured JSON reading (`load_from_file`).

## Major Classes, Functions, and Data Structures

### `SettingSchema` (Struct)
Defines metadata and validation constraints for a single configuration key.
- **Data Members**:
  - `QString key`: Unique setting identifier (e.g., `"editor/tab_size"`).
  - `QString title`: Human-readable name.
  - `QString description`: Detailed description of the setting.
  - `QString category`: Category grouping (e.g., `"Editor"`).
  - `QVariant defaultValue`: Fallback value if no user setting is stored.
  - `QMetaType type`: Expected metadata type (e.g., `QMetaType::Int`, `QMetaType::Bool`).
  - `QVariant min`, `QVariant max`: Numeric range limits (optional).
  - `QStringList enumOptions`: Allowed string options (optional).
- **Methods**:
  - `isValid(const QVariant &val) const -> bool`: Validates a candidate value against type, numeric bounds (`min`/`max`), and enumeration constraints (`enumOptions`).

### `SettingsManager` (Class)
Manages runtime configuration values and schema registrations.
- **Data Members**:
  - `QHash<QString, QVariant> m_values`: Active user configuration values indexed by key.
  - `QHash<QString, SettingSchema> m_schemaRegistry`: Registered schema metadata indexed by key.
- **Key Methods**:
  - `registerSchema(const SettingSchema &schema)`: Registers schema metadata for a setting key.
  - `get(const QString &key) const -> QVariant`: Retrieves user-configured value, or schema default value if unconfigured, or an invalid `QVariant` if key is unknown.
  - `set(const QString &key, const QVariant &value) -> bool`: Validates value against schema (if present) and updates setting. Emits `settingChanged` if value changed.
  - `activeValues() const -> const QHash<QString, QVariant>&`: Returns flat map of active user settings.
  - `schema(const QString &key) const -> std::optional<SettingSchema>`: Returns registered schema for a key if present.
  - `schemas() const -> const QHash<QString, SettingSchema>&`: Returns all registered schemas.
  - `save_to_file(const QString &filePath) -> bool`: Groups values by category, nests keys by `/`, serializes to formatted JSON, and writes atomically via `QSaveFile`.
  - `load_from_file(const QString &filePath) -> bool`: Reads JSON from disk, flattens structure, strips top-level category prefixes, validates values against registered schemas, and updates runtime settings.

## Public Usage Patterns

```cpp
using namespace dir2md::backend;

SettingsManager manager;

// 1. Register Schema
SettingSchema tabSizeSchema{
    .key = "editor/tab_size",
    .title = "Tab Size",
    .description = "Number of spaces per tab",
    .category = "Editor",
    .defaultValue = 4,
    .type = QMetaType::fromType<int>(),
    .min = 1,
    .max = 16
};
manager.registerSchema(tabSizeSchema);

// 2. Read Setting (Returns default 4)
QVariant size = manager.get("editor/tab_size");

// 3. Set Setting (Validates against schema and emits settingChanged)
bool success = manager.set("editor/tab_size", 8);

// 4. Persist to JSON File
manager.save_to_file("config.json");

// 5. Restore from JSON File
manager.load_from_file("config.json");
```

## Control Flow and Serialization Algorithms

### Serialization (`save_to_file`)
1. Iterates over active values in `m_values`.
2. Resolves category via schema lookup (defaults to `"General"` if no category is specified or key is unregistered).
3. Splits setting key on `/` (e.g., `"editor/tab_size"` $\rightarrow$ `["editor", "tab_size"]`).
4. Uses recursive `insertNestedValue()` to build a nested `QJsonObject` structure inside the target category object.
5. Formats JSON document with indentations using `QJsonDocument::Indented`.
6. Performs atomic disk write using `QSaveFile` (writes to temporary file and renames on `commit()`).
7. Emits `settingsSaved(filePath)` on successful commit.

### Deserialization (`load_from_file`)
1. Opens file for reading using `QFile`.
2. Parses byte payload via `QJsonDocument::fromJson`.
3. Flattens nested JSON hierarchy into key paths using `flattenJsonObject()` (e.g., `"Editor/editor/tab_size"`).
4. Clears active setting values in `m_values`.
5. Iterates through flattened JSON key paths, splits on `/`, and strips the top-level category segment.
6. Lookups schema for reconstituted key (`"editor/tab_size"`). Silently skips unknown keys.
7. Validates value with `schema->isValid()`. Silently skips invalid values.
8. Converts variant to expected schema type and stores value in `m_values`, emitting `settingChanged` for each updated entry.

## Ownership, Lifetime, Thread-Safety, and Exception-Safety

- **Ownership & Lifetime**: `SettingsManager` inherits from `QObject`. Ownership follows Qt parent-child hierarchy if a parent `QObject` is passed to the constructor. Standard value types (`QHash`, `QVariant`, `QString`, `SettingSchema`) are managed by value.
- **Nullability**: Methods return `std::optional<SettingSchema>` when schema lookup may fail, avoiding raw pointer nullability issues.
- **Thread-Safety**: `SettingsManager` is **not thread-safe**. Concurrent reads and writes to `m_values` or `m_schemaRegistry` across multiple threads will cause data races. All operations should occur on the managing `QThread` (typically the main/GUI thread).
- **Exception-Safety**: Operations rely on Qt containers and value semantics, providing strong exception safety. File operations do not throw exceptions and return `bool` status codes.

## Input Validation and Error-Handling Behavior

- `SettingSchema::isValid()` validates type convertibility (`canConvert`), numeric range bounds (`min`, `max`), and string options (`enumOptions`).
- Unregistered keys set via `set()` are stored without schema validation.
- Unknown keys present in a JSON file during `load_from_file()` are silently ignored.
- File write failures (permissions, disk full) are logged via `qWarning()` and return `false`.

## Contextual Dependencies

- `QtCore` components: `QObject`, `QHash`, `QString`, `QVariant`, `QStringList`, `QFile`, `QSaveFile`, `QJsonDocument`, `QJsonObject`, `QJsonArray`, `QMetaType`, `QDebug`.

---

## Static Analysis and Security

### 1. State Loss on Corrupted or Partial Configuration File Load
- **Evidence**: In `load_from_file()` (`src/backend/core/settings_manager.cpp`), `m_values.clear()` is called immediately after verifying that the root JSON object is non-empty, but *before* parsing, validating, or matching any schema keys.
- **Risk**: If a JSON file contains valid JSON structure but invalid value types, unknown keys, or corrupted setting data, `m_values` is wiped clean. Existing runtime settings are discarded without being replaced by valid file settings.
- **Impact**: Loss of active runtime configuration and unexpected fallback to defaults mid-session.
- **Mitigation**: Stash parsed file values in a temporary `QHash<QString, QVariant>` first. Only clear and update `m_values` once file contents have been successfully parsed and validated.
- **Follow-up Test Recommendation**: Invoke `load_from_file()` with a JSON file containing only invalid or unregistered keys when `m_values` already contains active settings, and verify that existing settings remain intact.

### 2. Redundant Code and Potential Structure Overwrite in `insertNestedValue`
- **Evidence**: In `insertNestedValue()` (`src/backend/core/settings_manager.cpp`):
  ```cpp
  QJsonObject nested;
  if (!obj[first].isObject()) {
      nested = obj[first].toObject();
  } else {
      nested = obj[first].toObject();
  }
  ```
  Both branches of the `if`-statement execute identical code (`nested = obj[first].toObject();`).
- **Risk**: The condition `!obj[first].isObject()` was likely intended to handle cases where `obj[first]` is a primitive value or missing. Calling `.toObject()` on a primitive value returns an empty `QJsonObject`, silently overwriting any existing non-object JSON value stored under key `first`.
- **Impact**: Dead code branch; potential silent overwriting of configuration keys if slash-separated paths conflict with primitive node names.
- **Mitigation**: Replace with `if (obj.contains(first) && obj[first].isObject()) { nested = obj[first].toObject(); }`.
- **Follow-up Test Recommendation**: Test `save_to_file()` with conflicting setting keys (e.g. `"foo"` and `"foo/bar"`).

### 3. Limited Numeric Bounds Checking in `SettingSchema::isValid`
- **Evidence**: `SettingSchema::isValid()` checks bounds using:
  ```cpp
  if (type == QMetaType::fromType<int>() || type == QMetaType::fromType<double>()) { ... }
  ```
- **Risk**: Other numeric meta-types (e.g., `QMetaType::LongLong`, `QMetaType::Float`, `QMetaType::UInt`, `QMetaType::Short`) bypass min/max bounds validation even if `min` and `max` constraints are set.
- **Impact**: Bypassed schema validation leading to out-of-bound values in application state for non-`int`/non-`double` numeric types.
- **Mitigation**: Expand type comparison or use numeric type category check (e.g., checking if `type.flags().testFlag(QMetaType::IsNumeric)` or handling additional common integral/floating types).
- **Follow-up Test Recommendation**: Register a schema with type `QMetaType::fromType<float>()` or `QMetaType::fromType<qlonglong>()` with `min`/`max` constraints, and test `isValid()` with out-of-range values.

### 4. Unconditional Top-Level Segment Removal during Unflattening
- **Evidence**: In `load_from_file()`, `flatParts.removeFirst()` unconditionally drops the first segment of every flattened key path to strip the category prefix.
- **Risk**: Assumes all valid configuration JSON files strictly adhere to the category-grouped structure written by `save_to_file()`. If a user manually edits or provides a flat JSON file without top-level category wrappers, valid setting keys will have their leading key path segment silently stripped (e.g., `"editor/tab_size"` becomes `"tab_size"`).
- **Impact**: Config keys from non-categorized JSON files fail to match registered schemas and are silently ignored.
- **Mitigation**: Check if the stripped key matches a registered schema; if not, test if the original full key path matches a schema before discarding.
- **Follow-up Test Recommendation**: Pass a non-categorized flat JSON document (e.g. `{"editor/tab_size": 4}`) to `load_from_file()` and verify behavior.
