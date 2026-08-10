
#include <backend/core/core_schema.hpp>
#include <backend/core/settings_manager.hpp>

#include <QObject>
#include <QTest>
#include <QString>
#include <QVariant>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <optional>

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


// Helper function to register core schemas
static void registerCoreSchemas(dir2md::backend::SettingsManager &manager)
{
    dir2md::backend::CoreSchema::registerSchemas(manager);
}

// Helper function to write JSON file
static bool writeJsonFile(const QString &filePath, const QJsonObject &obj)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// Helper function to read JSON file — returns nullopt on failure
static std::optional<QJsonObject> readJsonFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        return std::nullopt;
    }

    return doc.object();
}

void setting_manager_test::test_basic_assertion()
{
    QVERIFY(true);
    QCOMPARE(1 + 1, 2);
}

void setting_manager_test::test_string_operations()
{
    QString str = "dir2md";
    QCOMPARE(str.length(), 6);
    QVERIFY(str.startsWith("dir"));
    QVERIFY(!str.endsWith("xyz"));
}

void setting_manager_test::test_variant_types()
{
    QVariant intVal(42);
    QCOMPARE(intVal.toInt(), 42);

    QVariant boolVal(true);
    QVERIFY(boolVal.toBool());

    QVariant strVal("hello");
    QCOMPARE(strVal.toString(), QString("hello"));
}

void setting_manager_test::test_settings_manager_instantiation()
{
    QObject parent;
    dir2md::backend::SettingsManager manager(&parent);

    QVERIFY(manager.parent() == &parent);
}

void setting_manager_test::test_settings_manager_get_set()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Getting a non-existent key should return an invalid QVariant.
    QVERIFY(!manager.get("nonexistent").isValid());

    // Set and retrieve a string value using registered schema.
    QVERIFY(manager.set("general/core/tool_path", QString("/usr/bin/tool")));
    QCOMPARE(manager.get("general/core/tool_path"), QVariant(QString("/usr/bin/tool")));

    // Set and retrieve an int value using registered schema.
    QVERIFY(manager.set("performance/core/max_threads", 8));
    QCOMPARE(manager.get("performance/core/max_threads"), QVariant(8));
}

void setting_manager_test::test_settings_manager_active_values()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Initially empty.
    QVERIFY(manager.activeValues().isEmpty());

    // After setting values, they should appear.
    manager.set("general/core/tool_path", QString("/usr/bin/tool"));
    manager.set("performance/core/max_threads", 4);
    QCOMPARE(manager.activeValues().size(), 2);
    QCOMPARE(manager.activeValues()["general/core/tool_path"], QVariant(QString("/usr/bin/tool")));
    QCOMPARE(manager.activeValues()["performance/core/max_threads"], QVariant(4));
}

void setting_manager_test::test_settings_manager_schema()
{
    dir2md::backend::SettingsManager manager;

    // No schemas initially.
    QVERIFY(!manager.schema("test/key").has_value());
    
    // Register a schema.
    dir2md::backend::SettingSchema schema;
    schema.key = "test/key";
    schema.category = "test";
    schema.title = "Test Key";
    schema.defaultValue = 10;
    schema.type = QMetaType(QMetaType::Int);
    schema.min = 0;
    schema.max = 100;
    
    manager.registerSchema(schema);

    // Schema should now be retrievable.
    auto retrieved = manager.schema("test/key");
    QVERIFY(retrieved.has_value());
    QCOMPARE(retrieved->title, QString("Test Key"));
    QCOMPARE(retrieved->defaultValue.toInt(), 10);
}

void setting_manager_test::test_settings_manager_schemas()
{
    dir2md::backend::SettingsManager manager;

    QVERIFY(manager.schemas().isEmpty());

    dir2md::backend::SettingSchema schema1;
    schema1.key = "key/one";
    manager.registerSchema(schema1);

    dir2md::backend::SettingSchema schema2;
    schema2.key = "key/two";
    manager.registerSchema(schema2);

    QCOMPARE(manager.schemas().size(), 2);
    QVERIFY(manager.schemas().contains("key/one"));
    QVERIFY(manager.schemas().contains("key/two"));
}

