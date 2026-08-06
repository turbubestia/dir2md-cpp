# SettingsManager

## Source Files

- `src/backend/core/settings_manager.hpp` — Public header: class declaration, `SettingSchema` struct.
- `src/backend/core/settings_manager.cpp` — Implementation: accessors, schema registration, file I/O.

**Counterpart status:** Both files present and analyzed as a pair.

---

## Purpose and Role

`SettingsManager` is the central configuration store for the dir2md application. It provides:

1. **Schema-registered settings** — every writable key must be registered via `SettingSchema`, which declares type, default value, constraints (min/max/enum), category, and human-readable metadata.
2. **Type-safe accessors** — `get()` returns the active user value or falls back to schema default; `set()` validates against schema before storing.
3. **Hierarchical JSON persistence** — flat key paths (`"editor/tab_size"`) are serialized to nested JSON grouped by category, and deserialized back with case-insensitive category matching.
4. **Signal-based change notification** — emits `settingChanged` on mutations and `settingsSaved` on successful file writes.

The class is designed as a shared backend component consumed by both the QtQuick frontend and the CLI.

---

## Major Structures

### `SettingSchema`

| Field | Type | Description |
|---|---|---|
| `key` | `QString` | Flat dotted path, e.g., `"editor/tab_size"` |
| `title` | `QString` | Human-readable label |
| `description` | `QString` | Longer explanation |
| `category` | `QString` | Grouping for JSON serialization (auto-derived from key prefix if empty) |
| `defaultValue` | `QVariant` | Fallback when no user value is set |
| `type` | `QMetaType` | Expected QMetaType for type coercion and validation |
| `min` / `max` | `QVariant` | Numeric bounds (only checked for `int` and `double`) |
| `enumOptions` | `QStringList` | Allowed string values for enum-like settings |

### `SettingsManager`

| Member | Role |
|---|---|
| `m_values` | Flat `QHash<QString, QVariant>` of user-set values (O(1) lookup) |
| `m_schemaRegistry` | Flat `QHash<QString, SettingSchema>` of registered schemas (O(1) lookup) |

---

## Public API

### Accessors

- **`get(key)`** → `QVariant`  
  Returns user value if present; otherwise schema default; otherwise invalid `QVariant`.

- **`set(key, value)`** → `bool`  
  Validates key syntax, schema registration, type conversion, and constraints. Stores the canonicalized value and emits `settingChanged` only on actual change. Returns `false` on any validation failure.

### Schema Management

- **`registerSchema(schema)`** → `void`  
  Registers or replaces a schema. Auto-fills empty category from key prefix. Rejects schemas whose category doesn't match the key prefix (normalized comparison). If replacing, invalidates the active value if it no longer satisfies the new schema. Throws `std::runtime_error` via `RUNTIME_ASSERT` if key syntax is invalid.

- **`schema(key)`** → `std::optional<SettingSchema>`  
  Lookup by key.

- **`schemas()`** → `const QHash<QString, SettingSchema> &`  
  Full registry reference.

### State Inspection

- **`activeValues()`** → `const QHash<QString, QVariant> &`  
  Reference to the flat user-value store.

### Persistence

- **`save_to_file(filePath)`** → `bool`  
  Resolves path (security-checked), groups values by category into nested JSON, writes atomically via `QSaveFile`. Emits `settingsSaved` on success.

- **`load_from_file(filePath)`** → `bool`  
  Resolves path, parses JSON, flattens nested structure, matches flat keys to registered schemas via case-insensitive category comparison, stages accepted values in a candidate map, then atomically replaces `m_values`. Emits `settingChanged` for added, changed, and removed keys.

---

## Internal Helpers (Anonymous Namespace)

