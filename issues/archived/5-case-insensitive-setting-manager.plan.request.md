# Case Insensitive for Cathegories in the Setting Manager

We have implemented several improvements to the setting manager. The next we want to enable is case insensitive to the category. The keys are formated as `{category}/path/to/key` and the schema has a property `category`. Right now both has to be case-match. However, the property `category` is mean to be use for display purpose and therefor will use capitalization and spaces. For example we can have the category `File Editor` which should be resolve to `file-editor`. In the same way the prefix key `file-editor/path/to/key` should be converted to `File Editor`. So, we must expect equality for both keys size that `"File Editor" == "file-editor"`. Also, when we create a schema and the category property is empty we will fill it key the same procedure, for example `my-category/to/my/key` will set the `category` property to `My Category`.

## Goals

- Make the category property and prefix case insenstive and converter between lowercase dash separater to title-case space separated.
- when a schema is added and the category is empty, set it with the prefix converted to title-case space separated.

# Refinement Iteration 1
**Status:** PENDING USER FEEDBACK

## 1. Executive Summary
Implement case-insensitive category matching in `SettingsManager` with bidirectional conversion between normalized key prefixes (lowercase-dash-separated, e.g., `file-editor`) and display-formatted categories (title-case space-separated, e.g., `File Editor`). This affects schema registration, file loading, and file saving operations.

## 2. Refined Requirements & Acceptance Criteria

### Requirement [RM-01]: Category Normalization Helper
- **Description:** Provide internal helper functions to convert between normalized key prefix format and display-formatted category format.
- **Acceptance Criteria:**
  - [ ] Given `"file-editor"`, when converted to display format, then returns `"File Editor"` (dash → space, each segment title-cased)
  - [ ] Given `"my-category"`, when converted to display format, then returns `"My Category"`
  - [ ] Given `"some-deeply-nested-category"`, when converted to display format, then returns `"Some Deeply Nested Category"`
  - [ ] Given `"File Editor"`, when converted to normalized format, then returns `"file-editor"` (space → dash, all lowercase)
  - [ ] Given `"My Category"`, when converted to normalized format, then returns `"my-category"`
  - [ ] Single-word categories are preserved: `"general"` → `"General"` and vice versa

### Requirement [RM-02]: Case-Insensitive Category Matching on Load
- **Description:** When loading settings from a JSON file, the category key in the JSON (which may be in display format) must match the schema's registered category case-insensitively via normalized comparison.
- **Acceptance Criteria:**
  - [ ] Given a JSON file with top-level key `"File Editor"` and a schema registered with category `"file-editor"`, when loading, then the value is successfully loaded (not rejected as a category mismatch)
  - [ ] Given a JSON file with top-level key `"FILE-EDITOR"` and a schema registered with category `"file-editor"`, when loading, then the value is successfully loaded
  - [ ] Given a JSON file with top-level key `"file_editor"` (underscore instead of dash) and a schema registered with category `"file-editor"`, when loading, then the value is rejected as an unknown/mismatched category (only dash-space conversion is supported, not underscore)
  - [ ] Existing behavior where wrong category (e.g., `"WrongCategory"` vs `"performance"`) still rejects the entry is preserved

### Requirement [RM-03]: Auto-Fill Empty Category on Schema Registration
- **Description:** When `registerSchema()` is called with an empty or whitespace-only `category` field, automatically derive the category from the first segment of the schema key using display-format conversion.
- **Acceptance Criteria:**
  - [ ] Given a schema with `key = "file-editor/tab-size"` and `category = ""`, when registered, then the stored category becomes `"File Editor"` (display format)
  - [ ] Given a schema with `key = "my-app/general-setting"` and `category = "   "`, when registered, then the stored category becomes `"My App"` (trimmed and converted)
  - [ ] Given a schema with `key = "general/core/tool_path"` and `category = ""`, when registered, then the stored category becomes `"General"`

### Requirement [RM-04]: Display-Format Category on Save
- **Description:** When saving settings to a JSON file, the top-level category keys in the JSON output must use display format (title-case space-separated) rather than normalized format.
- **Acceptance Criteria:**
  - [ ] Given a schema with key `"file-editor/tab-size"` and category `"File Editor"`, when saving, then the JSON contains a top-level key `"File Editor"` (not `"file-editor"`)
  - [ ] Given a schema with key `"general/core/tool_path"` and category `"General"`, when saving, then the JSON contains a top-level key `"General"`
  - [ ] The nested structure under each category key is preserved correctly