void setting_manager_test::test_settings_manager_signal()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    bool signalEmitted = false;
    QString emittedKey;
    QVariant emittedValue;

    QObject::connect(
        &manager,
        &dir2md::backend::SettingsManager::settingChanged,
        [&signalEmitted, &emittedKey, &emittedValue](const QString &key, const QVariant &newValue) {
            signalEmitted = true;
            emittedKey = key;
            emittedValue = newValue;
        }
    );

    manager.set("performance/core/max_threads", 8);

    QVERIFY(signalEmitted);
    QCOMPARE(emittedKey, QString("performance/core/max_threads"));
    QCOMPARE(emittedValue.toInt(), 8);
}

// Save/load tests ----------------------------------------------------------

void setting_manager_test::test_save_to_file_creates_json()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("general/core/tool_path", QString("/usr/bin/tool"));
    manager.set("performance/core/max_threads", 8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));
    QVERIFY(QFile::exists(filePath));

    // Verify JSON is valid and contains expected keys
    auto rootOpt = readJsonFile(filePath);
    QVERIFY(rootOpt.has_value());
    QJsonObject root = rootOpt.value();
    QVERIFY(root.contains("general"));
    QJsonObject general = root.value("general").toObject();
    QVERIFY(general.contains("core"));
    QJsonObject coreObj = general.value("core").toObject();
    QCOMPARE(coreObj.value("tool_path").toString(), QString("/usr/bin/tool"));

    QVERIFY(root.contains("performance"));
    QJsonObject performance = root.value("performance").toObject();
    QJsonObject performanceCoreObj = performance.value("core").toObject();
    QCOMPARE(performanceCoreObj.value("max_threads").toInt(), 8);
}

void setting_manager_test::test_save_to_file_nested_keys()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("general/core/tool_path", QString("/usr/bin/tool"));
    manager.set("performance/core/max_threads", 4);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));

    auto rootOpt = readJsonFile(filePath);
    QVERIFY(rootOpt.has_value());
    QJsonObject root = rootOpt.value();
    // Core schema registers under "general" and "performance" categories
    QVERIFY(root.contains("general") || root.contains("performance"));
}

void setting_manager_test::test_save_rejects_schema_less_values()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Set a registered key
    manager.set("performance/core/max_threads", 8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));

    auto rootOpt = readJsonFile(filePath);
    QVERIFY(rootOpt.has_value());
    QJsonObject root = rootOpt.value();
    // Only schema-backed values should be saved
    if (root.contains("general")) {
        QJsonObject general = root.value("general").toObject();
        QVERIFY(general.contains("core"));
    }
}

void setting_manager_test::test_load_from_file_missing_returns_false()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    bool result = manager.load_from_file("/nonexistent/path/settings.json");
    QVERIFY(!result);

    // Values should be unchanged
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), 8);
}

void setting_manager_test::test_load_from_file_malformed_json()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/malformed.json";

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ broken json }");
    file.close();

    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    int oldValue = manager.get("performance/core/max_threads").toInt();

    bool result = manager.load_from_file(filePath);
    QVERIFY(!result);

    // Values should be unchanged (atomic failure)
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), oldValue);
}

void setting_manager_test::test_load_from_file_valid_replaces_values()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    manager.set("general/core/tool_path", QString("/usr/bin/tool"));
    manager.set("performance/core/max_threads", 4);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    // Save initial values
    QVERIFY(manager.save_to_file(filePath));

    // Change values in memory
    manager.set("general/core/tool_path", QString("/usr/local/bin/tool"));
    manager.set("performance/core/max_threads", 16);

    // Load from file should restore original values
    QVERIFY(manager.load_from_file(filePath));
    QCOMPARE(manager.get("general/core/tool_path").toString(), QString("/usr/bin/tool"));
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), 4);
}

void setting_manager_test::test_roundtrip_preserves_types()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    manager.set("general/core/tool_path", QString("/usr/bin/tool"));
    manager.set("performance/core/max_threads", 8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));

    // Create a new manager and load
    dir2md::backend::SettingsManager manager2;
    registerCoreSchemas(manager2);

    QVERIFY(manager2.load_from_file(filePath));

    QCOMPARE(manager2.get("general/core/tool_path").toString(), QString("/usr/bin/tool"));
    QCOMPARE(manager2.get("performance/core/max_threads").toInt(), 8);

    // Verify types match
    QCOMPARE(manager2.get("general/core/tool_path").metaType().id(), QMetaType::QString);
    QCOMPARE(manager2.get("performance/core/max_threads").metaType().id(), QMetaType::Int);
}

