# Implementation Analysis: 1-save-load-settings

## 1. Architectural Impact & Data Flow

*High-level overview of how data flows through the system for this feature. Identify any new patterns or structural additions.*

- **Affected Subsystems:** Backend Core (`src/backend/core/`) only — no frontend, CLI, or schema changes required.
- **Data Flow Changes:**

  **Save Path (In-Memory → Disk):**
  ```
  SettingsManager::save_to_file()
    → flattenJsonObject(m_values)           // QHash<QString,QVariant> → nested QJsonObject by category
    → insertNestedValue per category         // flat key split on "/" into deep nesting
    → QJsonDocument::fromObject()            // nested QJsonObject → UTF-8 JSON text (pretty-printed)
    → QSaveFile atomic write                 // safe, crash-resistant disk write
    → emit settingsSaved(path)               // signal emission on success
  ```

  **Load Path (Disk → In-Memory):**
  ```
  SettingsManager::load_from_file(filePath)
    → QFile open/read                         // attempt to read config file
    → QJsonDocument::fromJson()              // parse UTF-8 JSON text → QJsonObject
    → flattenJsonObject(nested object)       // nested QJsonObject → flat QHash<QString,QVariant>
    → m_values.clear() + insert all entries  // full replacement (no merge)
    → for each value: schema validation via SettingSchema::isValid()
      → valid: keep in m_values, emit settingChanged(key, value)
      → invalid: qWarning() to stdout, skip that key
    → return true on success, false on parse/file error
  ```

- **New Patterns:** JSON serialization/deserialization layer bridging flat key-value storage and hierarchical file format. Atomic write pattern via `QSaveFile` ensures no corruption on crash.

## 2. Component & File Impact Map

### [`src/backend/core/settings_manager.hpp`](c:\Development\projects\dir2md-cpp\src\backend\core\settings_manager.hpp)
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add `settingsSaved(const QString &path)` signal to the `signals:` section.
  - [ ] Add `#include <QJsonDocument>` and `#include <QJsonObject>` (if not transitively included).
  - [ ] Add `#include <QStandardPaths>` and `#include <QSaveFile>` for file I/O support.
- **Logic Modifications Required:**
  - [ ] Signal declaration for successful save completion with path parameter.

### [`src/backend/core/settings_manager.cpp`](c:\Development\projects\dir2md-cpp\src\backend\core\settings_manager.cpp)
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add `#include <QJsonDocument>`, `#include <QJsonObject>`, `#include <QJsonArray>`, `#include <QFile>`, `#include <QSaveFile>`, `#include <QStandardPaths>`, `#include <QDebug>` to the includes.
  - [ ] Create an **anonymous namespace** at the top of the file (after existing includes, before `namespace dir2md::backend`) containing two static helper functions:
    - `insertNestedValue(QJsonObject &obj, const QStringList &pathParts, const QVariant &value)` — recursively nests a value at the given path. Splits on `/` only. Creates intermediate `QJsonObject` levels as needed.
    - `flattenJsonObject(const QJsonObject &obj, const QString &prefix, QHash<QString, QVariant> &out)` — recursively traverses nested object, joining path segments with `/`, collecting leaf values into flat hash.
  - [ ] Implement `SettingsManager::save_to_file(const QString &filePath)` method body:
    1. Call `flattenJsonObject` to convert `m_values` → nested `QJsonObject` organized by category.
    2. Wrap in `QJsonDocument`, serialize with `QJsonDocument::Compact` or `QJsonDocument::Indented` (pretty-printed, 2-space indentation).
    3. Write via `QSaveFile` for atomicity; call `commit()` on success.
    4. Emit `settingsSaved(path)` on successful write.
    5. Return `true` on success, `false` on write failure.
  - [ ] Implement `SettingsManager::load_from_file(const QString &filePath)` method body:
    1. Open file with `QFile`; return `false` if open fails (missing/unreadable).
    2. Read contents, parse with `QJsonDocument::fromJson()`; return `false` with `qWarning()` on malformed JSON.
    3. Call `flattenJsonObject` to convert nested JSON → flat `QHash<QString, QVariant>`.
    4. Clear `m_values` entirely (full replacement semantics).
    5. For each key/value pair: validate against registered schema via `SettingSchema::isValid()`.
       - Valid: insert into `m_values`, emit `settingChanged(key, value)`.
       - Invalid: `qWarning()` to stdout, skip that key.
    6. Return `true` on success.