### Requirement [RM-05]: Backward Compatibility with Existing Settings Files
- **Description:** Settings files saved in the previous format (with normalized/lowercase category keys) must still load successfully.
- **Acceptance Criteria:**
  - [ ] Given a JSON file saved by the old code with top-level key `"general"` (lowercase), when loading, then values under that category are loaded correctly
  - [ ] Given a JSON file saved by the old code with top-level key `"performance"`, when loading, then values under that category are loaded correctly
  - [ ] A roundtrip (save → load → save) produces semantically equivalent output, even if the exact casing of top-level keys changes

### Requirement [RM-06]: Schema Lookup Uses Normalized Key
- **Description:** After normalizing the loaded JSON category key to form a flat key (e.g., `"file-editor/tab-size"`), the schema lookup must succeed by matching against the first segment of the registered schema key.
- **Acceptance Criteria:**
  - [ ] Given a loaded flat key `"File Editor/core/tool_path"`, when normalized to `"file-editor/core/tool_path"`, then it matches a registered schema with key `"general/core/tool_path"` only if the categories match after normalization (i.e., `"file-editor"` ≠ `"general"`)
  - [ ] The schema lookup remains O(1) using the normalized flat key — no iteration over all schemas is required

## 3. Scope & Constraints

- **In-Scope:**
  - `src/backend/core/settings_manager.cpp` — modification of `registerSchema()`, `load_from_file()`, and `save_to_file()` methods
  - Addition of internal helper functions for category format conversion (in anonymous namespace)
  - New unit tests in `test/backend/core/setting_manager_test.cpp`
  - No changes to the public API surface (`settings_manager.hpp`)

- **Out-of-Scope:**
  - Changes to the JSON file format schema versioning or migration
  - UI/QML layer changes — this is purely a backend concern
  - Support for other category name formats (e.g., underscore-separated, camelCase)
  - Changes to key syntax validation rules (`isValidKeySyntax`)
  - Multi-language / i18n support for category names

- **Technical Constraints / Edge Cases:**
  - Categories with multiple consecutive spaces (e.g., `"File  Editor"`) — should these be normalized to single spaces? **SEE QUESTION [UX-01]**
  - Categories containing non-alphanumeric characters (e.g., `"C++ Settings"`) — the conversion function must handle these gracefully
  - The existing `test_load_category_mismatch_ignored` test expects strict case-sensitive matching and will need updating
  - The current code in `registerSchema()` already auto-fills empty categories but uses the raw prefix as-is — this behavior changes to use display format

## 4. Open Design Choices (Questions for User)

### [UX/UI]: Multi-Space Handling in Display Format
- **Question:** When converting a normalized prefix like `"file--editor"` (double dash, if that were possible) or when the user provides `"File  Editor"` (double space), should the output be normalized to single spaces (`"File Editor"`) or preserved as-is?
- **Recommendation:** Normalize to single spaces for consistency and clean display.
**User: normalize to single space.**

### [Business Logic]: Schema Category Override Behavior
- **Question:** If a schema is registered with `key = "file-editor/tab-size"` and `category = "Display Preferences"` (a category that does NOT normalize to `"file-editor"`), should:
  - **Option A:** The provided category be accepted as-is (they are independent — key prefix for lookup, category for display)
  - **Option B:** The provided category be rejected or logged as a warning (category should derive from key prefix)
  - **Option C:** The key prefix takes precedence and the stored category is overwritten to match
- **Recommendation:** Option A — the category field is metadata for display purposes and the key prefix is for structural lookup. They serve different roles. However, if they are inconsistent (e.g., `"file-editor"` vs `"Display Preferences"`), a debug-mode warning should be emitted.
**User: the prefix MUST match the category. `key = "file-editor/tab-size"` and `category = "Display Preferences"` must be rejected when the schema is added.**

### [Technical]: Canonical Category Storage
- **Question:** Should the `SettingSchema::category` field store the display-formatted version (e.g., `"File Editor"`) or the normalized version (e.g., `"file-editor"`)?
  - **Option A (Display):** Store display format. Conversion to normalized form happens only during load/save operations. Preserves original user intent but requires conversion on every lookup comparison.
  - **Option B (Normalized):** Store normalized format. Conversion to display format happens only during save operations. Simpler internal logic but loses the original display-formatted value if the user provided one.
- **Recommendation:** Option A — store display format in `category` since it is meant for UI display. Use a separate internal comparison mechanism (normalized key prefix extraction) for lookups.
**User: The `SettingSchema::category` field is mean to store the display format.**

