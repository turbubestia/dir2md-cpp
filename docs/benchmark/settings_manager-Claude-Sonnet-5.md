# `settings_manager.hpp` / `settings_manager.cpp`

## Purpose and Role

`SettingsManager` (namespace `dir2md::backend`) is the backend's flat key/value settings store. It is a `QObject`-derived class meant to be shared between the frontend and CLI targets. It combines:

- A **schema registry** (`SettingSchema`) describing known setting keys: title, description, category, default value, `QMetaType`, and optional numeric/enum constraints.
- A **value store** (`m_values`, `QHash<QString, QVariant>`) holding only the settings that have been explicitly set (deviating from schema defaults).
- **JSON persistence** (`save_to_file` / `load_from_file`) that groups flattened `"section/key"` style keys into a nested, per-category JSON document on disk.

Both files were provided; this is a complete pair (header + implementation).

## Major Types

### `SettingSchema`
Plain struct describing metadata and constraints for a setting key (e.g. `"editor/tab_size"`). `isValid(const QVariant&)` validates a candidate value:
- Rejects values that cannot convert to `type` via `QVariant::canConvert`.
- For `int`/`double` types, enforces `min`/`max` bounds (compared via `toDouble()`).
- If `enumOptions` is non-empty, requires `val.toString()` to be one of the allowed options.

### `SettingsManager`
- `get(key)`: returns the stored value if present in `m_values`, else the schema's `defaultValue` if a schema is registered, else an invalid `QVariant()`.
- `set(key, value)`: validates against a registered schema (if any), stores the value, and emits `settingChanged` only when the value actually differs from what's currently stored. Returns `false` only when schema validation rejects the value — an unregistered key is always accepted.
- `activeValues()` / `schema()` / `schemas()`: read-only accessors into the two internal hashes.
- `registerSchema(schema)`: inserts/overwrites an entry in `m_schemaRegistry` keyed by `schema.key`.
- `save_to_file(path)` / `load_from_file(path)`: JSON persistence (see below).

## Usage Pattern

Typical flow (matches `test/backend/core/setting_manager_test.cpp`):
1. Construct a `SettingsManager` (optionally with a `QObject` parent).
2. Register `SettingSchema` entries for known keys (schemas are optional — unregistered keys work but are unvalidated and default-less).
3. `set()`/`get()` values using `"category/nested/leaf"`-style string keys.
4. `save_to_file()` to persist, `load_from_file()` to restore into a (possibly different) manager instance that has the relevant schemas pre-registered — schemas must be registered *before* loading, since `load_from_file` silently drops keys that have no matching schema.

## Persistence: Data Flow

**Save (`save_to_file`)**: For each entry in `m_values`, its category is looked up from the schema (defaulting to `"General"` if unregistered or blank), the key is split on `/`, and the value is inserted into a per-category `QJsonObject` via the recursive helper `insertNestedValue`. The result (`{ "<Category>": { "<nested>": { "<leaf>": value } } }`) is serialized with `QJsonDocument::Indented` and written atomically via `QSaveFile`.

**Load (`load_from_file`)**: Reads and parses the file, flattens the nested JSON back into `"Category/nested/leaf"` keys via `flattenJsonObject`, strips the leading category segment, looks up a schema for the remaining `"nested/leaf"` key, validates and converts the value's type, then clears `m_values` entirely and repopulates it — emitting `settingChanged` for every loaded key. Keys without a matching schema, or whose value fails `isValid`, are silently skipped (with `qWarning()` only for validation failures, not for unknown keys).

## Invariants and Assumptions

- Keys are expected to use `/` as a hierarchy separator; leading empty segments (from a leading `/`) are stripped in both save and load paths.
- A schema's `type` field (`QMetaType`) has no default in the struct definition — a default-constructed `SettingSchema` has an invalid type, so `isValid` will reject essentially all values via `canConvert` unless `type` is explicitly set by the caller.
- `load_from_file` assumes the on-disk JSON always has exactly one category level above the setting hierarchy (matching what `save_to_file` produces). Hand-edited or foreign JSON without this shape will have its top-level keys misinterpreted as the "category" to strip.
- Not documented or enforced as thread-safe: `m_values` and `m_schemaRegistry` are plain `QHash`s with no synchronization.

## Static Analysis and Security

**Finding 1 — Dead/no-op conditional in `insertNestedValue` masks a real branch bug**
- Evidence: 
  ```cpp
  QJsonObject nested;
  if (!obj[first].isObject()) {
      nested = obj[first].toObject();
  } else {
      nested = obj[first].toObject();
  }
  ```
  Both branches of the `if`/`else` execute the identical statement, so the condition has no effect on behavior; `nested` is unconditionally reassigned from `obj[first].toObject()`.
- Risk: `QJsonValue::toObject()` returns an empty `QJsonObject` when the value is not already an object (e.g. a scalar string/number/bool). If a setting key stored as a scalar leaf (e.g. `"foo"`) collides with a hierarchical prefix used by another key (e.g. `"foo/bar"`), the existing scalar at `"foo"` is silently discarded and replaced by a nested object during the next save, with no warning.
  It also means the intended distinction the code appears to have been written for (preserve vs. reset the nested object depending on whether the existing value is already an object) is not actually implemented — the code always takes the "reset" behavior.
