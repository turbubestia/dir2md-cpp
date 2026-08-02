# Improve and Fix the Settings Manager

In this task we focus in updating the settings_manager to solve the security and bugs documented in `docs\internal\backend\core\settings_manager-Grok-4.5.md` while also we will correct some of the semantic and expected behavior of the manager.

## Code improvements

### 1. Improve `insertNestedValue()` method

This method is programmed using recursion. We must avoid it to prefer a stack-based traversal. So using a stack we traverse the json object to insert the regquested pair key/value. Doing this we also must fix the fingind **5. `insertNestedValue` branch is redundant; intermediate non-objects can be overwritten**.

### 2. Add validations to where save/load the settings

Where not really a bug, this can harden the security of the app. Solve the finding **8. Unvalidated file path and unrestricted write/read location**. We will want to restrict save the setting to the `{QDir::homePath()}/.config/dir2md` path only. We will only allow change the filename for test purposes.

### 3. Improve `flattenJsonObject` method
This method also use recursion, which must converted to non-recursive method using a stack-based algorithm.

### 4. Load is atomic
The load_from_file method calls `m_values.clear();` before parsing the flatened json. If something fails or throw we will lose all the settings. The load method must construct a new object and only when it succesfully finish assign it to m_values (or swap it).

### 5. QVariant conversion
The Qt documentation says
```
bool QVariant::canConvert(QMetaType type) const
Returns true if the variant's type can be cast to the requested type, type. Such casting is done automatically when calling the toInt(), toBool(), ... methods.
Note this function operates only on the variant's type, not the contents. It indicates whether there is a conversion path from this variant to type, not that the conversion will succeed when attempted.
```
This means canConvert only state if we can cast from/to the given type, but does not guarranty that convert() will success. We must check the return bool value of convert().

## Resolve bugs

These are the documented bugs that needs to be solved (by their finding number in the `docs\internal\backend\core\settings_manager-Grok-4.5.md` file):
3. No signal when effective value disappears after clear-without-replace
4. `SettingSchema::isValid` conversion and enum checks are coarse
5. `insertNestedValue` branch is redundant; intermediate non-objects can be overwritten
6. Path edge cases: empty segments and category strip
10. `registerSchema` overwrite and stale values

Also the load_from_file is striping the cathegory from the flatten key string. The cathegory is part of the key, since there can be the same property path/name under different cathegories. See line 233 in `settings_manager.cpp`.

## No Action

Leave with no actions the next finding

7. `activeValues()` / `schemas()` return internal references
9. Thread safety and re-entrancy

## Thread-Safety

The class settings_manager is not designed to be thread-safety. Worker tasks must campture actual values and baked them in their internal data structure.

## Setting key/value semantics

### 1. Unknown setting keys not allows
If the schema does not have a given key, then the settings must be rejected, and do not throw a exception. Currently, if a key is not found in the schema, the schema validation is skip and the value is inserted anyway.

### 2. First part of the key must match the cathegory
The, for example, `editor/line-stop` correspond to the `line-stop` property of the `editor` cathegory.

### 3. No white spaces allowed in the key
Spaces are not allowed in the key, so the next string `editor / line-stop` would be invalid. We must check for key validation.

### 4. No empty sections
Empty sections in a key are not allowed, so the string `editor//line-stop` is not allowed.

### 5. Unknown keys in the setting files must be ignored
If there is a key not found in the schema then it must be ignored and not inserted. For now is enough to print a warning like `key {key} not valid`.

---
# Refinement Iteration 1
**Status:** PENDING USER FEEDBACK

## 1. Executive Summary
Improve the settings manager so that setting keys, schema registration, value conversion, and persisted data follow explicit and predictable rules. The implementation must avoid recursive JSON traversal, preserve existing values when loading fails, restrict settings-file access to the application's configuration directory, and reject or ignore invalid data without throwing for ordinary validation failures.

## 2. Refined Requirements & Acceptance Criteria
- **Requirement SM-01: Validate setting keys consistently**
	- **Description:** A setting key must contain non-empty path segments, must not contain whitespace, and must begin with the category represented by the registered schema. Keys that do not satisfy these rules are invalid.
	- **Acceptance Criteria:**
		- [ ] Given a key with an empty segment, leading/trailing separator, or whitespace, when it is submitted, then it is rejected and no value is inserted or changed.
		- [ ] Given a key whose first segment does not match its category, when it is submitted, then it is rejected and no value is inserted or changed.
		- [ ] Given an unknown key, when it is submitted, then it is rejected without throwing an exception.

