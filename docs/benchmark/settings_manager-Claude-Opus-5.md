# `settings_manager` — Internal Source Documentation

**Analyzed pair**

- `src/backend/core/settings_manager.hpp`
- `src/backend/core/settings_manager.cpp`

Both members of the pair exist; the counterpart was found and analyzed. Analysis is grounded solely in these two files. Qt API semantics (`QVariant`, `QJson*`, `QSaveFile`) are treated as contextual assumptions where noted.

---

## 1. Purpose and Role

`settings_manager` is the schema-driven settings store for the `dir2md` backend library. It is a pure `QtCore` component (no widgets, no QML) intended to be shared by both the QtQuick frontend and the CLI.

It provides three things:

1. A **schema registry** (`SettingSchema`) that describes each known setting: identity, human-readable metadata, category, default value, expected `QMetaType`, and optional constraints.
2. A **flat value store** for values that have been explicitly set, with default fallback resolution on read.
3. **JSON persistence** — a category-grouped, path-nested on-disk representation written atomically and reloaded with schema validation.

Both types live in `namespace dir2md::backend`.

---

## 2. Types and Responsibilities

### 2.1 `struct SettingSchema`

Plain aggregate describing one setting key.

| Member | Role |
| --- | --- |
| `key` | Logical identifier; `/` is significant and used as a nesting separator during save. |
| `title`, `description` | Presentation metadata only; never used by the manager logic. |
| `category` | Grouping used as the **top-level JSON object name** during save. |
| `defaultValue` | Returned by `get()` when no explicit value exists. Never itself validated. |
| `type` | Target `QMetaType` used for conversion checks and post-load coercion. |
| `min`, `max` | Optional numeric bounds; only consulted when `type` is `int` or `double`. |
| `enumOptions` | Optional allow-list; when non-empty, the value's string form must be a member. |

`isValid(const QVariant &) const -> bool` is the single validation entry point:

1. Reject if `!val.canConvert(type)`.
2. If `type` is exactly `int` or `double`, compare `val.toDouble()` against `min`/`max` when those are valid.
3. If `enumOptions` is non-empty, require `enumOptions.contains(val.toString())`.

Note that steps 2 and 3 are **not** mutually exclusive: a numeric schema that also declares `enumOptions` must satisfy both range and allow-list.

### 2.2 `class SettingsManager : public QObject`

State:

- `QHash<QString, QVariant> m_values` — explicitly-set values only. Defaults are **not** materialized into this map.
- `QHash<QString, SettingSchema> m_schemaRegistry` — schema metadata, keyed by `SettingSchema::key`.

Signals:

- `settingChanged(const QString &key, const QVariant &newValue)` — emitted on accepted mutation and per key accepted during load.
- `settingsSaved(const QString &path)` — emitted only after a fully successful atomic commit.

Public surface:

| Member | Behavior |
| --- | --- |
| `get(key)` | Explicit value → schema default → invalid `QVariant()`. Never throws, never inserts. |
| `set(key, value)` | Validates against schema **if registered**; unregistered keys bypass validation entirely. Stores and emits only when the value differs from the current stored value. Returns `false` only on validation rejection. |
| `activeValues()` | Const reference to the live explicit-value map. Not a snapshot. |
| `registerSchema(schema)` | Unconditional insert keyed by `schema.key`; silently replaces an existing registration. |
| `schema(key)` | `std::optional<SettingSchema>` by value. |
| `schemas()` | Const reference to the live schema map. Not a snapshot. |
| `save_to_file(path)` | Serializes `m_values` to nested JSON grouped by category; atomic write via `QSaveFile`. |
| `load_from_file(path)` | Parses JSON, flattens, strips the category component, validates, **replaces** `m_values`. |

### 2.3 Internal helpers (anonymous namespace, `.cpp` only)

- `insertNestedValue(QJsonObject &obj, const QStringList &pathParts, const QVariant &value)` — recursively walks/creates nested `QJsonObject`s along `pathParts` and inserts the leaf. Returns silently on an empty path.
- `flattenJsonObject(const QJsonObject &obj, const QString &prefix, QHash<QString, QVariant> &out)` — depth-first flattening; every non-object leaf becomes `prefix/key`.

---

## 3. Persistence Format and Round-Trip Contract

Save maps a value as:

```
<category> / <key split on "/">  →  value
```

For example, key `editor/tab_size` with category `Editor` produces:

```json
{
  "Editor": {
    "editor": {
      "tab_size": 4
    }
  }
}
```

Load performs the inverse by flattening to `Editor/editor/tab_size` and then **unconditionally removing the first path component**. The category is therefore write-only metadata: it is emitted on save and discarded on load. Consequences:

- Moving a setting to a different `category` does not break loading of previously saved files.
- The category name is never validated against the schema on load; a hand-edited file with a bogus category still resolves.
- Two schemas that share the same `key` but differ in `category` are indistinguishable after the strip and will collide.

Round-trip fidelity holds only for scalar leaves. Any `QVariant` that serializes to a JSON object (e.g. `QVariantMap`) is flattened into synthetic sub-keys on load and will not resolve back to the original key.

Only keys present in `m_values` are persisted. Settings still resolving to their schema default are absent from the file by design.

---

## 4. Control Flow

### 4.1 `save_to_file`

1. Iterate `m_values`.
2. Resolve category: schema lookup → `category.trimmed()` → fall back to `"General"` when absent or blank.
3. Split key on `/` with `Qt::KeepEmptyParts`, then strip leading empty components (normalizes a leading `/`). Interior empty components are preserved and become empty-named JSON keys.
4. Ensure the category object exists, copy it out by value, insert the nested value, write the modified copy back.
5. Serialize indented, open a `QSaveFile`, write, `commit()`.
6. On success emit `settingsSaved(filePath)` and return `true`. Any of open/write/commit failure logs via `qWarning` and returns `false` with no partial file left behind (`QSaveFile` discards on destruction without commit).

### 4.2 `load_from_file`

1. Open read-only with `QIODevice::Text`. Missing/unreadable file returns `false` **silently** — this is the documented expected first-run path.
2. Read all, close, parse. `doc.isNull() || doc.isEmpty()` → warn and return `false`.
3. `doc.object()`; empty root → return `false`.
4. Flatten to `loadedValues`.
5. **`m_values.clear()`** — the previous state is destroyed at this point, before any per-key validation.
6. For each flattened entry: normalize leading empties, skip if empty, drop the first component (category), rejoin as `schemaKey`.
7. Unknown key → skipped silently. Invalid value → `qWarning` and skipped.
8. Coerce with `convert(sch->type)` when convertible, insert, emit `settingChanged`.
9. Return `true`.

---

## 5. Invariants, Ownership, and Assumptions

- **Ownership/lifetime**: no raw owning pointers; all state is value-semantic Qt containers. `SettingsManager` follows standard `QObject` parent ownership. `SettingSchema` is freely copyable.
- **Nullability**: `parent` may be null. `get()` on an unknown key returns an *invalid* `QVariant`, not a null pointer — callers must check `isValid()`.
- **Reference escape**: `activeValues()` and `schemas()` hand out references to live members. They are invalidated by any subsequent `set()`, `registerSchema()`, or `load_from_file()`. Callers must not hold them across mutations.
- **Thread safety**: none. No mutexes, no atomics. `QHash` iteration and mutation are unsynchronized, and signals are emitted synchronously on the calling thread. The class must be confined to one thread, or all access externally serialized.
- **Exception safety**: no `try`/`catch` and no explicit throws. Allocation failure from Qt containers would propagate; `load_from_file` is *not* exception-safe with respect to state because `m_values.clear()` has already run.
- **Schema ordering invariant**: schemas must be registered *before* `load_from_file` and before any `set()` that requires validation. Values set before registration bypass validation permanently — they are never re-validated afterwards.
- **`QMetaType` invariant**: a `SettingSchema` constructed without setting `type` carries a default `QMetaType` (`UnknownType`). Validation against it is effectively degenerate; schemas must always populate `type`.

---

## 6. Header/Implementation Consistency and Project Conventions

Repository convention requires `snake_case` for all identifiers and trailing return types for all functions, with no `[[nodiscard]]`.

- `SettingSchema` members, `isValid`, `registerSchema`, `activeValues`, `settingChanged`, `settingsSaved`, `defaultValue`, `enumOptions`, `m_schemaRegistry`, and the class names themselves are `camelCase`/`PascalCase`, deviating from the stated convention. `save_to_file` / `load_from_file` are the only convention-compliant names.
- `save_to_file` and `load_from_file` are declared with a leading `bool` return type instead of a trailing return type, unlike every other member.
- `registerSchema` is declared `void registerSchema(...)` in the header but defined `auto registerSchema(...) -> void` in the `.cpp`. This is legal and refers to the same function, but the mismatch is a readability hazard.

These are style/consistency observations, not defects.

---

## Static Analysis and Security

### F-1 — `canConvert` is a type-level query, so invalid strings pass validation and become silent zeros

**Evidence.** `SettingSchema::isValid` gates on `val.canConvert(type)` and then range-checks `val.toDouble()`. `SettingsManager::load_from_file` subsequently calls `typedValue.convert(sch->type)` and ignores its `bool` result.

**Risk.** `QVariant::canConvert` reports whether a *conversion path exists between the types*, not whether the specific value converts successfully. A `QString` value such as `"abc"` reports convertible to `int`; `toDouble()` yields `0.0`, which passes any range whose `min` is `<= 0`; `convert()` then fails and leaves a default-constructed `0`. Similarly, any integer satisfies a `bool` schema.