| Function | Purpose |
|---|---|
| `isValidKeySyntax(key)` | Rejects empty, whitespace-containing, leading/trailing `/`, or repeated `//` keys |
| `isWithinPath(path, root)` | Case-insensitive check that `path` is under `root` after normalization |
| `resolvePersistencePath(filePath)` | Security sandbox: in production, rejects paths with `/`, `\`, or absolute paths and resolves to `~/.config/dir2md/<filePath>`. In debug test mode (`DIR2MD_DEBUG_TEST_PATH`), allows home/temp directory paths |
| `insertNestedValue(obj, pathParts, value)` | Rebuilds nested `QJsonObject` from a flat path (leaf-up reconstruction) |
| `flattenJsonObject(obj, prefix, out)` | Iterative stack-based flattening of nested JSON to flat key paths |
| `toDisplayFormat(normalized)` | `"tab-size"` → `"Tab Size"` (split on `-`, title-case each segment) |
| `toNormalizedFormat(display)` | `"Tab Size"` → `"tab-size"` (split on whitespace, lowercase, join with `-`) |
| `convertToSchemaType(schema, value)` | Attempts type coercion; returns `std::optional<QVariant>` |

---

## Control Flow & State Transitions

### `set()` Validation Pipeline
```
key syntax check → schema exists? → schema.isValid(value)? → convertToSchemaType() → store if changed → emit signal
```
Any step returning false aborts with no side effects.

### `load_from_file()` Atomic Commit Pattern
1. Parse JSON → flatten to flat keys.
2. For each flat key, find matching schema (case-insensitive category + path suffix match).
3. Type-coerce and stage in `candidateValues`.
4. Compare `candidateValues` against current `m_values` to classify keys as added/changed/removed.
5. **Atomically replace** `m_values = candidateValues`.
6. Emit signals for all changed keys.

This pattern ensures that a partial parse failure never corrupts in-memory state — the swap happens only after all values are staged.

### `save_to_file()` Atomic Write
Uses `QSaveFile` to write to a temporary file first, then atomically rename on commit. Prevents corruption from power loss or crash mid-write.

---

## Ownership, Lifetime & Thread Safety

- **QObject subclass** with standard Qt parent-child ownership. No custom destructor — relies on Qt's object tree cleanup.
- **No explicit thread-safety mechanisms.** `QHash` is not thread-safe for concurrent read/write. If accessed from multiple threads (e.g., UI thread + background worker), external synchronization is required.
- **Signals** are emitted synchronously during `set()` and `load_from_file()`. Connectors should be aware that slot execution happens on the calling thread (unless using `Qt::QueuedConnection`).

---

## Input Validation & Error Handling

| Entry Point | Validation | Failure Mode |
|---|---|---|
| `set()` | Key syntax, schema registration, type conversion, min/max/enum constraints | Returns `false`, no state change |
| `registerSchema()` | Key syntax (`RUNTIME_ASSERT` → throws), category-prefix consistency | Throws on bad key; returns early (no-op) on category mismatch with warning |
| `save_to_file()` | Path resolution (security sandbox), empty-category assertion, directory creation, file I/O | Returns `false` with `qWarning()`, or throws via `RUNTIME_ASSERT` if schema has empty category |
| `load_from_file()` | Path resolution, JSON parse, top-level object check, schema matching | Returns `false` silently (missing file) or with `qWarning()` (parse error). Unknown keys are skipped. |

---

## Contextual Dependencies

- **`assert.hpp`** — provides `RUNTIME_ASSERT` (throws `std::runtime_error`) used for invariant enforcement in `registerSchema()` and `save_to_file()`.
- **Qt Core** — `QObject`, `QHash`, `QString`, `QVariant`, `QMetaType`, `QJsonDocument`, `QSaveFile`, signals/slots.
- **`DIR2MD_DEBUG_TEST_PATH` macro** — compile-time flag that relaxes path sandboxing for tests. Absent in production builds.

---

## Static Analysis and Security

### 1. Path Traversal in `resolvePersistencePath` (Debug Mode)

**Evidence:** When `DIR2MD_DEBUG_TEST_PATH` is defined, the function accepts any absolute path within the user's home directory or temp directory after `QDir::cleanPath` normalization.

**Risk:** `QDir::cleanPath` removes `.` and `..` but does not resolve symlinks. A symlink inside `~/.config/dir2md/` pointing to a sensitive location could be exploited if an attacker controls the filesystem.

**Impact:** Moderate — only active in debug/test builds, but a test binary with this flag compiled could write settings to arbitrary locations within the home directory.

**Mitigation:** Add `QFileInfo(resolvedPath).canonicalFilePath()` comparison against the canonical root to detect symlink escapes. Alternatively, restrict debug mode to a known test subdirectory rather than the entire home path.

**Follow-up test:** Create a symlink from a temp file to `~/.ssh/id_rsa` and verify that `resolvePersistencePath` rejects it or resolves to a safe path.

### 2. Case-Insensitive Path Comparison on Windows

**Evidence:** `isWithinPath` uses `Qt::CaseInsensitive` for `startsWith` comparison after normalization. On Windows this is correct, but the same binary cross-compiled to Linux would still use case-insensitive comparison, weakening the sandbox.

**Risk:** On case-sensitive filesystems (Linux), `~/.config/dir2md/../../etc/passwd` could pass a case-insensitive check if the normalized root happens to share a prefix with a crafted path.

**Impact:** Low — the primary target platform is Windows where this is correct behavior, but portability is weakened.

**Mitigation:** Use `Qt::CaseSensitive` on non-Windows platforms via preprocessor guard, or use canonical path comparison instead of prefix matching.

**Follow-up test:** On a Linux CI runner, verify that `isWithinPath` correctly rejects paths outside the config directory with mixed-case components.

### 3. `RUNTIME_ASSERT` Throws `std::runtime_error` in Qt Context

**Evidence:** `registerSchema()` calls `RUNTIME_ASSERT(isValidKeySyntax(schema.key))` and `save_to_file()` calls `RUNTIME_ASSERT(false)` when a schema has an empty category. Both throw `std::runtime_error`.

**Risk:** `SettingsManager` is a `QObject` — Qt code typically uses return codes, signals, or `QWarning` for error reporting rather than C++ exceptions. A caller in QML (via `Q_INVOKABLE` or property binding) cannot catch `std::runtime_error`, and the exception will propagate through the Qt event loop potentially crashing the application.

**Impact:** High — uncaught exception in a Qt event loop is undefined behavior and typically results in process termination.

**Mitigation:** Replace `RUNTIME_ASSERT` with `qWarning()` + early return in public-facing methods, or add `Q_INVOKABLE` wrappers that catch exceptions and return error codes. Reserve exceptions for internal invariants only (not validation of caller input).

**Follow-up test:** Call `registerSchema()` with an invalid key from a QML context and verify the application doesn't crash.

### 4. Signal Emission During Container Iteration in `load_from_file`

**Evidence:** After atomically replacing `m_values`, the method iterates over `changedKeys`, `addedKeys`, and `removedKeys` lists and emits `settingChanged`. The signals are emitted _after_ the swap, so the container is stable during emission.

**Risk:** Low — the atomic commit pattern (stage → swap → emit) is correct. However, if a slot connected to `settingChanged` calls `set()` or `load_from_file()` recursively, re-entrancy could cause unexpected behavior.

**Impact:** Low — re-entrant calls would operate on the already-updated state, which may produce inconsistent signal ordering but not memory corruption.

**Mitigation:** Document that slots connected to `settingChanged` should not call `set()` or `load_from_file()`. Alternatively, use `Qt::QueuedConnection` for file-load signals to defer slot execution.

**Follow-up test:** Connect a slot to `settingChanged` that calls `set()` on another key during `load_from_file()` and verify signal ordering is deterministic.

### 5. Schema Replacement Invalidates Active Value Without Explicit Error Return

**Evidence:** `registerSchema()` replaces an existing schema silently. If the active value fails validation against the new schema, it is removed from `m_values` and a `settingChanged` signal is emitted with the default value — but the caller has no way to know this happened (no return value indicating invalidation).

**Risk:** A caller registering updated schemas may not notice that user settings were silently reset to defaults.

**Impact:** Medium — data loss of user preferences without explicit notification.

**Mitigation:** Add a separate signal like `settingInvalidated(key, oldValue, reason)` or return a status from `registerSchema()` indicating whether invalidation occurred.

**Follow-up test:** Register a schema with an active value, then re-register with tighter constraints that invalidate the value; verify the caller is notified of the reset.

### 6. O(N²) Schema Matching in `load_from_file`

**Evidence:** For each loaded flat key, the code iterates over all registered schemas (`m_schemaRegistry.cbegin()` to `cend()`) to find a match by normalized category comparison and path suffix equality.

**Risk:** Performance degradation when loading files with many keys against a large schema registry. Complexity is O(loaded_keys × registered_schemas).

**Impact:** Low for typical usage (tens of settings), but could become noticeable with hundreds of settings or frequent reloads.

**Mitigation:** Build a reverse index from `(normalized_category, path_suffix)` → `schema_key` during registration, enabling O(1) lookup during load.

**Follow-up test:** Benchmark `load_from_file()` with 500+ registered schemas and a JSON file with 300+ keys.

### 7. No File Locking for Concurrent Access

**Evidence:** `save_to_file` uses `QSaveFile` for atomic writes but does not acquire any file lock. `load_from_file` opens the file read-only without locking.

**Risk:** If two instances of the application (or a background sync process) write simultaneously, the last writer wins with no conflict detection.

**Impact:** Low — typical single-instance desktop application. Risk increases if multi-instance or cloud-sync scenarios are added.

**Mitigation:** Add `QLockFile` before write operations to enforce single-writer semantics.

**Follow-up test:** Start two instances that both call `save_to_file()` simultaneously and verify only one succeeds or the writes don't corrupt each other.

### 8. Unchecked `QSaveFile::write` Return Value Significance

**Evidence:** `save_to_file` checks `saveFile.write(jsonBytes) < 0` but `QIODevice::write` returns the number of bytes written, which can be less than requested on partial writes (though unlikely for local files).

**Risk:** Partial write followed by commit could produce a truncated JSON file that fails to load later.

**Impact:** Low — `QSaveFile` on local filesystem typically writes all or nothing, but the check should compare against `jsonBytes.size()` rather than just `< 0`.

**Mitigation:** Change check to `saveFile.write(jsonBytes) != jsonBytes.size()`.

**Follow-up test:** Mock a `QSaveFile` subclass that returns a partial write count and verify the error path is taken.

---

## Summary of Residual Risks

| Category | Risk Level | Notes |
|---|---|---|
| Exception safety in Qt context | **High** | `RUNTIME_ASSERT` throws through QObject event loop |
| Silent data loss on schema replacement | **Medium** | Active values reset to defaults without explicit notification |
| Path traversal (debug mode) | **Medium** | Symlink escape possible in test builds |
| Cross-platform path comparison | **Low** | Case-insensitive check on Linux weakens sandbox |
| Re-entrant signal handlers | **Low** | Slots calling `set()` during load may cause inconsistent state |
| O(N²) schema matching | **Low** | Acceptable for current scale, degrades with growth |
| No file locking | **Low** | Single-instance assumption; risk if multi-instance added |
| Partial write check | **Low** | Unlikely on local filesystem but technically incorrect |