- **Logic Modifications Required:**
  - [ ] `insertNestedValue`: Split path parts on `/` only (not `.`). If a path part has no further nesting needed, set the value directly. Handle `QVariant` → `QJsonValue` conversion for all supported types (int, double, QString, bool).
  - [ ] `flattenJsonObject`: Recurse into nested `QJsonObject`s, building dot-separated prefixes. Only leaf values (non-object) are added to output hash.
  - [ ] `save_to_file`: Group values by `SettingSchema::category` from the schema registry. Keys without a registered schema fall under `"General"`.
  - [ ] `load_from_file`: Silently ignore keys present in JSON but not registered in any schema (no warning, no insertion).

### [`src/backend/core/CMakeLists.txt`](c:\Development\projects\dir2md-cpp\src\backend\core\CMakeLists.txt)
- **Type of Change:** No change required.
- `QJson` and `QStandardPaths` are part of `Qt6::Core`, which is already linked via the parent `CMakeLists.txt`. No additional link dependencies needed.

### [`src/backend/core/core_schema.cpp`](c:\Development\projects\dir2md-cpp\src\backend\core\core_schema.cpp)
- **Type of Change:** No change required.
- Schema definitions remain unchanged. The existing schemas ("General" category for `ToolPath`, "Performance" category for `MaxThreads`) provide the category grouping needed for nested JSON output.

### [`src/backend/core/core_schema.hpp`](c:\Development\projects\dir2md-cpp\src\backend\core\core_schema.hpp)
- **Type of Change:** No change required.

## 3. Boundary & Edge Case Analysis

- **Error Handling:**
  - **Missing file (`load_from_file`):** Return `false`, do not modify `m_values`. No error logged — this is expected behavior (first run, no config yet).
  - **Unreadable file (permissions):** Return `false`, log via `qWarning()`.
  - **Malformed JSON:** Return `false`, log parse error via `qWarning()` with details from `QJsonParseError`.
  - **Invalid value during load:** Skip that key, log warning via `qWarning()` to stdout, continue processing remaining keys.
  - **Write failure (`save_to_file`):** `QSaveFile` rollback on `commit()` failure; return `false`, no signal emitted.

- **Security & Permissions:**
  - No user input is accepted directly from files — all values are validated against registered schemas before insertion into `m_values`.
  - Config file path is user-provided (not auto-resolved for writing); auto-resolution via `QStandardPaths` is only referenced for documentation purposes. The API accepts an explicit `filePath`.

- **Performance / Scale Impact:**
  - Flat ↔ nested conversion is O(n) where n = number of settings keys. With typical config files (<100 keys), performance impact is negligible.
  - No database queries or heavy loops involved.
  - JSON pretty-printing adds minor I/O overhead but is acceptable for infrequent save/load operations (application startup/shutdown).

- **Type Round-Trip Integrity:**
  - `QVariant` → `QJsonValue` conversion must preserve types: `int` stays `int`, `double` stays `double`, `QString` stays `QString`, `bool` stays `bool`.
  - `QJsonValue` → `QVariant` conversion on load must reconstruct the original type. Use `QJsonValue::toVariant()` which handles this automatically for standard Qt types.

- **Separator Semantics:**
  - Only `/` is a hierarchy separator. The `.` character and all other characters (including spaces, commas, etc.) are treated as literal key content.
  - Example: `"file.name"` stays as a single key; `"editor/tab_size"` becomes nested `{ "editor": { "tab_size": ... } }`.

## 4. Verification Checklist

- [ ] Verify that `save_to_file()` produces valid, pretty-printed JSON with categories as top-level keys and settings nested within.
- [ ] Verify that keys containing `.` (e.g., `"file.name"`) remain as single flat keys within their category — not split.
- [ ] Verify that keys containing `/` (e.g., `"editor/tab_size"`) are correctly nested under the category → key hierarchy.
- [ ] Verify that keys without a registered schema are placed under the `"General"` top-level group during save.
- [ ] Verify that `load_from_file()` replaces `m_values` entirely — no leftover values from before the load.
- [ ] Verify that after a full save→load round-trip, all key/value pairs and types are preserved identically.
- [ ] Verify that a missing config file causes `load_from_file()` to return `false` without modifying existing settings.
- [ ] Verify that malformed JSON causes `load_from_file()` to return `false` with an error logged via `qWarning()`.
- [ ] Verify that invalid values (failing `SettingSchema::isValid`) are skipped with a warning, while valid values are still loaded.
- [ ] Verify that keys in JSON but not registered in any schema are silently ignored (no warning, no insertion).
- [ ] Verify that `settingsSaved(const QString &path)` signal is emitted with the correct path after successful save.
- [ ] Verify that `QSaveFile` atomic write works correctly — partial writes on crash do not corrupt the config file.
- [ ] Verify that the anonymous namespace helpers (`insertNestedValue`, `flattenJsonObject`) are not visible outside `settings_manager.cpp`.