**Impact.** Corrupt or hand-edited configuration files inject silently wrong values instead of being rejected. Because `min`/`max` are only consulted for exactly `int` and `double`, a schema typed as `qint64`, `uint`, or `float` has its bounds ignored entirely, so out-of-range values are accepted unconditionally.

**Mitigation.** Validate the value, not the type: attempt a copy `QVariant tmp = val; if (!tmp.convert(type)) return false;` and range-check `tmp`. Broaden the numeric branch to cover all arithmetic metatypes (or test `QMetaType::isRegistered` + `typeFlags`). In `load_from_file`, treat a failing `convert()` as a rejection rather than a silent coercion.

**Test.** Register an `int` schema with `min = 1`, `max = 10`; assert `set(key, QString("abc"))` returns `false`; write a file containing `"abc"` for that key and assert the key is skipped and `get()` still returns the default.

### F-2 — `load_from_file` clears all state before validating, and never reports keys that disappeared

**Evidence.** `m_values.clear()` executes immediately after a successful parse. `settingChanged` is emitted only inside the per-key insert loop.

**Risk.** Two distinct problems. (a) A syntactically valid file whose entries are all unknown or invalid leaves the manager with an empty value map and a `true` return — every previously-set setting is silently reverted to its default. (b) Any key that was present in `m_values` before the load but absent from the file is reset without any `settingChanged` emission.

**Impact.** Observers that cache values from `settingChanged` retain stale state that no longer matches `get()`. A truncated or partially-corrupted settings file becomes a silent global reset rather than a recoverable error.

**Mitigation.** Build the replacement map into a local `QHash` first, swap it in only after the loop, then diff old versus new and emit `settingChanged` for every key whose effective value (explicit value or schema default) actually changed — including removals. Consider returning `false`, or an explicit result type, when the file parsed but produced zero accepted keys.

**Test.** `set()` two keys, then load a file containing only one of them; assert a `settingChanged` is emitted for the dropped key with its default value and that `get()` on it returns the default.

### F-3 — Dead branch in `insertNestedValue` masks silent overwrite of scalar leaves

**Evidence.**

```cpp
if (!obj[first].isObject()) {
    nested = obj[first].toObject();
} else {
    nested = obj[first].toObject();
}
```

Both branches are identical, so the `isObject()` test has no effect.

**Risk.** When an existing entry at `first` is a scalar (or absent), `toObject()` returns an empty object and the previously stored scalar is discarded. Registering both `"a"` and `"a/b"` in the same category means whichever is iterated last wins: saving `a = 1` then `a/b = 2` drops `a`, and the reverse order drops `a/b`.

**Impact.** Silent data loss on save for any key set that has prefix collisions. `QHash` iteration order is unspecified, so which value survives is non-deterministic across runs.

**Mitigation.** Collapse the branches and make the collision explicit: if `obj[first]` exists and is not an object, log a warning and skip (or reject) the conflicting key. Better, reject prefix-colliding keys at `registerSchema` time so the conflict cannot reach persistence.

**Test.** Register `a` and `a/b` in one category, set both, save, reload, and assert both round-trip — this test should fail against the current implementation and document the constraint.

### F-4 — Unregistered keys bypass validation entirely and are never reconciled

**Evidence.** `set()` validates only `if (m_schemaRegistry.contains(key))`. `registerSchema` performs no re-validation of already-stored values.

