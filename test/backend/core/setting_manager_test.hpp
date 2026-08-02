#pragma once

#include <QObject>
#include <QTest>
#include <QString>
#include <QVariant>

#include <backend/core/settings_manager.hpp>

class setting_manager_test : public QObject {
    Q_OBJECT

private slots:
    // POC: verify test infrastructure works (no dependency on SettingsManager).
    void test_basic_assertion();

    // Test QString functionality to validate QtTest integration.
    void test_string_operations();

    // Test QVariant basic operations (relevant to SettingsManager API).
    void test_variant_types();

    // Test that SettingsManager can be instantiated (integration POC).
    void test_settings_manager_instantiation();

    // Key syntax validation tests --------------------------------------------

    void test_set_rejects_empty_key();
    void test_set_rejects_whitespace_key();
    void test_set_rejects_leading_separator();
    void test_set_rejects_trailing_separator();
    void test_set_rejects_repeated_separator();
    void test_set_rejects_unknown_key();

    // Schema-backed set/get tests --------------------------------------------

    void test_settings_manager_get_set();
    void test_settings_manager_active_values();
    void test_settings_manager_schema();
    void test_settings_manager_schemas();
    void test_settings_manager_signal();

    // Conversion and validation tests ----------------------------------------

    void test_set_rejects_invalid_conversion();
    void test_set_canonicalizes_to_schema_type();

    // Schema replacement tests -----------------------------------------------

    void test_schema_replacement_retains_compatible();
    void test_schema_replacement_removes_incompatible();
    void test_schema_replacement_emits_signal_on_change();

    // Save/load tests --------------------------------------------------------

    void test_save_to_file_creates_json();
    void test_save_to_file_nested_keys();
    void test_save_rejects_schema_less_values();
    void test_load_from_file_missing_returns_false();
    void test_load_from_file_malformed_json();
    void test_load_from_file_valid_replaces_values();
    void test_roundtrip_preserves_types();
    void test_load_from_file_invalid_value_skipped();
    void test_load_from_file_unknown_key_silently_ignored();
    void test_settings_saved_signal_emitted();

    // Path restriction tests -------------------------------------------------

    void test_save_path_rejects_empty_name();
    void test_save_path_rejects_absolute_path();
    void test_save_path_rejects_traversal();
    void test_save_path_rejects_separator_in_name();
    void test_save_path_accepts_debug_test_override();

    // Traversal tests --------------------------------------------------------

    void test_insert_nested_scalar_replacement();
    void test_flatten_deeply_nested_json();

    // Load atomic commit tests -----------------------------------------------

    void test_load_category_mismatch_normalized();
    void test_load_mixed_valid_invalid_entries();
    void test_load_malformed_json_fails_atomically();
    void test_load_non_object_top_level_fails();
    void test_load_successful_replaces_atomically();

    // Case-insensitive category tests ----------------------------------------

    void test_normalize_toDisplayFormat();
    void test_normalize_toNormalizedFormat();
    void test_normalize_multiSpace_collapse();
    void test_registerSchema_autoFillEmptyCategory();
    void test_registerSchema_consistencyEnforcement_accept();
    void test_registerSchema_consistencyEnforcement_reject();
    void test_load_caseInsensitiveCategoryMatching();
    void test_load_underscoreNotSupported();
    void test_save_normalizedFormatOutput();
    void test_roundtrip_preservesCategories();
};