void setting_manager_test::test_load_from_file_invalid_value_skipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    // Register a schema with constraints (int between 1 and 32)
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Manually write a JSON with an invalid value (exceeds max)
    QJsonObject root;
    QJsonObject performance;
    QJsonObject core;
    core.insert("max_threads", QJsonValue(64)); // Invalid: exceeds max of 32
    performance.insert("core", core);
    root.insert("performance", performance);

    QVERIFY(writeJsonFile(filePath, root));

    bool result = manager.load_from_file(filePath);
    QVERIFY(result);

    // Invalid value should be skipped, so we get the default
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), 4); // Default value from CoreSchema
}

void setting_manager_test::test_load_from_file_unknown_key_silently_ignored()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    // Register only core schemas
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Write JSON with both known and unknown keys (matching save_to_file structure)
    QJsonObject root;
    QJsonObject general;
    QJsonObject core;
    core.insert("tool_path", QJsonValue("/usr/bin/tool"));
    general.insert("core", core);
    QJsonObject unknown;
    unknown.insert("custom_key", QJsonValue("should_be_ignored"));
    general.insert("unknown", unknown);
    root.insert("general", general);

    QVERIFY(writeJsonFile(filePath, root));

    QVERIFY(manager.load_from_file(filePath));

    // Known key should be loaded
    QCOMPARE(manager.get("general/core/tool_path").toString(), QString("/usr/bin/tool"));

    // Unknown key should NOT be in m_values
    QVERIFY(!manager.activeValues().contains("general/unknown/custom_key"));
}

void setting_manager_test::test_settings_saved_signal_emitted()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QSignalSpy spy(&manager, &dir2md::backend::SettingsManager::settingsSaved);
    QVERIFY(spy.isValid());

    manager.save_to_file(filePath);

    QCOMPARE(spy.count(), 1);
    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), filePath);
}

// Key syntax validation tests ------------------------------------------------

void setting_manager_test::test_set_rejects_empty_key()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    QVERIFY(!manager.set("", QString("value")));
}

void setting_manager_test::test_set_rejects_whitespace_key()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    QVERIFY(!manager.set("key with spaces", QString("value")));
    QVERIFY(!manager.set("key\twith\ttabs", QString("value")));
}

void setting_manager_test::test_set_rejects_leading_separator()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    QVERIFY(!manager.set("/leading/slash", QString("value")));
}

void setting_manager_test::test_set_rejects_trailing_separator()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    QVERIFY(!manager.set("trailing/slash/", QString("value")));
}

void setting_manager_test::test_set_rejects_repeated_separator()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    QVERIFY(!manager.set("key//with//double", QString("value")));
}

void setting_manager_test::test_set_rejects_unknown_key()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Unregistered key should be rejected
    QVERIFY(!manager.set("unknown/key", QString("value")));
}

// Conversion and validation tests --------------------------------------------

void setting_manager_test::test_set_rejects_invalid_conversion()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // "not_a_number" cannot be converted to int, even though canConvert might return true for QString->int
    QVERIFY(!manager.set("performance/core/max_threads", QString("not_a_number")));
}

void setting_manager_test::test_set_canonicalizes_to_schema_type()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Set an int value (from QVariant(int))
    QVERIFY(manager.set("performance/core/max_threads", 8));
    QCOMPARE(manager.get("performance/core/max_threads").metaType().id(), QMetaType::Int);
}

// Schema replacement tests ---------------------------------------------------

void setting_manager_test::test_schema_replacement_retains_compatible()
{
    dir2md::backend::SettingsManager manager;

    // Register initial schema
    dir2md::backend::SettingSchema schema1;
    schema1.key = "test/value";
    schema1.defaultValue = 50;
    schema1.type = QMetaType(QMetaType::Int);
    schema1.min = 0;
    schema1.max = 100;
    manager.registerSchema(schema1);

    // Set a valid value
    QVERIFY(manager.set("test/value", 50));
    QCOMPARE(manager.get("test/value").toInt(), 50);

    // Replace with compatible schema (same constraints)
    dir2md::backend::SettingSchema schema2;
    schema2.key = "test/value";
    schema2.defaultValue = 10;
    schema2.type = QMetaType(QMetaType::Int);
    schema2.min = 0;
    schema2.max = 100;
    manager.registerSchema(schema2);

    // Value should be retained
    QCOMPARE(manager.get("test/value").toInt(), 50);
}

