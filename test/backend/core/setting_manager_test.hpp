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

    // Test SettingsManager get/set without schemas (basic value store).
    void test_settings_manager_get_set();

    // Test SettingsManager activeValues() returns stored values.
    void test_settings_manager_active_values();

    // Test SettingsManager schema registration and retrieval.
    void test_settings_manager_schema();

    // Test SettingsManager schemas() returns all registered schemas.
    void test_settings_manager_schemas();

    // Test SettingsManager signal emission on set.
    void test_settings_manager_signal();
};
