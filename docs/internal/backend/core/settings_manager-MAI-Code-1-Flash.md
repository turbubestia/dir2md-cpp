# SettingsManager

## Purpose and role

The SettingsManager class is a QObject-backed settings container for backend configuration values. It stores active values in a flat hash and uses registered SettingSchema instances to validate values, provide defaults, and describe metadata such as categories and numeric ranges.

## Public API and responsibilities

- SettingSchema: a metadata record for a single setting key. It carries the setting key, title, description, category, default value, type, and optional min/max/enum constraints. Its isValid method checks value convertibility, numeric bounds, and enum membership.
- SettingsManager:
  - get(key): returns an explicit value if present; otherwise it falls back to the schema default, and finally to an invalid QVariant when no schema exists.
  - set(key, value): validates through the schema when available, updates the in-memory store if the value differs, and emits settingChanged.
  - registerSchema/schema/schemas: manage schema metadata.
  - save_to_file/load_from_file: serialize and deserialize settings using JSON, grouping values by category and using slash-delimited keys to represent nested paths.

## Behavior and data flow

- Values are stored as flat keys in m_values, which makes lookups O(1). The serializer turns a slash-delimited key such as "editor/tab_size" into nested JSON under a category object.
- During save, the category for each setting is taken from the corresponding schema if present; otherwise the code defaults to "General".
- During load, the JSON is flattened into a temporary map, the leading category segment is removed, and each remaining key is matched against a registered schema. Unknown keys are skipped, and invalid values are skipped with a warning.

## Invariants and preconditions

- A value can only be set successfully if it satisfies the registered schema constraints; otherwise set returns false and stores nothing.
- The persistence format assumes a consistent contract: slash-separated keys represent nested paths, and the first path segment is treated as the category name during load.
- The class does not maintain separate default values in the runtime state; get relies on schema defaults when no override is present.
- Because the runtime store is a flat hash, nested JSON is reconstructed from the key path rather than from an explicit tree structure.

## Static Analysis and Security

### 1. Silent data loss during load

- Evidence: In [src/backend/core/settings_manager.cpp](src/backend/core/settings_manager.cpp), load_from_file skips unknown keys with `continue` and skips invalid values with a warning before continuing. The method still returns success after those entries were dropped.
- Risk: A partially invalid or forward-incompatible settings file can appear to load successfully while silently dropping user settings.
- Impact: Users can lose configuration values without a clear indication, which is especially risky when settings are edited manually or when the schema evolves.
- Mitigation: Return a richer status result, surface the skipped entries to callers, or preserve unknown values in a separate bucket instead of dropping them.
- Follow-up test recommendation: Create a test that loads a file containing one valid entry, one unknown key, and one invalid value and verifies that the outcome exposes the skipped entries instead of silently succeeding.

### 2. Round-trip behavior depends on a strict key/path convention

- Evidence: save_to_file splits keys on "/" and load_from_file removes the first segment as the category. This behavior is implemented directly in [src/backend/core/settings_manager.cpp](src/backend/core/settings_manager.cpp) and is not guarded by schema-level normalization.
- Risk: Any key layout that does not follow the expected convention will not round-trip correctly and may be saved under the wrong structure or dropped during reload.
- Impact: This creates fragile persistence behavior and makes future schema evolution or key renaming more error-prone.
- Mitigation: Normalize keys at registration and serialization time, validate path structure explicitly, and document the contract for slash-separated keys and category placement.
- Follow-up test recommendation: Add a round-trip test with a nested key path and a key that intentionally does not follow the convention to verify the expected behavior and guard regressions.