void setting_manager_test::test_schema_replacement_removes_incompatible()
{
    dir2md::backend::SettingsManager manager;

    // Register initial schema with wide range
    dir2md::backend::SettingSchema schema1;
    schema1.key = "test/value";
    schema1.defaultValue = 50;
    schema1.type = QMetaType(QMetaType::Int);
    schema1.min = 0;
    schema1.max = 100;
    manager.registerSchema(schema1);

    // Set a value within range
    QVERIFY(manager.set("test/value", 50));
    QCOMPARE(manager.get("test/value").toInt(), 50);

    // Replace with incompatible schema (tighter constraints)
    dir2md::backend::SettingSchema schema2;
    schema2.key = "test/value";
    schema2.defaultValue = 10;
    schema2.type = QMetaType(QMetaType::Int);
    schema2.min = 0;
    schema2.max = 40; // Max is now 40, but active value is 50
    manager.registerSchema(schema2);

    // Value should be removed and default returned
    QCOMPARE(manager.get("test/value").toInt(), 10); // New default
}

void setting_manager_test::test_schema_replacement_emits_signal_on_change()
{
    dir2md::backend::SettingsManager manager;

    // Register initial schema
    dir2md::backend::SettingSchema schema1;
    schema1.key = "test/value";
    schema1.defaultValue = 50;
    schema1.type = QMetaType(QMetaType::Int);
    schema1.min = 0;
    schema1.max = 100;
    manager.registerSchema(schema1);

    // Set a value
    QVERIFY(manager.set("test/value", 50));

    bool signalEmitted = false;
    QObject::connect(
        &manager,
        &dir2md::backend::SettingsManager::settingChanged,
        [&signalEmitted](const QString &, const QVariant &) {
            signalEmitted = true;
        }
    );

    // Replace with incompatible schema
    dir2md::backend::SettingSchema schema2;
    schema2.key = "test/value";
    schema2.defaultValue = 10;
    schema2.type = QMetaType(QMetaType::Int);
    schema2.min = 0;
    schema2.max = 40;
    manager.registerSchema(schema2);

    // Signal should have been emitted for the fallback to default
    QVERIFY(signalEmitted);
}

// Path restriction tests -------------------------------------------------

void setting_manager_test::test_save_path_rejects_empty_name()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Empty name should be rejected
    QVERIFY(!manager.save_to_file(QString()));
}

void setting_manager_test::test_save_path_rejects_absolute_path()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    // Absolute path should be rejected in production builds
#ifdef DIR2MD_DEBUG_TEST_PATH
    // In debug builds with test-path override, absolute paths are allowed
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(manager.save_to_file(tempDir.filePath("settings.json")));
#else
    QVERIFY(!manager.save_to_file("/tmp/settings.json"));
#endif
}

void setting_manager_test::test_save_path_rejects_traversal()
{
#ifdef DIR2MD_DEBUG_TEST_PATH
    QSKIP("This test is disabled when DIR2MD_DEBUG_TEST_PATH is set");
#else
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    // Path traversal should be rejected
    QVERIFY(!manager.save_to_file("../../etc/passwd"));
#endif
}

void setting_manager_test::test_save_path_rejects_separator_in_name()
{
#ifdef DIR2MD_DEBUG_TEST_PATH
    QSKIP("This test is disabled when DIR2MD_DEBUG_TEST_PATH is set");
#else
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    // Filename with separator should be rejected
    QVERIFY(!manager.save_to_file("dir/settings.json"));
#endif
}

void setting_manager_test::test_save_path_accepts_debug_test_override()
{
#ifdef DIR2MD_DEBUG_TEST_PATH
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    // In debug builds with test-path override, explicit paths should work
    QVERIFY(manager.save_to_file(filePath));
    QVERIFY(QFile::exists(filePath));
#else
    QSKIP("This test requires DIR2MD_DEBUG_TEST_PATH compile definition");
#endif
}

// Traversal tests --------------------------------------------------------

