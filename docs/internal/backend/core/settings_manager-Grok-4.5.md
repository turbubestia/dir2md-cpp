# settings_manager

**Source pair:** [settings_manager.hpp](../../../src/backend/core/settings_manager.hpp) + [settings_manager.cpp](../../../src/backend/core/settings_manager.cpp)  
**Namespace:** `dir2md::backend`  
**Counterpart:** Found (header + implementation).

## Purpose and Role

`SettingsManager` is a QtCore-only backend component that stores application settings as a flat `key → QVariant` map, optionally validated against registered `SettingSchema` metadata. It can persist and restore values as nested JSON grouped by schema category.

It is designed as pure logic (no UI): consumers register schemas, read/write values, and optionally save/load a JSON file. A typical registration helper in the same module is `CoreSchema::registerSchemas` (contextual dependency; not part of this pair).

## Major Types

### `SettingSchema`

Metadata for one setting key:

| Field | Role |
|--------|------|
| `key` | Canonical flat key (e.g. `"editor/tab_size"`). Used as registry index. |
| `title`, `description`, `category` | Human-oriented metadata; `category` drives JSON top-level grouping on save. |
| `defaultValue` | Returned by `get` when the key is registered but not present in `m_values`. |
| `type` | Expected `QMetaType` for validation and conversion on load. |
| `min`, `max` | Optional numeric bounds (checked only for `int` / `double` schemas). |
| `enumOptions` | If non-empty, value must stringify to a member of the list. |

`SettingSchema::isValid(const QVariant &val)`:

1. Rejects if `!val.canConvert(type)`.
2. For `int` or `double` types, converts to `double` and enforces `min`/`max` when those `QVariant`s are valid.
3. If `enumOptions` is non-empty, requires `enumOptions.contains(val.toString())`.
4. Otherwise accepts.

### `SettingsManager` (`QObject`)

| Member | Role |
|--------|------|
| `m_values` | User/loaded overrides only (does not mirror defaults). |
| `m_schemaRegistry` | `key → SettingSchema`. |
| Signals | `settingChanged(key, newValue)`, `settingsSaved(path)`. |

Constructor is inline: `explicit SettingsManager(QObject *parent = nullptr)`.

## Public API and Behavior

### Read / write

- **`get(key)`**  
  1. `m_values` hit → stored value.  
  2. Else schema hit → `defaultValue`.  
  3. Else invalid `QVariant()`.

- **`set(key, value)`**  
  - If a schema exists for `key` and `!schema.isValid(value)` → `false`, no store, no signal.  
  - If no schema → accepts any value.  
  - If `m_values.value(key) != value` → insert and emit `settingChanged`.  
  - Returns `true` on acceptance (including no-op same-value sets).

- **`activeValues()`**  
  Const reference to `m_values` only (defaults not included).

### Schema registry

- **`registerSchema(schema)`** — inserts/overwrites by `schema.key`. Does not validate schema completeness, does not clear conflicting `m_values`, does not emit.
- **`schema(key)`** — `std::optional<SettingSchema>`; `nullopt` if missing.
- **`schemas()`** — const reference to the full registry.

### Persistence

**`save_to_file(filePath)`**

1. Builds a JSON object whose top-level keys are categories.
2. For each entry in `m_values` only (not schema defaults):
   - Category = schema `category` trimmed if non-empty, else `"General"`.
   - Key is split on `/` (`Qt::KeepEmptyParts`); leading empty segments (from a leading `/`) are stripped.
   - Remaining path segments are nested under the category via recursive `insertNestedValue`.
3. Writes indented JSON through `QSaveFile` (open → write → `commit`) for atomic replace.
4. On I/O failure: `qWarning`, `false`, no `settingsSaved`.
5. On success: emit `settingsSaved(filePath)`, return `true`.

**`load_from_file(filePath)`**

1. Open `QFile` read-only; missing/unreadable → `false` with no warning (first-run friendly).
2. Parse JSON; null/empty document → `qWarning` with parse error, `false`.
3. Empty root object → `false`.
4. Flatten nested objects to `prefix/key` paths (`flattenJsonObject`). Arrays and non-object containers are not recursed as objects; leaf values become `QVariant`s via `toVariant()`.
5. **`m_values.clear()`** then walk flattened pairs:
   - Strip leading empty path parts; drop the first remaining segment as the **category** prefix; join the rest as `schemaKey`.
   - No matching schema → silently skip.
   - `!isValid(value)` → `qWarning`, skip.
   - Else convert toward schema type when `canConvert`, insert into `m_values`, emit `settingChanged` per accepted key.