- **Requirement SM-02: Enforce schema validation and QVariant conversion**
	- **Description:** Values may be accepted only when their key has a registered schema and the value can be successfully converted to the schema's declared type. Conversion capability alone is insufficient; the conversion operation must succeed. Enum values must be validated against the allowed enum values rather than only against a broad type category.
	- **Acceptance Criteria:**
		- [ ] Given a key without a registered schema, when a value is submitted, then the value is not stored and the manager does not throw for that validation failure.
		- [ ] Given a value whose conversion operation fails, when it is submitted, then the value is not stored and the existing value remains unchanged.
		- [ ] Given an enum schema, when a value is outside the declared enum set, then it is rejected even if its underlying type is otherwise convertible.
		- [ ] Given a valid value, when it is submitted, then the stored value conforms to the schema type and normal change notifications are preserved.

- **Requirement SM-03: Maintain schema and value consistency**
	- **Description:** Registering a schema for an existing category/property must define deterministic overwrite behavior and must not leave values that are incompatible with the replacement schema. Replaced or removed schemas must not leave stale active values.
	- **Acceptance Criteria:**
		- [ ] Given an existing schema, when a replacement schema is registered, then the resulting schema and active value state follow the agreed overwrite policy.
		- [ ] Given an active value incompatible with a replacement or removed schema, when the schema changes, then that stale value is removed or otherwise handled according to the agreed policy and cannot be returned as valid.
		- [ ] Effective-value change notifications are emitted when a schema change or clear operation causes the effective value to disappear.

- **Requirement SM-04: Use bounded settings-file locations**
	- **Description:** Settings persistence must read and write only within the application's configuration directory under the user's home directory. The filename may remain configurable for tests, but callers must not be able to select an arbitrary directory or escape the allowed directory through path traversal or absolute-path input.
	- **Acceptance Criteria:**
		- [ ] Given a valid filename, when settings are saved or loaded, then the resolved file is inside the application's configuration directory.
		- [ ] Given an absolute path, parent-directory traversal, an empty filename, or a filename that resolves outside the allowed directory, when persistence is requested, then the request is rejected before file I/O.
		- [ ] The configuration directory is created or handled according to the agreed failure policy when it does not exist.

- **Requirement SM-05: Make loading atomic**
	- **Description:** Loading must build and validate a complete candidate settings state before replacing the active state. A failed read, parse, key validation, or value conversion must not partially modify the current settings.
	- **Acceptance Criteria:**
		- [ ] Given valid persisted settings, when loading succeeds, then the active state is replaced with the complete validated state.
		- [ ] Given an unreadable, malformed, or otherwise invalid settings file, when loading fails, then all settings that were active before the operation remain active.
		- [ ] Persisted flattened keys retain their category as part of the key and are not incorrectly stripped before schema lookup.
		- [ ] Unknown persisted keys are ignored, a warning is produced, and valid persisted keys are processed according to the atomic-load policy.

- **Requirement SM-06: Replace recursive JSON traversal**
	- **Description:** Nested JSON insertion and flattening must use stack-based traversal so that behavior does not depend on call-stack depth. Intermediate non-object nodes may be replaced when required to create the requested nested object path.
	- **Acceptance Criteria:**
		- [ ] Given deeply nested JSON data, when it is flattened or a nested value is inserted, then the operation completes without recursive call-stack growth.
		- [ ] Given an intermediate path segment that is not an object, when a nested value is inserted, then that segment is replaced as needed and the requested value is reachable at the complete path.
		- [ ] Empty path segments are rejected before traversal.

- **Requirement SM-07: Preserve intentional no-action boundaries**
	- **Description:** This change does not alter the ownership or thread-safety contracts of the settings manager. Internal references returned by existing APIs and thread-safety/re-entrancy behavior remain outside this task unless required to implement the requirements above.
	- **Acceptance Criteria:**
		- [ ] No API-copying redesign is introduced for active values or schemas as part of this change.
		- [ ] Worker tasks continue to capture the settings they need rather than relying on concurrent access to the manager.

## 3. Scope & Constraints
- **In-Scope:** Key syntax and category validation; rejection of unknown keys; schema replacement and stale-value handling; precise type and enum validation; atomic loading; category-preserving flattened-key handling; stack-based JSON flattening and insertion; bounded settings-file paths; warnings for unknown persisted keys; regression tests for the listed behaviors and edge cases.
- **Out-of-Scope:** Making the manager thread-safe or re-entrant; changing the public reference-returning APIs solely to provide copies; redesigning worker-task data capture; unrelated UI, CLI, or persistence-format changes.
- **Technical Constraints / Edge Cases:** The allowed configuration directory is the user's home configuration subdirectory for this application. Path validation must account for absolute paths, parent traversal, empty names, separators, and platform-specific path normalization. Loading must not partially apply valid entries if the selected atomic-load policy treats any validation failure as a failed load. JSON traversal must handle deep nesting and intermediate scalar values. Notifications must reflect changes to effective values, including disappearance after clearing without replacement.

