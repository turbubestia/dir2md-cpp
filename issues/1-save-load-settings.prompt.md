# Implementation Plan: 1-save-load-settings

[Analysis Reference](./1-save-load-settings.plan.analysis.md)

---

## Overview

This plan implements `save_to_file()` and `load_from_file()` on `SettingsManager`, enabling persistent JSON configuration storage with atomic writes, schema validation, and full type round-trip integrity. The methods are already declared in the public header but have no implementation body.

**Scope:** Backend Core only (`src/backend/core/`). No frontend, CLI, or schema changes.

---

## Phase 1 — Anonymous-Namespace Serialization Helpers

**References:** Analysis §2 (Component & File Impact Map → `settings_manager.cpp`), §3 (Separator Semantics, Type Round-Trip Integrity)

**Goal:** Implement two static helper functions in an anonymous namespace so they are invisible outside `settings_manager.cpp`.

### Step 1.1 — Add Required Includes

Add the following includes to `src/backend/core/settings_manager.cpp`, after the existing `#include "settings_manager.hpp"`:

```cpp
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QDebug>
```

**Exit Criterion:** All new includes compile without errors. No other files modified in this step.

### Step 1.2 — Implement `insertNestedValue` (Anonymous Namespace)

Add to the anonymous namespace at the top of `settings_manager.cpp`:

```cpp
namespace {

void insertNestedValue(QJsonObject &obj, const QStringList &pathParts, const QVariant &value)
{
    if (pathParts.isEmpty()) {
        return;
    }

    if (pathParts.size() == 1) {
        // Leaf: set the value directly.
        obj.insert(pathParts.first(), QJsonValue::fromVariant(value));
        return;
    }

    // Intermediate level: ensure a QJsonObject exists at this path segment.
    QString key = pathParts.first();
    if (!obj.contains(key) || !obj[key].isObject()) {
        obj.insert(key, QJsonObject{});
    }

    // Recurse into the nested object with remaining path parts.
    QStringList rest = pathParts;
    rest.removeFirst();
    insertNestedValue(obj[key].toObject(), rest, value);
}

} // anonymous namespace
```

**Logic details:**
- Splits on `/` only — `.` and all other characters are literal key content (Analysis §3).
- Creates intermediate `QJsonObject` levels as needed.
- Uses `QJsonValue::fromVariant()` for type-preserving conversion (Analysis §3, Type Round-Trip Integrity).

**Exit Criterion:** Function compiles. No runtime test yet — validation in Phase 4.

### Step 1.3 — Implement `flattenJsonObject` (Anonymous Namespace)

Add to the same anonymous namespace:

```cpp
void flattenJsonObject(const QJsonObject &obj, const QString &prefix, QHash<QString, QVariant> &out)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        QString key = it.key();
        QString fullKey = prefix.isEmpty() ? key : prefix + "/" + key;

        if (it.value().isObject()) {
            // Recurse into nested object.
            flattenJsonObject(it.value().toObject(), fullKey, out);
        }
        else {
            // Leaf value: collect into flat hash.
            out.insert(fullKey, it.value().toVariant());
        }
    }
}
```

**Logic details:**
- Recursively traverses nested `QJsonObject`s (Analysis §2).
- Joins path segments with `/` — the same separator used during save (Analysis §3).
- Only leaf values (non-object) are added to output hash.

**Exit Criterion:** Both helpers compile within the anonymous namespace. No external visibility.

---

## Phase 2 — Implement `save_to_file()`

**References:** Analysis §2 (`settings_manager.cpp` → `save_to_file`), §3 (Error Handling — Write Failure)

**Goal:** Serialize in-memory settings to a pretty-printed JSON file with atomic write via `QSaveFile`.

### Step 2.1 — Implement `save_to_file` Method Body

Add the implementation after the existing methods in `settings_manager.cpp`:

```cpp
auto SettingsManager::save_to_file(const QString &filePath) -> bool
{
    // 1. Group values by category from schema registry.
    QHash<QString, QJsonObject> grouped;  // category → nested object

    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
        const QString &key = it.key();
        const QVariant &value = it.value();

        // Determine category: use schema if registered, otherwise "General".
        QString category;
        if (m_schemaRegistry.contains(key)) {
            category = m_schemaRegistry.value(key).category;
        }
        else {
            category = "General";
        }

        // Split key on "/" for nesting.
        QStringList pathParts = key.split('/', Qt::KeepEmptyParts);
        insertNestedValue(grouped[category], pathParts, value);
    }

    // 2. Build top-level nested object from grouped categories.
    QJsonObject topLevel;
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        topLevel.insert(it.key(), it.value());
    }

    // 3. Serialize to pretty-printed JSON (2-space indentation).
    QJsonDocument doc(topLevel);
    QByteArray json = doc.toJson(QJsonDocument::Indented);

    // 4. Atomic write via QSaveFile.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "save_to_file: cannot open" << filePath << ":" << file.errorString();
        return false;
    }

    file.write(json);

    if (!file.commit()) {
        qWarning() << "save_to_file: commit failed for" << filePath << ":" << file.errorString();
        return false;
    }

    // 5. Emit signal on success.
    emit settingsSaved(filePath);

    return true;
}
```

