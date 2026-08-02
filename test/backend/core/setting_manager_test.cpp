#include <QObject>
#include <QTest>
#include <QString>
#include <QVariant>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "setting_manager_test.hpp"

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

    // Getting a non-existent key should return an invalid QVariant.
    QVERIFY(!manager.get("nonexistent").isValid());

    // Set and retrieve a string value.
    QVERIFY(manager.set("test/key", QString("hello")));
    QCOMPARE(manager.get("test/key"), QVariant(QString("hello")));

    // Set and retrieve an int value.
    QVERIFY(manager.set("test/number", 42));
    QCOMPARE(manager.get("test/number"), QVariant(42));

    // Set and retrieve a bool value.
    QVERIFY(manager.set("test/flag", true));
    QCOMPARE(manager.get("test/flag"), QVariant(true));
}

void setting_manager_test::test_settings_manager_active_values()
{
    dir2md::backend::SettingsManager manager;

    // Initially empty.
    QVERIFY(manager.activeValues().isEmpty());

    // After setting values, they should appear.
    manager.set("a/1", 1);
    manager.set("a/2", 2);
    QCOMPARE(manager.activeValues().size(), 2);
    QCOMPARE(manager.activeValues()["a/1"], QVariant(1));
    QCOMPARE(manager.activeValues()["a/2"], QVariant(2));
}

void setting_manager_test::test_settings_manager_schema()
{
    dir2md::backend::SettingsManager manager;

    // No schemas initially.
    QVERIFY(!manager.schema("test/key").has_value());

    // Register a schema.
    dir2md::backend::SettingSchema schema;
    schema.key = "test/key";
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

    manager.set("signal/test", 99);

    QVERIFY(signalEmitted);
    QCOMPARE(emittedKey, QString("signal/test"));
    QCOMPARE(emittedValue.toInt(), 99);
}

// Save/load tests ----------------------------------------------------------

void setting_manager_test::test_save_to_file_creates_json()
{
    dir2md::backend::SettingsManager manager;
    manager.set("test/string", QString("hello"));
    manager.set("test/number", 42);
    manager.set("test/flag", true);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));
    QVERIFY(QFile::exists(filePath));

    // Verify JSON is valid and contains expected keys
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    QVERIFY(!doc.isNull());
    QCOMPARE(parseError.error, QJsonParseError::NoError);

    QJsonObject root = doc.object();
    QVERIFY(root.contains("General"));
    QJsonObject general = root.value("General").toObject();
    // Keys with "/" are nested in JSON
    QVERIFY(general.contains("test"));
    QJsonObject testObj = general.value("test").toObject();
    QCOMPARE(testObj.value("string").toString(), QString("hello"));
    QCOMPARE(testObj.value("number").toInt(), 42);
    QCOMPARE(testObj.value("flag").toBool(), true);
}

void setting_manager_test::test_save_to_file_nested_keys()
{
    dir2md::backend::SettingsManager manager;
    manager.set("editor/tab_size", 4);
    manager.set("editor/indent_style", QString("spaces"));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject root = doc.object();
    QVERIFY(root.contains("General"));
    QJsonObject general = root.value("General").toObject();
    QVERIFY(general.contains("editor"));
    QJsonObject editor = general.value("editor").toObject();
    QCOMPARE(editor.value("tab_size").toInt(), 4);
    QCOMPARE(editor.value("indent_style").toString(), QString("spaces"));
}

void setting_manager_test::test_save_to_file_unregistered_keys_general_category()
{
    dir2md::backend::SettingsManager manager;
    // Set values without registering schemas
    manager.set("custom/key1", QString("value1"));
    manager.set("custom/key2", 123);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject root = doc.object();
    QVERIFY(root.contains("General"));
    QJsonObject general = root.value("General").toObject();
    // Keys with "/" are nested in JSON: { "custom": { "key1": ... } }
    QVERIFY(general.contains("custom"));
    QJsonObject custom = general.value("custom").toObject();
    QCOMPARE(custom.value("key1").toString(), QString("value1"));
}

void setting_manager_test::test_load_from_file_missing_returns_false()
{
    dir2md::backend::SettingsManager manager;
    manager.set("existing/key", 42);

    bool result = manager.load_from_file("/nonexistent/path/settings.json");
    QVERIFY(!result);

    // Values should be unchanged
    QCOMPARE(manager.get("existing/key").toInt(), 42);
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
    bool result = manager.load_from_file(filePath);
    QVERIFY(!result);
}

void setting_manager_test::test_load_from_file_valid_replaces_values()
{
    dir2md::backend::SettingsManager manager;

    // Register schemas so loaded values can be matched
    dir2md::backend::SettingSchema schemaA;
    schemaA.key = "key/a";
    schemaA.type = QMetaType(QMetaType::Int);
    manager.registerSchema(schemaA);

    dir2md::backend::SettingSchema schemaB;
    schemaB.key = "key/b";
    schemaB.type = QMetaType(QMetaType::Int);
    manager.registerSchema(schemaB);

    manager.set("key/a", 1);
    manager.set("key/b", 2);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    // Save initial values
    QVERIFY(manager.save_to_file(filePath));

    // Change values in memory
    manager.set("key/a", 999);
    manager.set("key/b", 888);

    // Load from file should restore original values
    QVERIFY(manager.load_from_file(filePath));
    QCOMPARE(manager.get("key/a").toInt(), 1);
    QCOMPARE(manager.get("key/b").toInt(), 2);
}