## 4. Open Design Choices (Questions for User)
- **[Business Logic]:** When registering a replacement schema, should an existing compatible active value be retained, should every active value for that schema be cleared, or should retention depend on successful revalidation against the replacement schema?
**User: when replacing an schema and if the value exist then it match be validated against the new schema. If is valid then can be kept, but if is invalid then it must be removed in which case reding the key will return the default value registered by the schema.**

- **[Business Logic]:** When a settings file contains both valid and invalid/unknown entries, should loading ignore invalid entries and commit the valid subset, or should any invalid entry cause the entire load to fail atomically?
**User: We must ensure to read as many settings as posible to the application can start, therefore we simple will ignore invalid/unknown entires. We will not enforce their persistance after a save operation, therefore as results of a load/save the invalid/unknown will be removed. This las behavior is accepted for now.**

- **[Technical]:** What exact persistence API should remain configurable for tests: a filename only, a filename plus an optional test directory, or another mechanism? Production calls must still resolve beneath the application's configuration directory.
**For test we need to be able to set the file and path, but for production only the filename. Therefore a compilation flag that allows in debug mode bypass the path place constraint.**

- **[Technical]:** What should happen when the configuration directory cannot be created or a settings file cannot be read or written: throw a domain-specific error, return a failure status, or use another existing error contract?
**User: for now print the error to stdout. We will decide later how to handle error and logs application wise.**

- **[Technical]:** For enum schemas, should accepted values be represented and persisted as their underlying numeric values, symbolic names, or both?
**User: symbolic names is preferable if posible. But for this the enum must be register like shown next**
```cpp
enum State {
    Idle,
    Running,
    Completed,
    Failed
};
Q_ENUM(State) // Tells MOC to register string names for State
```
it is also possible (and prefered) to do this
```cpp
#include <QMetaEnum>

Status::State state = Status::Completed;

// Get key string using QMetaEnum
QMetaEnum metaEnum = QMetaEnum::fromType<Status::State>();
const char* key = metaEnum.valueToKey(state);

qDebug() << key; // Output: "Completed"
```

- **[Technical]:** What notification is expected when a clear operation removes an effective value: one notification carrying an invalid/empty value, a dedicated removal notification, or the existing signal with a defined sentinel value?
**User: We don't have defined how to consume the event. Leave as no action any event notification changed.**
---

# Refinement Iteration 2
**Status:** PENDING USER FEEDBACK

## 1. Executive Summary
This iteration resolves the design questions from the previous refinement. Schema replacement will preserve an existing value only when it remains valid, loading will accept all valid persisted settings while ignoring invalid or unknown entries, and production persistence will remain confined to the application configuration directory. Test builds may explicitly bypass that path restriction, while persistence errors are reported to stdout until the application's broader logging policy is defined.

## 2. Refined Requirements & Acceptance Criteria
- **Requirement SM-08: Revalidate values when replacing schemas**
	- **Description:** When a schema replaces an existing schema for the same setting, an existing active value must be validated against the replacement schema. A valid value is retained; an invalid value is removed so that subsequent reads resolve to the replacement schema's default value.
	- **Acceptance Criteria:**
		- [ ] Given a setting with an existing value and a replacement schema, when the existing value satisfies the replacement schema, then the value remains active.
		- [ ] Given a setting with an existing value and a replacement schema, when the existing value does not satisfy the replacement schema, then the value is removed.
		- [ ] Given an invalid existing value removed during schema replacement, when the setting is read, then the replacement schema's default value is returned.
		- [ ] Given a schema replacement that changes the effective value, when the replacement is completed, then the manager's existing effective-value notification behavior is preserved; no new event-consumption contract is introduced by this task.

- **Requirement SM-09: Load valid persisted settings independently**
	- **Description:** Loading must process as many persisted settings as possible. Unknown keys, malformed entries, invalid keys, and values that fail schema validation are ignored individually, while valid entries are applied. Invalid or unknown entries are not retained for a later save and therefore disappear when the current settings are saved.
	- **Acceptance Criteria:**
		- [ ] Given a file containing valid and invalid or unknown entries, when it is loaded, then every valid entry is made available to the application and invalid or unknown entries are ignored.
		- [ ] Given ignored entries followed by a successful save, when the saved file is read, then the ignored entries are absent.
		- [ ] Given an invalid entry, when it is ignored during loading, then the load operation does not fail solely because of that entry.
		- [ ] A failure to read or parse the overall file remains distinct from an individual invalid setting and follows the persistence-error policy.