- Impact: Silent data loss of a stored setting value on save, purely from key-naming collisions between a "leaf" key and a "prefix" key. Because keys are user/schema-defined free-form strings, this is realistic (e.g. `"editor"` and `"editor/tab_size"` both registered).
- Mitigation: Fix the condition to have distinct branches (e.g. only treat `obj[first]` as the base if it is already an object; otherwise start from an empty `QJsonObject`), and/or add validation when registering schemas/setting keys to reject a key that is a strict prefix of another existing key (or vice versa).
- Follow-up test recommendation: Register/set both `"editor"` (scalar value) and `"editor/tab_size"` (nested value), call `save_to_file`, and assert on the resulting JSON shape / whether the scalar value survives.

**Finding 2 — `load_from_file` treats a legitimately empty saved file as a failure**
- Evidence: `save_to_file` on a manager with zero settings would write `{}` (an empty `categoryRoot`). `load_from_file` does `QJsonObject root = doc.object(); if (root.isEmpty()) return false;`.
- Risk: A previously-saved file representing "no settings configured" (a valid state) is indistinguishable from a parse/read failure — callers cannot tell whether `load_from_file` failed because the file was missing/corrupt or because it validly contained zero settings.
- Impact: Application logic branching on the return value (e.g., falling back to first-run defaults vs. surfacing a load error to the user) may behave incorrectly for a valid empty-settings file.
- Mitigation: Distinguish "file unreadable / malformed JSON" from "valid JSON with an empty top-level object" — only the former should return `false`.
- Follow-up test recommendation: Save a `SettingsManager` with no settings set, then `load_from_file` that path and assert it returns `true` (currently would fail).

**Finding 3 — No API to reset/remove a stored value back to its schema default**
- Evidence: The header exposes `get`, `set`, `activeValues`, `registerSchema`, `schema`, `schemas`, `save_to_file`, `load_from_file` — there is no `remove`/`reset`/`unset` method, and `m_values` is only ever cleared wholesale inside `load_from_file`.
- Risk: Once `set(key, value)` is called for a key — including with an invalid/default-constructed `QVariant()` — `m_values.contains(key)` becomes permanently `true`, so `get(key)` will never again fall back to the schema's `defaultValue` for that key within the process lifetime (short of a full `load_from_file` reload).
- Impact: Callers that want "restore this one setting to its default" have no supported way to do so other than re-deriving and re-`set`ing the schema's `defaultValue` themselves, and `set(key, QVariant())` would only pass validation for keys with no registered schema — for validated keys, `isValid` would reject the invalid `QVariant` via `canConvert`, giving a confusing failure path for callers attempting a "reset."
- Impact for maintainability: This is more a completeness gap than a memory/security bug, but it affects correctness of any future reset-to-default UI/CLI feature.
- Mitigation: Add an explicit `reset(key)`/`remove(key)` API that erases the key from `m_values` (falling back to schema default) and emits `settingChanged` with the default value.
- Follow-up test recommendation: `set()` a key, then attempt to restore its default value, and assert `get()` returns the schema default afterward.

**Finding 4 — Unvalidated, unbounded input acceptance for unregistered keys**
- Evidence: In `set()`, validation only runs `if (m_schemaRegistry.contains(key))`; otherwise the value is stored unconditionally, and `save_to_file` will happily persist it (as category `"General"`).
- Risk: Arbitrary keys/values (including deeply nested `"a/b/c/.../z"` paths or huge strings) can be inserted with no bounds, size, or shape checks, from any code path that has a `SettingsManager&`. Since `key.split("/")` on save/`insertNestedValue`'s recursion depth is driven directly by caller-controlled key content, a maliciously long or deeply-segmented key from an untrusted source (e.g. if keys ever originate from external/untrusted input such as an imported settings file, CLI argument, or IPC payload) could produce excessive JSON nesting depth or size.
- Impact: Low likelihood in current call sites (schemas are presumably developer-controlled), but this is a real input-validation gap if any external input is ever plumbed into `set()`'s `key` parameter directly.
- Mitigation: Consider requiring a registered schema before any `set()` succeeds (opt-in "strict mode"), or at minimum bound key length/segment count for defense in depth if keys can originate from external input.
- Follow-up test recommendation: `set()` with a very long or deeply nested key and confirm the library's/application's behavior is acceptable (currently no test covers unregistered-key limits).

**Finding 5 — Concurrency is undocumented and unenforced**
- Evidence: No mutex/lock guards `m_values`/`m_schemaRegistry`; no thread-affinity assertions (e.g. no `Q_ASSERT(thread() == QThread::currentThread())`) in any accessor or mutator.
- Risk: If `SettingsManager` is ever accessed from multiple threads (e.g. a background load/save thread alongside a UI thread reading `get()`), concurrent `QHash` access is a data race (undefined behavior).
- Impact: Latent instability that would only manifest once multi-threaded usage is introduced; currently benign if the class is only used single-threaded as its `QObject` signal usage without explicit `Qt::QueuedConnection` suggests.
- Mitigation: Document the intended thread-affinity (e.g. "must only be used from its own thread") in the header, or add synchronization if cross-thread use is a requirement.
- Follow-up test recommendation: N/A directly testable without introducing threading, but a contract test/assert could catch accidental cross-thread calls in debug builds.

**Contextual assumption**: This analysis assumes `SettingsManager` instances are always constructed and used entirely on a single thread, and that schema registration always happens before any `load_from_file` call expected to restore those keys — both are consistent with all current call sites in `setting_manager_test.cpp`, but neither is enforced or documented in the header.