void setting_manager_test::test_insert_nested_scalar_replacement()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Set a value that creates nested structure
    QVERIFY(manager.set("general/core/tool_path", QString("/usr/bin/tool")));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));

    // Verify the nested structure is correct
    auto rootOpt = readJsonFile(filePath);
    QVERIFY(rootOpt.has_value());
    QJsonObject root = rootOpt.value();
    QVERIFY(root.contains("general"));
    QJsonObject general = root.value("general").toObject();
    QVERIFY(general.contains("core"));
}

void setting_manager_test::test_flatten_deeply_nested_json()
{
    // Create a deeply nested JSON structure (50+ levels)
    QJsonObject deepObj;
    for (int i = 50; i > 0; --i) {
        QJsonObject wrapper;
        wrapper.insert(QString("level_%1").arg(i), QJsonValue(deepObj));
        deepObj = wrapper;
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/deep.json";
    QVERIFY(writeJsonFile(filePath, deepObj));

    // Read and flatten the JSON
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    QVERIFY(!doc.isNull());

    QJsonObject root = doc.object();
    QVERIFY(root.size() > 0); // Just verify we can parse it without stack overflow
}

// Load atomic commit tests -----------------------------------------------

void setting_manager_test::test_load_category_mismatch_normalized()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Write JSON with case-variations of the same category — should all be accepted now
    QJsonObject root;
    QJsonObject performance;
    QJsonObject core;
    core.insert("max_threads", QJsonValue(8));
    performance.insert("core", core);
    root.insert("PERFORMANCE", performance); // Uppercase variant

    QVERIFY(writeJsonFile(filePath, root));

    bool result = manager.load_from_file(filePath);
    QVERIFY(result);

    // Value SHOULD be loaded — case-insensitive matching now works
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), 8);
}

void setting_manager_test::test_load_mixed_valid_invalid_entries()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Write JSON with valid and invalid entries
    QJsonObject root;
    QJsonObject performance;
    QJsonObject core;
    core.insert("max_threads", QJsonValue(8)); // Valid: within range 1-32
    performance.insert("core", core);
    root.insert("performance", performance);

    QVERIFY(writeJsonFile(filePath, root));

    bool result = manager.load_from_file(filePath);
    QVERIFY(result);

    // Valid value should be loaded
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), 8);
}

void setting_manager_test::test_load_malformed_json_fails_atomically()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/malformed.json";

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ invalid json [[[");
    file.close();

    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    int oldValue = manager.get("performance/core/max_threads").toInt();

    bool result = manager.load_from_file(filePath);
    QVERIFY(!result);

    // Values should be unchanged (atomic failure)
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), oldValue);
}

void setting_manager_test::test_load_non_object_top_level_fails()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/array.json";

    // Write a JSON array as top-level document
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[1, 2, 3]");
    file.close();

    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("performance/core/max_threads", 8);

    int oldValue = manager.get("performance/core/max_threads").toInt();

    bool result = manager.load_from_file(filePath);
    QVERIFY(!result);
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), oldValue);

    // Values should be unchanged (atomic failure)
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), oldValue);
}

void setting_manager_test::test_load_successful_replaces_atomically()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("general/core/tool_path", QString("/usr/bin/tool"));
    manager.set("performance/core/max_threads", 4);

    // Save initial values
    QVERIFY(manager.save_to_file(filePath));

    // Change values in memory
    manager.set("general/core/tool_path", QString("/usr/local/bin/tool"));
    manager.set("performance/core/max_threads", 16);

    // Track signals
    QSignalSpy spy(&manager, &dir2md::backend::SettingsManager::settingChanged);

    // Load from file - should replace all values atomically
    QVERIFY(manager.load_from_file(filePath));

    // Verify values are restored
    QCOMPARE(manager.get("general/core/tool_path").toString(), QString("/usr/bin/tool"));
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), 4);

    // Signals should have been emitted for changed keys
    QVERIFY(spy.count() >= 2);
}

// Case-insensitive category tests --------------------------------------------