**Logic details:**
- Groups values by `SettingSchema::category` from the registry (Analysis §2).
- Keys without a registered schema fall under `"General"` (Analysis §3, Verification Checklist item 4).
- Uses `QSaveFile::commit()` for atomicity — if commit fails, file is rolled back automatically (Analysis §3).
- Emits `settingsSaved(filePath)` only on success (Analysis §2).

**Exit Criterion:** Compiles. No signal handler connected yet — validation in Phase 4.

### Step 2.2 — Add `settingsSaved` Signal to Header

In `src/backend/core/settings_manager.hpp`, add the signal declaration inside the `signals:` section:

```cpp
    void settingsSaved(const QString &path);
```

**Exit Criterion:** Signal is declared in the header, matching the emit in the .cpp.

---

## Phase 3 — Implement `load_from_file()`

**References:** Analysis §2 (`settings_manager.cpp` → `load_from_file`), §3 (Error Handling — Missing File, Malformed JSON, Invalid Value; Security & Permissions)

**Goal:** Load settings from a JSON file with full schema validation, full replacement semantics, and proper error handling.

### Step 3.1 — Implement `load_from_file` Method Body

Add the implementation after `save_to_file` in `settings_manager.cpp`:

```cpp
auto SettingsManager::load_from_file(const QString &filePath) -> bool
{
    // 1. Open file; return false if missing/unreadable (no error logged for missing — expected on first run).
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        // Missing file is not an error — just return false without modifying m_values.
        if (file.exists()) {
            qWarning() << "load_from_file: cannot read" << filePath << ":" << file.errorString();
        }
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    // 2. Parse JSON; return false with error on malformed JSON.
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull() || parseError.error != QJsonParseError::NoError) {
        qWarning() << "load_from_file: malformed JSON in" << filePath
                   << ":" << parseError.errorString();
        return false;
    }

    // 3. Convert nested JSON → flat QHash<QString, QVariant>.
    QJsonObject rootObj = doc.object();
    QHash<QString, QVariant> flatValues;
    for (auto it = rootObj.begin(); it != rootObj.end(); ++it) {
        QString category = it.key();
        const QJsonObject &catObj = it.value().toObject();
        flattenJsonObject(catObj, category, flatValues);
    }

    // 4. Clear m_values entirely (full replacement semantics).
    m_values.clear();

    // 5. Validate each key/value against registered schema.
    for (auto it = flatValues.constBegin(); it != flatValues.constEnd(); ++it) {
        const QString &key = it.key();
        const QVariant &value = it.value();

        // Silently ignore keys not registered in any schema (no warning, no insertion).
        if (!m_schemaRegistry.contains(key)) {
            continue;
        }

        // Validate against schema.
        const SettingSchema &schema = m_schemaRegistry.value(key);
        if (!schema.isValid(value)) {
            qWarning() << "load_from_file: invalid value for key" << key
                       << "(type:" << value.typeName() << "), skipping.";
            continue;
        }

        // Valid: insert and emit.
        m_values.insert(key, value);
        emit settingChanged(key, value);
    }

    return true;
}
```

**Logic details:**
- **Missing file:** Returns `false`, does NOT modify `m_values`, no error logged (Analysis §3). This is expected on first run.
- **Unreadable file (permissions):** Returns `false`, logs via `qWarning()` (Analysis §3).
- **Malformed JSON:** Returns `false`, logs parse error details via `qWarning()` with `QJsonParseError` info (Analysis §3).
- **Invalid value during load:** Skips that key, logs warning to stdout, continues processing remaining keys (Analysis §3).
- **Keys not in schema:** Silently ignored — no warning, no insertion (Analysis §2, Verification Checklist item 10).
- **Full replacement:** `m_values.clear()` before inserting loaded values (Analysis §2, Verification Checklist item 5).

**Exit Criterion:** Compiles. All error paths return `false` as specified.

---

## Phase 4 — Test Infrastructure Setup

**References:** Analysis §4 (Verification Checklist), Prompt Instructions §3 (Test and Coverage)