## **User Remark**

The `save_to_file` MUST SAVE THE PREFIX as `file-editor` NOT `File Editor`. So the json would be { 'file-editor' = {'key' = value}}. Make this clear in the requirements.

**ADD NEXT ITERATION BELOW THIS LINE**
---

# Refinement Iteration 2
**Status:** LOCKED

## 1. Executive Summary
Incorporates user answers from Iteration 1: multi-space normalization to single space, strict prefix-category consistency enforcement on schema registration, display-format storage in `SettingSchema::category`, and corrected save behavior using normalized (lowercase-dash-separated) category keys in JSON output instead of display format.

## 2. Refined Requirements & Acceptance Criteria

### Requirement [RM-01]: Category Normalization Helper
- **Description:** Provide internal helper functions to convert between normalized key prefix format and display-formatted category format.
- **Acceptance Criteria:**
  - [ ] Given `"file-editor"`, when converted to display format, then returns `"File Editor"` (dash → space, each segment title-cased)
  - [ ] Given `"my-category"`, when converted to display format, then returns `"My Category"`
  - [ ] Given `"some-deeply-nested-category"`, when converted to display format, then returns `"Some Deeply Nested Category"`
  - [ ] Given `"File Editor"`, when converted to normalized format, then returns `"file-editor"` (space → dash, all lowercase)
  - [ ] Given `"My Category"`, when converted to normalized format, then returns `"my-category"`
  - [ ] Single-word categories are preserved: `"general"` → `"General"` and vice versa
  - [ ] Given `"File  Editor"` (double space), when converted to normalized format, then returns `"file-editor"` (multiple consecutive spaces collapse to single dash)
  - [ ] Given `"file--editor"` (double dash), when converted to display format, then returns `"File Editor"` (multiple consecutive dashes collapse to single space)

### Requirement [RM-02]: Case-Insensitive Category Matching on Load
- **Description:** When loading settings from a JSON file, the category key in the JSON (which may be in any case or format) must match the schema's registered category via normalized comparison.
- **Acceptance Criteria:**
  - [ ] Given a JSON file with top-level key `"File Editor"` and a schema registered with display category `"File Editor"`, when loading, then the value is successfully loaded (normalized key `"file-editor"` matches schema prefix)
  - [ ] Given a JSON file with top-level key `"FILE-EDITOR"` and a schema registered with display category `"File Editor"`, when loading, then the value is successfully loaded
  - [ ] Given a JSON file with top-level key `"file_editor"` (underscore instead of dash) and a schema registered with display category `"File Editor"`, when loading, then the value is rejected as an unknown/mismatched category (only dash-space conversion is supported, not underscore)
  - [ ] Given a JSON file with top-level key `"general"` (lowercase, old format) and a schema registered with display category `"General"`, when loading, then the value is successfully loaded (backward compatibility)
  - [ ] Existing behavior where wrong category (e.g., `"WrongCategory"` vs `"performance"`) still rejects the entry is preserved

### Requirement [RM-03]: Auto-Fill Empty Category on Schema Registration
- **Description:** When `registerSchema()` is called with an empty or whitespace-only `category` field, automatically derive the display-formatted category from the first segment of the schema key.
- **Acceptance Criteria:**
  - [ ] Given a schema with `key = "file-editor/tab-size"` and `category = ""`, when registered, then the stored category becomes `"File Editor"` (display format)
  - [ ] Given a schema with `key = "my-app/general-setting"` and `category = "   "`, when registered, then the stored category becomes `"My App"` (trimmed and converted)
  - [ ] Given a schema with `key = "general/core/tool_path"` and `category = ""`, when registered, then the stored category becomes `"General"`

### Requirement [RM-04]: Schema Prefix-Category Consistency Enforcement
- **Description:** When `registerSchema()` is called with a non-empty `category` field, the provided category must normalize to the same value as the first segment of the schema key. If they do not match, registration is rejected (returns without inserting).
- **Acceptance Criteria:**
  - [ ] Given a schema with `key = "file-editor/tab-size"` and `category = "File Editor"`, when registered, then the schema is accepted (normalized category `"file-editor"` matches key prefix)
  - [ ] Given a schema with `key = "file-editor/tab-size"` and `category = "Display Preferences"`, when registered, then the schema is rejected — not inserted into the registry (normalized category `"display-preferences"` does not match key prefix `"file-editor"`)
  - [ ] Given a schema with `key = "general/core/tool_path"` and `category = "General"`, when registered, then the schema is accepted
  - [ ] Given a schema with `key = "general/core/tool_path"` and `category = "Performance"`, when registered, then the schema is rejected (mismatched category)
  - [ ] Rejected schemas produce no side effects — no partial insertion, no signal emission