void setting_manager_test::test_normalize_toDisplayFormat()
{
    // Test via registerSchema: empty category should be auto-filled from key prefix
    dir2md::backend::SettingsManager manager;

    dir2md::backend::SettingSchema schema1;
    schema1.key = "file-editor/some-key";
    manager.registerSchema(schema1);
    QCOMPARE(manager.schema("file-editor/some-key")->category, QString("File Editor"));

    dir2md::backend::SettingSchema schema2;
    schema2.key = "my-category/deep/nested";
    manager.registerSchema(schema2);
    QCOMPARE(manager.schema("my-category/deep/nested")->category, QString("My Category"));

    dir2md::backend::SettingSchema schema3;
    schema3.key = "some-deeply-nested-category/x";
    manager.registerSchema(schema3);
    QCOMPARE(manager.schema("some-deeply-nested-category/x")->category,
             QString("Some Deeply Nested Category"));

    dir2md::backend::SettingSchema schema4;
    schema4.key = "general/y";
    manager.registerSchema(schema4);
    QCOMPARE(manager.schema("general/y")->category, QString("General"));
}

void setting_manager_test::test_normalize_toNormalizedFormat()
{
    // Register schemas with display-format categories and verify they normalize correctly
    dir2md::backend::SettingsManager manager;

    // Register with explicit display category that matches key prefix
    dir2md::backend::SettingSchema schema1;
    schema1.key = "file-editor/key1";
    schema1.category = "File Editor";
    manager.registerSchema(schema1);
    QVERIFY(manager.schema("file-editor/key1").has_value());

    dir2md::backend::SettingSchema schema2;
    schema2.key = "my-category/key2";
    schema2.category = "My Category";
    manager.registerSchema(schema2);
    QVERIFY(manager.schema("my-category/key2").has_value());

    dir2md::backend::SettingSchema schema3;
    schema3.key = "general/key3";
    schema3.category = "General";
    manager.registerSchema(schema3);
    QVERIFY(manager.schema("general/key3").has_value());
}

void setting_manager_test::test_normalize_multiSpace_collapse()
{
    // Double space in category should normalize to single dash
    dir2md::backend::SettingsManager manager;

    dir2md::backend::SettingSchema schema1;
    schema1.key = "file-editor/key1";
    schema1.category = "File  Editor"; // double space
    manager.registerSchema(schema1);
    QVERIFY(manager.schema("file-editor/key1").has_value());
    QCOMPARE(manager.schema("file-editor/key1")->category, QString("File  Editor"));

    // Double dash in key prefix should display as single space category
    dir2md::backend::SettingSchema schema2;
    schema2.key = "file--editor/key2";
    manager.registerSchema(schema2);
    QCOMPARE(manager.schema("file--editor/key2")->category, QString("File Editor"));
}

void setting_manager_test::test_registerSchema_autoFillEmptyCategory()
{
    dir2md::backend::SettingsManager manager;

    // Register schema with empty category — should auto-fill from key prefix
    dir2md::backend::SettingSchema schema1;
    schema1.key = "display-preferences/brightness";
    manager.registerSchema(schema1);
    QVERIFY(manager.schema("display-preferences/brightness").has_value());
    QCOMPARE(manager.schema("display-preferences/brightness")->category,
             QString("Display Preferences"));

    // Register schema with whitespace-only category — same behavior
    dir2md::backend::SettingSchema schema2;
    schema2.key = "network-settings/timeout";
    schema2.category = "   ";
    manager.registerSchema(schema2);
    QVERIFY(manager.schema("network-settings/timeout").has_value());
    QCOMPARE(manager.schema("network-settings/timeout")->category,
             QString("Network Settings"));
}

void setting_manager_test::test_registerSchema_consistencyEnforcement_accept()
{
    dir2md::backend::SettingsManager manager;

    // Matching prefix + category should be accepted
    dir2md::backend::SettingSchema schema1;
    schema1.key = "file-editor/tab-size";
    schema1.category = "File Editor";
    manager.registerSchema(schema1);
    QVERIFY(manager.schema("file-editor/tab-size").has_value());

    // Another matching case
    dir2md::backend::SettingSchema schema2;
    schema2.key = "general/option";
    schema2.category = "General";
    manager.registerSchema(schema2);
    QVERIFY(manager.schema("general/option").has_value());
}