6. Returns `true` if parsing and root handling succeeded, even if every value was skipped.

Helpers (anonymous namespace in `.cpp`):

- `insertNestedValue` — recursive nest insert; empty `pathParts` is a no-op.
- `flattenJsonObject` — recursive object flatten into flat slash paths.

## Control Flow and Data Flow

```text
registerSchema ──► m_schemaRegistry
set ──(optional isValid)──► m_values ──► settingChanged
get ◄── m_values, else schema.defaultValue
save_to_file: m_values + schema.category ──► nested JSON ──► QSaveFile ──► settingsSaved
load_from_file: JSON ──► flatten ──► strip category ──► schema/isValid ──► replace m_values + settingChanged*
```

\*On successful open/parse, `m_values` is cleared before re-population; rejected keys leave no override (subsequent `get` uses default if registered).

## Usage Patterns

1. Construct `SettingsManager` (optional `QObject` parent).
2. Register all known schemas before load/set that should be constrained (e.g. via a helper like `CoreSchema::registerSchemas` — contextual).
3. Optionally `load_from_file` (expect `false` on first run).
4. `get` / `set` at runtime; observe `settingChanged`.
5. `save_to_file` when persisting; observe `settingsSaved` on success.

**JSON shape (save):** category object → nested path from key segments.  
**JSON shape (load expectation):** same category-first layout; the first path segment after flatten is always treated as category and stripped to recover the schema key.

Example correspondence:

- In-memory key `core/max_threads`, schema category `Performance`  
- On disk: `{ "Performance": { "core": { "max_threads": 4 } } }`  
- After flatten + strip: schema key `core/max_threads`.

## Invariants and Preconditions

- Canonical keys are flat strings; `/` is both the nesting delimiter in JSON and part of the logical key after category strip on load.
- `m_values` holds overrides only; equality of “effective config” must merge with schema defaults at read time via `get`.
- Validation on `set`/`load` applies only when a schema is registered for that key.
- Re-registering a schema overwrites metadata for the same key without reconciling existing `m_values`.
- Successful `load_from_file` always clears prior overrides before applying file content (full replace of active values, not merge).
- `activeValues()` / `schemas()` expose internal containers by const reference; callers must not retain references across mutating operations that rehash those maps.

## Ownership, Lifetime, Threading, Exceptions

- **Ownership:** Values and schemas are value-copied into `QHash` members. No external buffer ownership. `QObject` parent/child lifetime applies if a parent is set.
- **Nullability:** Keys are `QString`; empty keys are not specially rejected at API boundary. File path emptiness leads to open failure.
- **Thread-safety:** No locks. Not safe for concurrent use from multiple threads without external synchronization. Signals emit on the calling thread (standard Qt direct connection unless queued by the receiver).
- **Exception-safety:** Relies on Qt containers and I/O; no custom try/catch. `load_from_file` clears `m_values` only after successful JSON parse and non-empty root, so parse failures leave prior state intact; after `clear()`, partial skip of invalid keys can leave a thinner override set than before load.

## Input Validation and Error Handling

| Path | Behavior |
|------|----------|
| `set` invalid vs schema | `false`, no change |
| `set` unknown key | Accepted |
| `save` open/write/commit fail | `qWarning`, `false` |
| `load` missing file | `false`, silent |
| `load` bad JSON / empty doc | `qWarning`, `false`, state unchanged |
| `load` empty root object | `false`, state unchanged |
| `load` unknown key | Skip |
| `load` invalid value | `qWarning`, skip |
| `isValid` | `canConvert`, numeric min/max, optional enum list |

## Contextual Dependencies

- **Qt:** `QObject`, `QHash`, `QVariant`, `QMetaType`, `QJson*`, `QFile`, `QSaveFile`, `QDebug` (`qWarning`).
- **C++:** `<optional>` for `schema()`.
- **Call-site context (not part of this pair):** `CoreSchema` registers sample keys `core/tool_path` (`QString`) and `core/max_threads` (`int`, min 1, max 32). Tests under `test/backend/core/` exercise get/set, schemas, signals, and save/load round-trips — used only to confirm intended external behavior, not documented as this pair’s API surface.