**Goal:** Create Qt Test-based unit tests with CMake integration, enabling coverage reporting.

### Step 4.1 — Create `test/CMakeLists.txt`

Create the file `test/CMakeLists.txt`:

```cmake
# Enable testing
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(dir2md_settings_test
    settings_manager_test.cpp
)

target_link_libraries(dir2md_settings_test PRIVATE
    dir2md_backend
    Qt6::Test
)

set_target_properties(dir2md_settings_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Register as a CTest test
add_test(NAME dir2md_settings_test COMMAND dir2md_settings_test)
```

### Step 4.2 — Update Root `CMakeLists.txt` to Include Test Subdirectory

In the root `CMakeLists.txt`, after `add_subdirectory(src)`, add:

```cmake
add_subdirectory(test)
```

**Exit Criterion:** `cmake --preset debug` succeeds and discovers the test target. Verify with:
```bash
cmake --build --preset debug --target dir2md_settings_test
```

---

## Phase 5 — Unit Tests

**References:** Analysis §4 (all 13 verification checklist items), Prompt Instructions §3

**Goal:** Implement comprehensive unit tests covering every verification checklist item.

### Step 5.1 — Create `test/settings_manager_test.cpp`

Create the file `test/settings_manager_test.cpp` with the following test classes and methods:

#### Test Class: `TestSaveToFile`

| Test Method | Verification Checklist Item | Description |
|---|---|---|
| `saveProducesValidJson()` | #1 | Save settings, read back file, parse as JSON — verify structure has category top-level keys with nested settings. |
| `saveKeysWithDotNotSplit()` | #2 | Set a key containing `.` (e.g., `"core/file.name"`), save, verify the key remains flat within its category — not split on `.`. |
| `saveKeysWithSlashNested()` | #3 | Set a key containing `/` (e.g., `"editor/tab_size"` in "Editor" category), save, verify correct nesting: `{ "Editor": { "tab_size": 4 } }`. |
| `saveUnregisteredKeyUnderGeneral()` | #4 | Set a key with no registered schema, save, verify it appears under the `"General"` top-level group. |

#### Test Class: `TestLoadFromFile`

| Test Method | Verification Checklist Item | Description |
|---|---|---|
| `loadReplacesAllValues()` | #5 | Set some values, load from file (with different values), verify old values are gone — full replacement. |
| `missingFileReturnsFalseNoModify()` | #7 | Call `load_from_file()` on a non-existent path, verify it returns `false` and `m_values` is unchanged. |
| `malformedJsonReturnsFalse()` | #8 | Write invalid JSON to a file, call `load_from_file()`, verify it returns `false` and logs an error. |
| `invalidValueSkippedWithWarning()` | #9 | Write a JSON with one valid and one invalid value (fails schema validation), verify only the valid one is loaded. |
| `unregisteredKeySilentlyIgnored()` | #10 | Write a JSON containing a key not in any schema, verify it is silently ignored — no warning, no insertion. |

#### Test Class: `TestRoundTrip`

| Test Method | Verification Checklist Item | Description |
|---|---|---|
| `saveLoadRoundTripPreservesValues()` | #6 | Set multiple values of different types (int, double, QString, bool), save to file, load from same file, verify all key/value pairs and types are identical. |

#### Test Class: `TestSettingsSavedSignal`

| Test Method | Verification Checklist Item | Description |
|---|---|---|
| `settingsSavedEmittedOnSuccess()` | #11 | Connect a slot to `settingsSaved()`, call `save_to_file()`, verify the signal fires with the correct path. |

#### Test Class: `TestSerializationHelpers`

| Test Method | Verification Checklist Item | Description |
|---|---|---|
| `insertNestedValueCreatesHierarchy()` | #13 (indirect) | Directly test `insertNestedValue` to verify it creates nested `QJsonObject` structure from slash-separated path. |
| `flattenJsonObjectReconstructsFlatKeys()` | #13 (indirect) | Directly test `flattenJsonObject` to verify it reconstructs flat keys with `/` separators from nested object. |

**Note:** Since the helpers are in an anonymous namespace, they cannot be tested directly from outside the translation unit. Instead, test them indirectly through `save_to_file()` and `load_from_file()` — which is actually more robust as it tests the full integration path. Remove the two direct helper tests above; replace with integration-level coverage.

### Step 5.2 — Test File Structure (Pseudocode)