void setting_manager_test::test_roundtrip_preserves_types()
{
    dir2md::backend::SettingsManager manager;

    // Register schemas so loaded values can be matched and validated
    dir2md::backend::SettingSchema schemaInt;
    schemaInt.key = "types/int";
    schemaInt.type = QMetaType(QMetaType::Int);
    manager.registerSchema(schemaInt);

    dir2md::backend::SettingSchema schemaDouble;
    schemaDouble.key = "types/double";
    schemaDouble.type = QMetaType(QMetaType::Double);
    manager.registerSchema(schemaDouble);

    dir2md::backend::SettingSchema schemaString;
    schemaString.key = "types/string";
    schemaString.type = QMetaType(QMetaType::QString);
    manager.registerSchema(schemaString);

    dir2md::backend::SettingSchema schemaBool;
    schemaBool.key = "types/bool";
    schemaBool.type = QMetaType(QMetaType::Bool);
    manager.registerSchema(schemaBool);

    manager.set("types/int", 42);
    manager.set("types/double", 3.14);
    manager.set("types/string", QString("hello"));
    manager.set("types/bool", true);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    QVERIFY(manager.save_to_file(filePath));

    // Create a new manager and load
    dir2md::backend::SettingsManager manager2;
    // Register the same schemas in the new manager
    manager2.registerSchema(schemaInt);
    manager2.registerSchema(schemaDouble);
    manager2.registerSchema(schemaString);
    manager2.registerSchema(schemaBool);

    QVERIFY(manager2.load_from_file(filePath));

    QCOMPARE(manager2.get("types/int").toInt(), 42);
    QCOMPARE(manager2.get("types/double").toDouble(), 3.14);
    QCOMPARE(manager2.get("types/string").toString(), QString("hello"));
    QCOMPARE(manager2.get("types/bool").toBool(), true);

    // Verify types match
    QCOMPARE(manager2.get("types/int").metaType().id(), QMetaType::Int);
    QCOMPARE(manager2.get("types/double").metaType().id(), QMetaType::Double);
    QCOMPARE(manager2.get("types/string").metaType().id(), QMetaType::QString);
    QCOMPARE(manager2.get("types/bool").metaType().id(), QMetaType::Bool);
}

void setting_manager_test::test_load_from_file_invalid_value_skipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    // Register a schema with constraints (int between 1 and 32)
    dir2md::backend::SettingsManager manager;
    dir2md::backend::SettingSchema schema;
    schema.key = "limits/value";
    schema.title = "Value";
    schema.defaultValue = 1;
    schema.type = QMetaType(QMetaType::Int);
    schema.min = 1;
    schema.max = 32;
    manager.registerSchema(schema);

    // Manually write a JSON with one valid and one invalid value
    // Structure matches what save_to_file produces: category > nested keys
    QJsonObject root;
    QJsonObject general;
    QJsonObject limits;
    limits.insert("value", QJsonValue(64)); // Invalid: exceeds max
    general.insert("limits", limits);
    root.insert("General", general);

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    bool result = manager.load_from_file(filePath);
    QVERIFY(result);

    // Invalid value should be skipped, so we get the default
    QCOMPARE(manager.get("limits/value").toInt(), 1); // Default value
}

void setting_manager_test::test_load_from_file_unknown_key_silently_ignored()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString filePath = tempDir.path() + "/settings.json";

    // Register only one schema key
    dir2md::backend::SettingsManager manager;
    dir2md::backend::SettingSchema schema;
    schema.key = "known/key";
    schema.title = "Known Key";
    schema.defaultValue = QString("default");
    schema.type = QMetaType(QMetaType::QString);
    manager.registerSchema(schema);

    // Write JSON with both known and unknown keys (matching save_to_file structure)
    QJsonObject root;
    QJsonObject general;
    QJsonObject known;
    known.insert("key", QJsonValue("loaded_value"));
    general.insert("known", known);
    QJsonObject unknown;
    unknown.insert("key", QJsonValue("should_be_ignored"));
    general.insert("unknown", unknown);
    root.insert("General", general);

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    QVERIFY(manager.load_from_file(filePath));

    // Known key should be loaded
    QCOMPARE(manager.get("known/key").toString(), QString("loaded_value"));

    // Unknown key should NOT be in m_values
    QVERIFY(!manager.activeValues().contains("unknown/key"));
}

void setting_manager_test::test_settings_saved_signal_emitted()
{
    dir2md::backend::SettingsManager manager;
    manager.set("signal/test", 42);

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