## Static Analysis and Security

### 1. Save/load key path is not a pure round-trip for unregistered or category-colliding keys

- **Evidence:** `save_to_file` nests under schema `category` (or `"General"`). `load_from_file` always drops the first flattened path segment as category, then requires `schema(schemaKey)`. Unregistered keys can be saved but are always dropped on load. Keys whose first segment after flatten is wrong (hand-edited JSON, category renamed, or key without intermediate segments under a category whose name collides with key structure) will not match schemas.
- **Risk:** Silent data loss on reload; configuration appears to “reset” to defaults.
- **Impact:** Correctness and user trust in persistence; hard-to-debug “settings not sticking.”
- **Mitigation:** Document the category-first on-disk contract; reject save of unregistered keys or persist schema key explicitly (e.g. sidecar map); on load, log skipped keys at a higher visibility level; consider storing flat `key` fields instead of recoverable-by-strip paths.
- **Follow-up test:** Load a file with registered schema but wrong top-level category name; assert value skipped and default returned. Save unregistered key then load with later-registered schema under expected category — document actual vs desired behavior.

### 2. `load_from_file` clears overrides before applying entries (partial apply)

- **Evidence:** After successful parse and non-empty root, `m_values.clear()` runs, then each entry may be skipped (unknown/invalid). Function still returns `true`.
- **Risk:** Prior valid in-memory overrides are wiped even when the file contributes zero accepted keys, or only a subset.
- **Impact:** Correctness; unexpected loss of runtime configuration; `settingChanged` storms only for accepted keys, not for cleared ones that disappear without a dedicated “removed” signal.
- **Mitigation:** Build a candidate map, validate, then swap; or merge policy; emit changes for keys removed by clear; return structured result (accepted/skipped counts).
- **Follow-up test:** Set valid overrides, load file that parses but contains only unknown keys; assert whether old overrides survive (today they do not).

### 3. No signal when effective value disappears after clear-without-replace

- **Evidence:** `settingChanged` is emitted only on `set` when value changes and on load when a key is inserted after validation. Clearing `m_values` does not emit per removed key.
- **Risk:** UI/observers keep stale cached values while `get` falls back to defaults.
- **Impact:** Consistency between listeners and `get`.
- **Mitigation:** Diff old vs new maps and emit for removals/changes; or document that listeners must resync after `load_from_file`.
- **Follow-up test:** Spy `settingChanged` across a load that drops a previously set key; assert observer sync expectations.

### 4. `SettingSchema::isValid` conversion and enum checks are coarse

- **Evidence:** Uses `canConvert(type)` (often permissive across numeric/string forms). Numeric branch uses `toDouble()` for both int and double. Enum check uses `toString()` membership only when `enumOptions` non-empty; does not verify type first beyond `canConvert`.
- **Risk:** Values that convert lossily (e.g. truncated floats to int, surprising string-to-number) may pass; enum of non-string schema types depends on `QVariant::toString()` formatting.
- **Impact:** Weaker type safety than schema suggests; invalid configs accepted at boundaries.
- **Mitigation:** Prefer strict type id checks or `QVariant::convert` success with range checks on the post-conversion value; for enums, compare in the schema’s native type.
- **Follow-up test:** `set`/`isValid` matrix for string `"4"` vs int schema, out-of-range doubles, enum with non-string type.

### 5. `insertNestedValue` branch is redundant; intermediate non-objects can be overwritten

- **Evidence:**  
  ```cpp
  if (!obj[first].isObject()) {
      nested = obj[first].toObject();
  } else {
      nested = obj[first].toObject();
  }
  ```  
  Both branches call `toObject()`. If `obj[first]` is a scalar/array, `toObject()` yields empty object and the subsequent `insert` replaces the scalar with an object.
- **Risk:** Conflicting keys where one value is a prefix of another (e.g. `editor` as leaf and `editor/tab_size`) clobber data on save.
- **Impact:** Silent loss or structural corruption of saved JSON under adversarial or mistaken key sets.
- **Mitigation:** Detect non-object intermediate nodes and fail save or reject keys that are prefixes of others at `set`/`registerSchema` time; simplify the dead branch.
- **Follow-up test:** `set("a", 1)` and `set("a/b", 2)` then save; inspect JSON and reload behavior.