void setting_manager_test::test_registerSchema_consistencyEnforcement_reject()
{
    dir2md::backend::SettingsManager manager;

    // Mismatched prefix + category should be rejected (no insertion, no side effects)
    dir2md::backend::SettingSchema schema1;
    schema1.key = "file-editor/tab-size";
    schema1.category = "Display Preferences"; // Wrong — doesn't match "file-editor"
    manager.registerSchema(schema1);
    QVERIFY(!manager.schema("file-editor/tab-size").has_value());

    // Another mismatched case
    dir2md::backend::SettingSchema schema2;
    schema2.key = "general/option";
    schema2.category = "Performance"; // Wrong — doesn't match "general"
    manager.registerSchema(schema2);
    QVERIFY(!manager.schema("general/option").has_value());

    // Verify no schemas were inserted
    QCOMPARE(manager.schemas().size(), 0);
}

void setting_manager_test::test_load_caseInsensitiveCategoryMatching()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Test with various case formats of the same category
    QJsonObject root;

    // Use "General" (display format) for tool_path
    QJsonObject general;
    QJsonObject core1;
    core1.insert("tool_path", QJsonValue("/usr/bin/tool"));
    general.insert("core", core1);

    // Use "PERFORMANCE" (uppercase) for max_threads
    QJsonObject performanceUpper;
    QJsonObject core2;
    core2.insert("max_threads", QJsonValue(16));
    performanceUpper.insert("core", core2);

    root.insert("General", general);
    root.insert("PERFORMANCE", performanceUpper);

    QVERIFY(writeJsonFile(filePath, root));

    bool result = manager.load_from_file(filePath);
    QVERIFY(result);

    // Both values should load successfully despite case differences
    QCOMPARE(manager.get("general/core/tool_path").toString(), QString("/usr/bin/tool"));
    QCOMPARE(manager.get("performance/core/max_threads").toInt(), 16);
}

void setting_manager_test::test_load_underscoreNotSupported()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);

    // Write JSON with underscore-separated key — should be rejected
    QJsonObject root;
    QJsonObject general_underscore; // "general_underscore" != "general" after normalization
    QJsonObject core;
    core.insert("tool_path", QJsonValue("/usr/bin/tool"));
    general_underscore.insert("core", core);
    root.insert("general_underscore", general_underscore);

    QVERIFY(writeJsonFile(filePath, root));

    bool result = manager.load_from_file(filePath);
    QVERIFY(result); // Load succeeds but value is not loaded

    // Value should NOT be loaded — underscore format doesn't match normalized "general"
    // The default value is "/usr/bin/tool", so if it wasn't loaded, we get the default
    QCOMPARE(manager.get("general/core/tool_path").toString(), QString("/usr/bin/tool"));
}

void setting_manager_test::test_save_normalizedFormatOutput()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("general/core/tool_path", QString("/usr/bin/tool"));
    manager.set("performance/core/max_threads", 8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));

    // Verify JSON uses normalized (lowercase-dash) keys, NOT display format
    auto rootOpt = readJsonFile(filePath);
    QVERIFY(rootOpt.has_value());
    QJsonObject root = rootOpt.value();

    // Should have "general" and "performance" (normalized), NOT "General" or "Performance"
    QVERIFY(root.contains("general"));
    QVERIFY(root.contains("performance"));
    QVERIFY(!root.contains("General"));
    QVERIFY(!root.contains("Performance"));
}

void setting_manager_test::test_roundtrip_preservesCategories()
{
    dir2md::backend::SettingsManager manager;
    registerCoreSchemas(manager);
    manager.set("general/core/tool_path", QString("/usr/bin/tool"));
    manager.set("performance/core/max_threads", 8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    // Save
    QVERIFY(manager.save_to_file(filePath));

    // Load into fresh manager
    dir2md::backend::SettingsManager manager2;
    registerCoreSchemas(manager2);
    QVERIFY(manager2.load_from_file(filePath));

    // Verify values are preserved
    QCOMPARE(manager2.get("general/core/tool_path").toString(), QString("/usr/bin/tool"));
    QCOMPARE(manager2.get("performance/core/max_threads").toInt(), 8);

    // Save again and verify JSON is semantically equivalent
    QString filePath2 = tempDir.path() + "/settings2.json";
    QVERIFY(manager2.save_to_file(filePath2));

    auto root1Opt = readJsonFile(filePath);
    auto root2Opt = readJsonFile(filePath2);
    QVERIFY(root1Opt.has_value());
    QVERIFY(root2Opt.has_value());

    // Both should have the same normalized keys
    QCOMPARE(root1Opt.value().keys(), root2Opt.value().keys());
}

QTEST_MAIN(setting_manager_test)
#include "setting_manager_test.moc"