```cpp
#include <QObject>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <backend/core/settings_manager.hpp>
#include <backend/core/core_schema.hpp>

class TestSaveToFile : public QObject {
    Q_OBJECT
private slots:
    void saveProducesValidJson();
    void saveKeysWithDotNotSplit();
    void saveKeysWithSlashNested();
    void saveUnregisteredKeyUnderGeneral();
};

class TestLoadFromFile : public QObject {
    Q_OBJECT
private slots:
    void loadReplacesAllValues();
    void missingFileReturnsFalseNoModify();
    void malformedJsonReturnsFalse();
    void invalidValueSkippedWithWarning();
    void unregisteredKeySilentlyIgnored();
};

class TestRoundTrip : public QObject {
    Q_OBJECT
private slots:
    void saveLoadRoundTripPreservesValues();
};

class TestSettingsSavedSignal : public QObject {
    Q_OBJECT
private slots:
    void settingsSavedEmittedOnSuccess();
};
```

Each test method should:
1. Create a `QTemporaryFile` for isolated file I/O (no pollution of real filesystem).
2. Set up a `SettingsManager`, register schemas via `CoreSchema::registerSchemas()`.
3. Perform the operation (save/load).
4. Assert expectations using Qt Test macros (`QCOMPARE`, `QVERIFY`, `QVERIFY_NOT_NULL`).

**Exit Criterion:** All tests pass with:
```bash
cmake --build --preset debug --target dir2md_settings_test
ctest --preset debug --output-on-failure
```

---

## Phase 6 — Coverage Validation

**References:** Prompt Instructions §3 (Test and Coverage), Analysis §4 (Verification Checklist)

**Goal:** Run tests with coverage instrumentation and verify adequate test coverage.

### Step 6.1 — Build with Coverage

```bash
cmake --preset debug-coverage
cmake --build --preset debug --target dir2md_settings_test
```

### Step 6.2 — Run Tests with Coverage

```bash
ctest --preset debug --output-on-failure
```

This will produce `.profraw` files in the build directory.

### Step 6.3 — Merge and Report Coverage

```bash
llvm-profdata merge -o default.profdata *.profraw
llvm-cov show build/cmake-debug-coverage/test/dir2md_settings_test.exe -instr-profile=default.profdata
```

**Exit Criterion:** Coverage report shows ≥80% line coverage for `settings_manager.cpp` and `core_schema.cpp`. All tests pass.

---

## Phase 7 — Final Integration Verification

**References:** Analysis §4 (all verification checklist items), Prompt Instructions §1 (Clarifying Code vs. Pseudocode)

**Goal:** End-to-end verification that the implementation is complete, correct, and buildable.

### Step 7.1 — Full Build Verification

```bash
cmake --preset debug
cmake --build --preset debug --target all
```

**Exit Criterion:** Clean build with zero warnings related to the new code. No errors.

### Step 7.2 — Run All Tests

```bash
ctest --preset debug --output-on-failure
```

**Exit Criterion:** All tests pass, including the new `dir2md_settings_test`.

### Step 7.3 — Verify Anonymous Namespace Encapsulation

Confirm that `insertNestedValue` and `flattenJsonObject` are not visible outside `settings_manager.cpp`:

```bash
grep -n "insertNestedValue\|flattenJsonObject" src/backend/core/settings_manager.hpp
```

**Exit Criterion:** No matches in the header file. Both functions exist only in the anonymous namespace of the .cpp file.

### Step 7.4 — Code Convention Check

- All new code uses **snake_case** for variables, methods, classes, namespaces.
- All functions use **trailing return types**.
- No `[[nodiscard]]` decorators added anywhere.

**Exit Criterion:** Manual review confirms compliance.

---

## Summary of Files to Modify/Create

| File | Action | Phase |
|---|---|---|
| `src/backend/core/settings_manager.cpp` | Modify — add includes, anonymous namespace helpers, `save_to_file()`, `load_from_file()` | 1, 2, 3 |
| `src/backend/core/settings_manager.hpp` | Modify — add `settingsSaved` signal | 2.2 |
| `test/CMakeLists.txt` | Create — test target definition | 4.1 |
| `CMakeLists.txt` (root) | Modify — add `add_subdirectory(test)` | 4.2 |
| `test/settings_manager_test.cpp` | Create — comprehensive unit tests | 5 |

**No changes required:** `core_schema.cpp`, `core_schema.hpp`, `src/backend/core/CMakeLists.txt`.

---

## Execution Order

```
Phase 1 (Helpers) → Phase 2 (save_to_file) → Phase 3 (load_from_file)
    → Phase 4 (Test Infra) → Phase 5 (Unit Tests) → Phase 6 (Coverage) → Phase 7 (Final Verification)
```

Each phase must pass its exit criterion before proceeding to the next. Do not skip phases or combine them.