- **Requirement SM-10: Separate production and test persistence paths**
	- **Description:** In production, callers may choose only the settings filename and the resolved path must remain below `{QDir::homePath()}/.config/dir2md`. A test-only build configuration may permit an explicit file and path so tests can use isolated temporary locations. The bypass must not be available through normal production behavior.
	- **Acceptance Criteria:**
		- [ ] Given a production build, when persistence is requested, then the caller cannot select a directory outside the application configuration directory.
		- [ ] Given a test-enabled build, when a test supplies an explicit file and path, then persistence can use that location without weakening production path validation.
		- [ ] Given a production request containing traversal, an absolute path, or another escape from the configuration directory, when it is validated, then no file I/O occurs outside the allowed directory.
		- [ ] The mechanism used to enable the test-only bypass is explicit in the build configuration and cannot be activated merely by a runtime filename value.

- **Requirement SM-11: Report persistence errors using the interim policy**
	- **Description:** Until the application-wide logging and error-handling policy is defined, failures to create the configuration directory or read/write a settings file must be reported to stdout. This interim behavior does not define the future application logging API.
	- **Acceptance Criteria:**
		- [ ] Given a directory-creation failure, when persistence is attempted, then an explanatory error is printed to stdout.
		- [ ] Given a file read or write failure, when persistence is attempted, then an explanatory error is printed to stdout.
		- [ ] Error reporting does not expose settings values or other unnecessary sensitive content.

- **Requirement SM-12: Prefer symbolic enum persistence**
	- **Description:** Enum settings should use symbolic names for validation and persistence when the enum is registered with Qt's meta-object system, such as an enum exposed through `Q_ENUM`. The manager must use the registered enum metadata to map between accepted names and enum values.
	- **Acceptance Criteria:**
		- [ ] Given a Qt meta-object-registered enum and a valid symbolic name, when the setting is loaded or assigned, then it is accepted and converted to the corresponding enum value.
		- [ ] Given a Qt meta-object-registered enum and an unknown symbolic name, when the setting is loaded or assigned, then it is rejected or ignored according to the operation's validation policy.
		- [ ] Given a valid enum value, when settings are saved, then its symbolic name is persisted where Qt metadata provides that name.
		- [ ] Given an enum for which a symbolic name cannot be obtained, the fallback representation and behavior follow the agreed compatibility policy rather than silently producing an ambiguous value.

## 3. Scope & Constraints
- **In-Scope:** Applying the resolved decisions from Iteration 1; revalidating active values during schema replacement; retaining only valid persisted entries; removing ignored entries on a subsequent save; separating production path validation from an explicit test-build path override; interim stdout error reporting; Qt meta-enum-based symbolic enum handling; regression tests covering mixed valid/invalid files and schema replacement.
	- **Out-of-Scope:** Defining the application's final logging framework; defining consumers for effective-value notifications; making the manager thread-safe or re-entrant; changing the reference-returning APIs; preserving unknown settings across save operations.
	- **Technical Constraints / Edge Cases:** The test-only path override must be compile-time or build-configuration controlled and must not be inferred from arbitrary runtime input. Individual invalid entries must not prevent valid entries from loading, while file-level I/O or parse failures must retain the existing active state according to the atomic-load requirement. Symbolic enum persistence depends on Qt meta-object registration and must not assume that every enum has a discoverable name.

## 4. Open Design Choices (Questions for User)
- **[Technical]:** For a meta-enum that has a valid numeric value but no symbolic key, should persistence fall back to the underlying numeric representation, or should that enum setting be omitted and reported as invalid?
**User: Fallback to numeric value and in debug mode print to stdout a warning. This case would be more related to a software bug.**

- **[Technical]:** Should the test-only path override be enabled by a dedicated CMake option/compile definition, or is a debug-build-only condition sufficient for the project's test and production build matrix?
**User: yes a cmake symbol set in the `debug` config preset.**

- **[Technical]:** When a file is syntactically valid JSON but has an invalid top-level structure, should this be treated as a file-level load failure that preserves all current values, or as an empty/invalid entry set that leaves current values unchanged while reporting the error?
**User: any top level mismatch is a failure and the current values will be left untouched.**
---

# Refinement Iteration 3
**Status:** LOCKED