**Risk.** A value stored before its schema is registered (or under a typo'd key) is accepted with any type and any content, and is then written to disk by `save_to_file`. On the next load it is either silently dropped (unknown key) or rejected by validation.

**Impact.** Typos in key strings fail silently at write time and only manifest as settings that "don't persist". Type discipline is not actually enforced by the API.

**Mitigation.** Either reject unknown keys in `set()` (returning `false`), or re-validate and prune `m_values` inside `registerSchema`. At minimum, `qWarning` on writes to unregistered keys.

**Test.** `set("typo/key", ...)` without registering, save, reload, assert the key is gone; and assert `set()` returns `false` once strict mode is adopted.

### F-5 — Empty and malformed key components are dropped or serialized without diagnostics

**Evidence.** `save_to_file` splits with `Qt::KeepEmptyParts` and strips only *leading* empty components. `insertNestedValue` returns immediately when `pathParts` is empty. `load_from_file` skips entries whose path is empty and unconditionally `removeFirst()`s the category.

**Risk.** An empty key `""` produces an empty category object and the value is silently discarded. Interior empties (`"a//b"`) create empty-named JSON keys that survive a round trip only by coincidence. A top-level *scalar* in the JSON file (no category wrapper) flattens to a single-component path; after `removeFirst()` the remaining `schemaKey` is the empty string, which will never match a schema and is silently dropped.

**Impact.** Malformed input and malformed keys fail silently in both directions, making configuration bugs hard to diagnose.

**Mitigation.** Validate keys at `registerSchema`: reject empty keys, leading/trailing `/`, and consecutive `/`. On load, warn when a flattened path has fewer than two components instead of discarding it.

**Test.** Parameterized test over `""`, `"/a"`, `"a//b"`, `"a/"` asserting registration is rejected; plus a load test on a file with a top-level scalar asserting a warning is produced.

### F-6 — `filePath` is used unvalidated for both read and write

**Evidence.** Both `save_to_file` and `load_from_file` pass the caller-supplied `filePath` straight to `QSaveFile`/`QFile`.

**Risk.** If the path ever originates from untrusted input (CLI argument, config field, IPC), it permits arbitrary-location writes and reads via traversal (`../`), UNC paths, or Windows device names. `QSaveFile` also fails when the parent directory does not exist, and the failure surfaces only as a `qWarning` plus `false`.

**Impact.** Overwrite of arbitrary user-writable files, or disclosure of arbitrary readable files into the value map, in any caller that forwards untrusted input. Low severity today because callers are internal, but the class is a shared backend library with no stated precondition.

**Mitigation.** Document the precondition that `filePath` must be a trusted, application-controlled location. Where the path can be influenced externally, canonicalize it and assert containment within `QStandardPaths::AppConfigLocation`, and create the parent directory with `QDir::mkpath` before writing.

**Test.** Assert `save_to_file` returns `false` for a path inside a non-existent directory, and assert the containment check rejects a `../`-bearing path once implemented.

### F-7 — No thread-safety guarantees, and reference accessors invalidate under mutation

**Evidence.** No synchronization primitives anywhere; `activeValues()` and `schemas()` return references to the member `QHash`es; signals are emitted while the object's own state is being mutated.

**Risk.** Concurrent access from two threads is a data race on both hashes. Even single-threaded, a `settingChanged` handler that calls `set()` re-enters the object mid-mutation, and a handler holding a reference from `activeValues()` observes a container that may rehash underneath it. In `load_from_file`, a slot that calls `set()` mutates `m_values` while the load loop is still iterating `loadedValues` (a separate container, so not iterator invalidation, but the final state becomes order-dependent).

**Impact.** Undefined behavior under concurrency; surprising, order-dependent results under signal re-entrancy.

**Mitigation.** Document explicit thread-affinity (single-threaded, owner-thread only) in the header. Return the hashes by value if callers need stable snapshots. Defer `settingChanged` emission in `load_from_file` until after the loop completes so handlers observe a consistent final state.

**Test.** A test that connects a slot which calls `set()` on a different key from within `settingChanged` during a load, asserting the final map is the expected one.

### F-8 — Minor implementation hazards

- **Non-const `operator[]` in `set()`**: `const SettingSchema &schema = m_schemaRegistry[key];` uses the mutating overload on a non-const `QHash`, forcing a detach and creating a default entry if the `contains()` guard were ever removed. Prefer `m_schemaRegistry.value(key)` or `constFind`. The local name `schema` also shadows the member function `schema()`.
- **Short-write not detected**: `save_to_file` checks only `saveFile.write(...) < 0`, not a partial write. `commit()` happens to catch most such cases, but the check as written is misleading.
- **`QIODevice::Text` on JSON read**: harmless for parsing, but it performs newline translation on a format that does not need it and makes byte counts platform-dependent.
- **`parseError` reported when not populated**: the warning prints `parseError.errorString()` for the `doc.isEmpty()` case as well, where no parse error occurred, producing a misleading "no error" message.
- **Unbounded recursion**: both `insertNestedValue` and `flattenJsonObject` recurse on path/JSON depth. Depth is bounded in practice by Qt's JSON parser nesting limit, so this is a residual rather than an exploitable risk, but neither helper enforces its own bound.
- **`defaultValue` is never validated** against its own schema at registration time, so `get()` can return a value that `set()` would reject.

### Residual Risks and Limitations

- Qt behavior for `QVariant::canConvert`/`convert`, `QSaveFile` atomicity, `QJsonDocument` nesting limits, and `QHash` iteration-order instability is taken as a contextual assumption; none of it was verified from within the analyzed pair.
- Callers of `SettingsManager` (frontend, CLI, tests) were not analyzed, so it is not established whether any of them supply untrusted paths (F-6), rely on `settingChanged` for cache coherence (F-2), or register prefix-colliding keys (F-3). The severity of those findings depends on call-site behavior.
- No dynamic analysis, sanitizer run, or coverage measurement was performed; findings are source-level only.