### Requirement [RM-05]: Normalized-Format Category on Save
- **Description:** When saving settings to a JSON file, the top-level category keys in the JSON output must use normalized format (lowercase-dash-separated) derived from the display-formatted `category` field.
- **Acceptance Criteria:**
  - [ ] Given a schema with key `"file-editor/tab-size"` and display category `"File Editor"`, when saving, then the JSON contains a top-level key `"file-editor"` (normalized, NOT `"File Editor"`)
  - [ ] Given a schema with key `"general/core/tool_path"` and display category `"General"`, when saving, then the JSON contains a top-level key `"general"`
  - [ ] Given a schema with key `"my-app/general-setting"` and display category `"My App"`, when saving, then the JSON contains a top-level key `"my-app"`
  - [ ] The nested structure under each normalized category key is preserved correctly
  - [ ] Values are grouped by their schema's category prefix only — sub-paths within the key are not treated as additional category boundaries

### Requirement [RM-06]: Backward Compatibility with Existing Settings Files
- **Description:** Settings files saved in any previous format (lowercase, display-formatted, or mixed-case category keys) must still load successfully.
- **Acceptance Criteria:**
  - [ ] Given a JSON file with top-level key `"general"` (all lowercase), when loading, then values under that category are loaded correctly
  - [ ] Given a JSON file with top-level key `"General"` (title case), when loading, then values under that category are loaded correctly
  - [ ] Given a JSON file with top-level key `"GENERAL"` (uppercase), when loading, then values under that category are loaded correctly
  - [ ] A roundtrip (save → load → save) produces semantically equivalent output — all values preserved, types maintained

### Requirement [RM-07]: Schema Lookup Uses Normalized Key
- **Description:** After normalizing the loaded JSON category key to form a flat key (e.g., `"file-editor/tab-size"`), the schema lookup must succeed by matching against the normalized first segment of the registered schema key.
- **Acceptance Criteria:**
  - [ ] Given a loaded flat key `"file-editor/tab-size"`, when looking up schema, then it matches a registered schema with key `"file-editor/tab-size"` (exact match after normalization)
  - [ ] Given a loaded flat key `"File Editor/core/tool_path"`, when normalized to `"file-editor/core/tool_path"`, then it matches a registered schema with key `"general/core/tool_path"` only if the categories match after normalization (i.e., `"file-editor"` ≠ `"general"`, so it is rejected)
  - [ ] The schema lookup remains O(1) using the normalized flat key — no iteration over all schemas is required

## 3. Scope & Constraints

- **In-Scope:**
  - `src/backend/core/settings_manager.cpp` — modification of `registerSchema()`, `load_from_file()`, and `save_to_file()` methods
  - Addition of internal helper functions for category format conversion (in anonymous namespace): `toDisplayFormat()`, `toNormalizedFormat()`
  - New unit tests in `test/backend/core/setting_manager_test.cpp` covering normalization, consistency enforcement, backward compatibility, and save format
  - No changes to the public API surface (`settings_manager.hpp`)

- **Out-of-Scope:**
  - Changes to the JSON file format schema versioning or migration logic
  - UI/QML layer changes — this is purely a backend concern
  - Support for other category name formats (e.g., underscore-separated, camelCase)
  - Changes to key syntax validation rules (`isValidKeySyntax`)
  - Multi-language / i18n support for category names
  - Automatic migration of existing settings files on load

- **Technical Constraints / Edge Cases:**
  - Multiple consecutive dashes or spaces collapse to a single separator during conversion
  - Non-alphanumeric characters in categories (e.g., `"C++ Settings"`) — the display format preserves them as-is; normalization only affects spaces → dashes and lowercasing
  - The existing `test_load_category_mismatch_ignored` test expects strict case-sensitive matching and will need updating to use normalized comparison
  - The current code in `registerSchema()` already auto-fills empty categories but uses the raw prefix as-is — this behavior changes to use display format conversion
  - Save format changed from display (`"File Editor"`) to normalized (`"file-editor"`) — this is a breaking change for external consumers of the settings file format, but backward compatibility on load ensures old files still work

## **LOCKED**
