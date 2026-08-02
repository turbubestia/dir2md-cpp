# Implement Settings Save and Load

In the sub-project `src/backend/` we have implemented the class `src\backend\core\settings_manager.hpp` to manage setting application wide. Now we need to implement the `save_to_file` and `load_from_file`. 

## Goals

- Save the flattend slash setting in a expanded json configuration file
- Load an expanded json configuration file into a flattend key/value to the setting manager.
- Implement two helper methods `insertNestedValue` and `flattenJsonObject` to convert from flat to json, and json to flat. This can be place in the `settings_manager.cpp` in an anonymous namespace.
- Implement the save and load using the Qt6 clases 

| Class | Purpose |
|-------|---------|
| QJsonObject | Constructs the in-memory tree/map representation of your settings. |
| QJsonDocument | Wraps QJsonObject and handles parsing/serializing to raw UTF-8 JSON text. |
| QFile / QSaveFile | Performs file reading and safe, atomic writing to disk. |
| QStandardPaths | Finds standard system config directories (~/.config/app/settings.json or %APPDATA%\app\settings.json). |

---

# Refinement Iteration 2
**Status:** LOCKED

## 1. Executive Summary
Incorporates user feedback from Iteration 1 to resolve all open design choices. Key clarifications: `load_from_file()` replaces `m_values` entirely (mirror behavior), only `/` is a JSON hierarchy separator (`.` is literal), config file stores only key-value pairs without schema metadata, invalid values produce a stdout warning and are skipped, and `save_to_file()` emits a `settingsSaved` signal.

## 2. Updated Requirements (Changes from Iteration 1)

### Updated Requirement [REQ-01]: Flatten Settings to Nested JSON
- **Description:** Convert the flat `QHash<QString, QVariant>` value store into a nested `QJsonObject` organized by category. The nesting key is derived from `SettingSchema::category`. Within each category, keys are split on `/` only — the `.` character is treated as a literal character, not a hierarchy separator. For example, `"editor/tab_size"` becomes `{ "editor": { "tab_size": 4 } }`, while `"file.name"` remains a single flat key within its category.
- **Acceptance Criteria:**
  - [ ] Given a flat key `"editor/tab_size"`, When flattened into nested JSON, Then the result is `{ "editor": { "tab_size": <value> } }`.
  - [ ] Given a flat key `"file.name"` (no `/`), When flattened, Then it remains as a single top-level key within its category: `{ "General": { "file.name": <value> } }`.
  - [ ] Keys without an associated schema are placed under a default `"General"` top-level group.

### Updated Requirement [REQ-02]: Unflatten Nested JSON to Flat Settings
- **Description:** Convert a nested `QJsonObject` back into the flat key/value representation. Recursively traverse the JSON tree, joining path segments with `/` to reconstruct flat keys. The `.` character is NOT treated as a separator — it appears literally in reconstructed keys.
- **Acceptance Criteria:**
  - [ ] Given nested JSON `{ "editor": { "tab_size": 4 } }`, When unflattened, Then the result contains key `"editor/tab_size"` with value `4`.
  - [ ] Given nested JSON `{ "General": { "file.name": "test.txt" } }`, When unflattened, Then the result contains key `"file.name"` (literal dot, no splitting).
  - [ ] Values retain their original types (int, double, QString, bool) through the round-trip conversion.

### Updated Requirement [REQ-04]: Load Settings from File
- **Description:** Read a JSON configuration file from disk, parse it into a `QJsonObject`, unflatten it to flat key/value pairs, and **replace** the `SettingsManager`'s `m_values` entirely (mirror behavior — after load, `m_values` contains exactly the keys/values from the file). The load operation validates each value against registered schemas. Invalid values produce a warning via `qWarning()` (stdout) and are skipped; processing continues with remaining values. Keys in the JSON but not registered in the schema are silently ignored.
- **Acceptance Criteria:**
  - [ ] Given a valid JSON config file, When `load_from_file()` is called, Then `m_values` is replaced entirely with the deserialized key/value pairs from the file.
  - [ ] After load, `m_values` contains exactly the keys present in the file — no leftover values from before the load.
  - [ ] Given a missing or unreadable file, When `load_from_file()` is called, Then it returns `false` without modifying existing settings.
  - [ ] Given a malformed JSON file, When `load_from_file()` is called, Then it returns `false` and logs an error via `qWarning`.
  - [ ] When a loaded value fails schema validation (`SettingSchema::isValid` returns false), Then a warning is printed to stdout and that specific value is skipped; other valid values are still loaded.
  - [ ] Keys present in the JSON but not registered in any schema are silently ignored.

### Updated Requirement [REQ-06]: Helper Methods & Save Signal
- **Description:** Implement `insertNestedValue` and `flattenJsonObject` as static functions within an anonymous namespace in `settings_manager.cpp`. Additionally, `save_to_file()` must emit a signal `settingsSaved(const QString &path)` upon successful write.
- **Acceptance Criteria:**
  - [ ] Both helper functions are declared inside an anonymous namespace in `settings_manager.cpp` only.
  - [ ] `insertNestedValue(QJsonObject &obj, const QStringList &pathParts, const QVariant &value)` correctly nests a value at the given path (splitting on `/` only).
  - [ ] `flattenJsonObject(const QJsonObject &obj, QString prefix, QHash<QString, QVariant> &out)` recursively flattens a nested object into a flat hash.
  - [ ] When `save_to_file()` completes successfully, Then the signal `settingsSaved(const QString &path)` is emitted with the path that was written.

## 3. Updated Scope & Constraints
- **In-Scope:**
  - Flat ↔ nested JSON conversion helpers (`insertNestedValue`, `flattenJsonObject`) — `/` only as hierarchy separator
  - `save_to_file(const QString &filePath)` implementation with `settingsSaved` signal emission
  - `load_from_file(const QString &filePath)` implementation with full `m_values` replacement
  - Default config path resolution via `QStandardPaths` → `%APPDATA%\dir2md\settings.json` / `~/.config/dir2md/settings.json`
  - Atomic file writing with `QSaveFile`
  - Schema-aware validation during load (invalid values: warn to stdout, skip, continue)
  - Config file stores only key-value pairs — no schema metadata persisted
- **Out-of-Scope:**
  - UI settings editor or preferences dialog (frontend concern)
  - Migration from older config file formats
  - Hot-reload of settings on file change (no filesystem watcher)
  - Encryption or obfuscation of stored values
  - Schema registration persistence (schemas are code-defined, not stored in JSON)
- **Technical Constraints / Edge Cases:**
  - Only `/` is treated as a hierarchy separator in flat keys; `.` and all other characters are literal.
  - `load_from_file()` replaces `m_values` entirely — no merge behavior.
  - `QSaveFile` commit must be called explicitly after writing to ensure atomicity.
  - Thread safety is not required (single-threaded settings access assumed).
  - JSON output uses UTF-8 encoding with pretty-print formatting (2-space or 4-space indentation).
  - Config file format: top-level object with category keys, each containing nested key-value pairs. No schema metadata included.

## 4. Open Design Choices
*None — all design choices from Iteration 1 have been resolved.*

---

**LOCKED**
