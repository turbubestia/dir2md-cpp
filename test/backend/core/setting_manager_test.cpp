#include <QObject>
#include <QTest>
#include <QString>
#include <QVariant>

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