### 6. Path edge cases: empty segments and category strip

- **Evidence:** Split uses `KeepEmptyParts`; leading empties stripped on save and load. After strip, load always `removeFirst()` as category; if only one segment remains before remove, `schemaKey` becomes empty and is joined as `""`. Empty `pathParts` after strip on save leads to `insertNestedValue` no-op (value not written) while category object may still exist.
- **Risk:** Keys like `"/foo"`, `"foo/"`, or `""` produce odd JSON or unloadable entries; empty schema key lookups fail open.
- **Impact:** Data loss or confusing files from bad key strings.
- **Mitigation:** Validate key format at `registerSchema`/`set` (no empty segments, no leading/trailing `/`).
- **Follow-up test:** Parameterized keys with empty segments through save/load.

### 7. `activeValues()` / `schemas()` return internal references

- **Evidence:** Const refs to `m_values` and `m_schemaRegistry`.
- **Risk:** Dangling use if caller stores ref and manager is destroyed; iteration invalidated if another operation mutates the hash (less likely if single-threaded and ref used ephemerally).
- **Impact:** Lifetime bugs in careless callers.
- **Mitigation:** Return by value or document “ephemeral borrow”; prefer snapshot APIs for cross-thread or deferred use.
- **Follow-up test:** Lifetime/documentation review; optional API that returns `QHash` copy.

### 8. Unvalidated file path and unrestricted write/read location

- **Evidence:** `save_to_file` / `load_from_file` take raw `QString filePath` with no canonicalization, extension check, or allow-list. Save uses `QSaveFile` at that path.
- **Risk:** API misuse if a caller passes user-controlled paths (overwrite arbitrary files the process can write; read unexpected files into settings after schema validation).
- **Impact:** Integrity/security depends entirely on caller path policy (sandboxing). Not a direct network exploit in this pair, but a classic path-trust hazard.
- **Mitigation:** Constrain paths at a higher layer (app config dir only); document that this API trusts `filePath`.
- **Follow-up test:** Integration test that save/load only under a temporary app config directory helper.

### 9. Thread safety and re-entrancy

- **Evidence:** No mutex; `set`/`load` mutate hashes and emit signals synchronously.
- **Risk:** Data races if used from multiple threads; re-entrant slots that call `set`/`load` during `settingChanged` can nest mutations.
- **Impact:** Undefined behavior or inconsistent state under concurrent or re-entrant use.
- **Mitigation:** Document thread affinity (main/backend worker only); avoid re-entrant mutation in slots or queue changes.
- **Follow-up test:** Document-only unless a threading policy is fixed; then stress re-entrant `set` from a slot.

### 10. `registerSchema` overwrite and stale values

- **Evidence:** `m_schemaRegistry.insert(schema.key, schema)` replaces metadata; existing `m_values[key]` is not revalidated.
- **Risk:** In-memory value can violate new min/max/type until next `set`/`load`.
- **Impact:** Invariant “stored value always schema-valid” does not hold across schema evolution.
- **Mitigation:** Revalidate or drop override on schema replace; version schemas.
- **Follow-up test:** Set value 50, re-register max 10, assert `get` / next `save` policy.

### Residual risks / assumptions

- Behavior of `QVariant::canConvert` / `convert` / JSON number typing is assumed per Qt version in the build; not re-audited against Qt internals here.
- Atomicity of `QSaveFile::commit` across platforms is trusted.
- No encryption, integrity MAC, or permission hardening of the settings file — confidentiality of secrets placed in settings is out of scope of this pair unless callers avoid storing secrets.
- Dead/redundant branch in `insertNestedValue` is a maintainability defect with behavioral bite only when intermediate types conflict.

**Summary:** Persistence design (category-prefixed nested JSON + schema-gated load + clear-before-apply), coarse validation, and path/key edge cases are the main correctness risks. No classic memory-corruption patterns appear in this pair (RAII Qt types, no raw `new`/`delete`). Security impact is primarily integrity of configuration and trusted filesystem paths supplied by callers.