## 1. Executive Summary
The settings-manager requirements are now complete and unambiguous. Schema replacement, tolerant loading, production path restrictions, debug-only test path access, interim error reporting, enum serialization, and top-level document validation have all been decided. The implementation and regression tests must satisfy the complete set of requirements accumulated in the preceding iterations.

## 2. Refined Requirements & Acceptance Criteria
- **Requirement SM-13: Define enum serialization fallback**
	- **Description:** Enum values must be persisted using their symbolic Qt meta-enum key whenever one is available. If a valid enum value has no symbolic key, persistence must fall back to its underlying numeric value. Debug builds must print a warning for this fallback because it indicates a likely software defect.
	- **Acceptance Criteria:**
		- [ ] Given a valid enum value with a registered symbolic key, when settings are saved, then the symbolic key is persisted.
		- [ ] Given a valid enum value without a discoverable symbolic key, when settings are saved, then the underlying numeric value is persisted.
		- [ ] Given the numeric fallback case in a debug build, when settings are saved, then a warning is printed to stdout.
		- [ ] The numeric fallback remains readable and converts back to the declared enum type during loading.

- **Requirement SM-14: Control test path access through CMake**
	- **Description:** The ability to provide an explicit settings path for isolated tests must be enabled by a dedicated CMake compile definition set by the `debug` configuration preset. Runtime input alone must not enable the production path bypass.
	- **Acceptance Criteria:**
		- [ ] Given the `debug` preset, when the project is configured, then the dedicated test-path compile definition is available to the relevant test target or targets.
		- [ ] Given a production configuration without that definition, when persistence is requested, then only the configured application directory and filename rules are available.
		- [ ] Given a test build with the definition, when an explicit temporary file and path are supplied, then tests can read and write that location.
		- [ ] The test-only path capability is not enabled solely by a filename, environment value, or arbitrary runtime path.

- **Requirement SM-15: Reject invalid top-level JSON atomically**
	- **Description:** A settings document with valid JSON syntax but an unexpected top-level structure is a file-level load failure. The manager must report the failure and leave every setting currently active before the load untouched.
	- **Acceptance Criteria:**
		- [ ] Given a syntactically valid JSON document with an invalid top-level type or structure, when it is loaded, then the load is reported as failed.
		- [ ] Given such a top-level mismatch, when loading completes, then no valid or invalid entries from that document are applied.
		- [ ] Given such a top-level mismatch, when loading completes, then all settings active before loading remain unchanged.
		- [ ] The failure is reported using the interim persistence error policy without exposing setting values.

- **Requirement SM-16: Complete locked settings-manager contract**
	- **Description:** The implementation must satisfy all requirements from SM-01 through SM-15. Individual invalid or unknown setting entries in an otherwise valid settings document are ignored so valid entries can be loaded, while file-level read, parse, and top-level-structure failures are atomic and preserve the prior state. Production file access is restricted to `{QDir::homePath()}/.config/dir2md`; explicit path access exists only under the CMake-controlled debug test configuration.
	- **Acceptance Criteria:**
		- [ ] Regression tests cover invalid key syntax, category mismatches, unknown keys, conversion failures, enum validation, schema replacement, stale-value removal, effective-value disappearance, deep JSON traversal, intermediate scalar replacement, category-preserving flattened keys, path traversal rejection, mixed valid/invalid settings, enum numeric fallback, and invalid top-level documents.
		- [ ] The implementation does not change the explicitly out-of-scope thread-safety, re-entrancy, reference-returning API, or event-consumption contracts.
		- [ ] The complete project test suite passes under the supported debug configuration.

## 3. Scope & Constraints
- **In-Scope:** All settings-manager behavior specified by SM-01 through SM-16; implementation and tests for validation, schema/value consistency, stack-based JSON traversal, atomic file loading, bounded production paths, debug-configured test paths, tolerant entry handling, stdout error reporting, symbolic enum persistence, numeric enum fallback, and top-level document validation.
	- **Out-of-Scope:** Thread safety; re-entrancy redesign; changing `activeValues()` or `schemas()` to return copies; defining application-wide logging; defining consumers or a new contract for effective-value notifications; retaining unknown or invalid entries during save; unrelated UI, CLI, or persistence-format redesign.
	- **Technical Constraints / Edge Cases:** Production path validation must reject absolute paths, traversal, empty filenames, empty path segments, and platform-specific normalization escapes. A valid JSON document with an invalid top-level structure is atomic failure, whereas invalid individual entries are skipped. Enum numeric fallback is permitted for values without a symbolic key and must be warned about only in debug builds. The debug test-path bypass is controlled by a dedicated CMake symbol set in the `debug` preset.

---
**LOCKED**
