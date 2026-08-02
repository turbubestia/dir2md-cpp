
#include "settings_manager.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDebug>

namespace dir2md::backend {

// Anonymous namespace for internal helpers ----------------------------------

namespace {

void insertNestedValue(QJsonObject &obj, const QStringList &pathParts, const QVariant &value)
{
    if (pathParts.isEmpty()) {
        return;
    }

    if (pathParts.size() == 1) {
        obj.insert(pathParts[0], QJsonValue::fromVariant(value));
        return;
    }

    // Need to nest deeper
    QString first = pathParts[0];
    QJsonObject nested;
    if (!obj[first].isObject()) {
        nested = obj[first].toObject();
    } else {
        nested = obj[first].toObject();
    }

    insertNestedValue(nested, pathParts.mid(1), value);
    obj.insert(first, QJsonValue(nested));
}

void flattenJsonObject(const QJsonObject &obj, const QString &prefix, QHash<QString, QVariant> &out)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        QString key = it.key();
        QString fullPath = prefix.isEmpty() ? key : prefix + "/" + key;

        if (it.value().isObject()) {
            flattenJsonObject(it.value().toObject(), fullPath, out);
        } else {
            out.insert(fullPath, it.value().toVariant());
        }
    }
}

} // anonymous namespace

auto SettingSchema::isValid(const QVariant &val) const -> bool
{
    if (!val.canConvert(type)) return false;

    if (type == QMetaType::fromType<int>() || type == QMetaType::fromType<double>()) {
        double num = val.toDouble();
        if (min.isValid() && num < min.toDouble()) return false;
        if (max.isValid() && num > max.toDouble()) return false;
    }

    if (!enumOptions.isEmpty() && !enumOptions.contains(val.toString())) {
        return false;
    }

    return true;
}

// accessors --------------------------------------------------------------

auto SettingsManager::get(const QString &key) const -> QVariant
{
    // 1. Return user-configured value if present
    if (m_values.contains(key)) {
        return m_values.value(key);
    }

    // 2. Fallback to default value from schema
    if (m_schemaRegistry.contains(key)) {
        return m_schemaRegistry.value(key).defaultValue;
    }

    // 3. Fallback to invalid QVariant if unknown key
    return QVariant();
}

auto SettingsManager::set(const QString &key, const QVariant &value) -> bool
{
    // Validate against schema if registered
    if (m_schemaRegistry.contains(key)) {
        const SettingSchema &schema = m_schemaRegistry[key];
        if (!schema.isValid(value)) {
            return false; // Rejected!
        }
    }

    // Store value if it changed
    if (m_values.value(key) != value) {
        m_values.insert(key, value);
        emit settingChanged(key, value);
    }

    return true;
}

// methods --------------------------------------------------------------------

auto SettingsManager::activeValues() const -> const QHash<QString, QVariant> &
{
    return m_values;
}

auto SettingsManager::registerSchema(const SettingSchema &schema) -> void
{
    m_schemaRegistry.insert(schema.key, schema);
}

auto SettingsManager::schema(const QString &key) const -> std::optional<SettingSchema> {
    if (m_schemaRegistry.contains(key)) {
        return m_schemaRegistry.value(key);
    }
    return std::nullopt;
}

auto SettingsManager::schemas() const -> const QHash<QString, SettingSchema> &
{
    return m_schemaRegistry;
}

bool SettingsManager::save_to_file(const QString &filePath)
{
    // Group values by category
    QJsonObject categoryRoot;

    for (auto it = m_values.begin(); it != m_values.end(); ++it) {
        QString key = it.key();
        QVariant value = it.value();

        // Determine category from schema, default to "General"
        QString category = "General";
        auto sch = schema(key);
        if (sch.has_value()) {
            const QString schemaCategory = sch->category.trimmed();
            if (!schemaCategory.isEmpty()) {
                category = schemaCategory;
            }
        }

        // Split key on "/" for nesting
        QStringList pathParts = key.split("/", Qt::KeepEmptyParts);
        // Remove empty first element from leading "/"
        while (!pathParts.isEmpty() && pathParts.first().isEmpty()) {
            pathParts.removeFirst();
        }

        if (!categoryRoot.contains(category)) {
            categoryRoot.insert(category, QJsonObject{});
        }

        QJsonObject catObj = categoryRoot[category].toObject();
        insertNestedValue(catObj, pathParts, value);
        categoryRoot.insert(category, QJsonValue(catObj));
    }

    // Serialize with pretty-printing
    QJsonDocument doc(categoryRoot);
    QByteArray jsonBytes = doc.toJson(QJsonDocument::Indented);

    // Write atomically using QSaveFile
    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << filePath;
        return false;
    }

    if (saveFile.write(jsonBytes) < 0) {
        qWarning() << "Failed to write to file:" << filePath;
        return false;
    }

    if (!saveFile.commit()) {
        qWarning() << "Failed to commit file (atomic write failed):" << filePath;
        return false;
    }

    // Emit signal on success
    emit settingsSaved(filePath);

    return true;
}

bool SettingsManager::load_from_file(const QString &filePath)
{
    // Open file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false; // Missing/unreadable file — no warning, expected on first run
    }

    // Read and parse
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (doc.isNull() || doc.isEmpty()) {
        qWarning() << "Failed to parse JSON:" << parseError.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    if (root.isEmpty()) {
        return false;
    }

    // Flatten the nested JSON (produces keys like "Category/nested/key")
    QHash<QString, QVariant> loadedValues;
    flattenJsonObject(root, QString(), loadedValues);

    // Clear existing state and populate
    m_values.clear();

    for (auto it = loadedValues.begin(); it != loadedValues.end(); ++it) {
        QString flatKey = it.key();
        QVariant value = it.value();

        // Strip the category prefix: "Category/nested/key" → "nested/key"
        QStringList flatParts = flatKey.split("/", Qt::KeepEmptyParts);
        while (!flatParts.isEmpty() && flatParts.first().isEmpty()) {
            flatParts.removeFirst();
        }
        if (flatParts.isEmpty()) {
            continue; // Skip empty keys
        }
        flatParts.removeFirst(); // Remove category
        QString schemaKey = flatParts.join("/");

        // Look up schema
        auto sch = schema(schemaKey);
        if (!sch.has_value()) {
            // Unknown key — silently skip
            continue;
        }

        // Validate value against schema
        if (!sch->isValid(value)) {
            qWarning() << "Invalid value for key" << schemaKey << "skipping";
            continue;
        }

        QVariant typedValue = value;
        if (typedValue.canConvert(sch->type)) {
            typedValue.convert(sch->type);
        }

        // Insert and emit signal
        m_values.insert(schemaKey, typedValue);
        emit settingChanged(schemaKey, typedValue);
    }

    return true;
}

} // namespace dir2md::backend