
#include "settings_manager.hpp"
#include "assert.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStack>
#include <QVector>
#include <QDebug>

namespace dir2md::backend {

// Anonymous namespace for internal helpers ----------------------------------

namespace {

auto isValidKeySyntax(const QString &key) -> bool
{
    // Reject empty strings
    if (key.isEmpty()) return false;

    // Reject keys containing whitespace
    if (key.contains(QRegularExpression(R"(\s)"))) return false;

    // Reject keys with leading or trailing "/"
    if (key.startsWith('/') || key.endsWith('/')) return false;

    // Reject keys with repeated "//" separators
    if (key.contains("//")) return false;

    // Split on "/" and check for empty segments
    QStringList parts = key.split('/', Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        if (part.isEmpty()) return false;
    }

    return true;
}

auto isWithinPath(const QString &path, const QString &root) -> bool
{
    const QString normalizedPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString normalizedRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    if (normalizedPath.compare(normalizedRoot, Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString rootWithSeparator = normalizedRoot + "/";
    return normalizedPath.startsWith(rootWithSeparator, Qt::CaseInsensitive);
}

auto resolvePersistencePath(const QString &filePath) -> QString
{
    if (filePath.isEmpty()) return QString();

    // Test mode: when a test base directory is set, resolve plain file names
    // inside that directory.  Reject any input containing path separators or
    // absolute components to prevent traversal attacks.
    const QString &testBase = SettingsManager::testBaseDirectory();
    if (!testBase.isEmpty()) {
        if (filePath.contains('/') || filePath.contains('\\')) {
            qWarning() << "Test mode rejects path with separators:" << filePath;
            return QString();
        }
        if (QDir(filePath).isAbsolute()) {
            qWarning() << "Test mode rejects absolute path:" << filePath;
            return QString();
        }
        const QString resolvedPath = QDir::cleanPath(testBase + "/" + filePath);
        return isWithinPath(resolvedPath, testBase) ? resolvedPath : QString();
    }

    // Production mode: only allow simple file names (no separators).
    if (filePath.contains('/') || filePath.contains('\\')) return QString();
    if (QDir(filePath).isAbsolute()) return QString();

    const QString configDirectory = QDir::homePath() + "/.config/dir2md";
    const QString resolvedPath = QDir::cleanPath(configDirectory + "/" + filePath);
    return isWithinPath(resolvedPath, configDirectory) ? resolvedPath : QString();
}

void insertNestedValue(QJsonObject &obj, const QStringList &pathParts, const QVariant &value)
{
    if (pathParts.isEmpty()) {
        return;
    }

    // QJsonObject values are copy-on-write, so collect the path and rebuild it
    // from the leaf upward after inserting the value.
    QVector<QJsonObject> objects;
    objects.reserve(pathParts.size());
    objects.append(obj);

    for (int index = 0; index < pathParts.size() - 1; ++index) {
        const QJsonValue nestedValue = objects.last().value(pathParts[index]);
        objects.append(nestedValue.isObject() ? nestedValue.toObject() : QJsonObject{});
    }

    QJsonObject nestedObject = objects.last();
    nestedObject.insert(pathParts.last(), QJsonValue::fromVariant(value));

    for (int index = pathParts.size() - 2; index >= 0; --index) {
        QJsonObject parentObject = objects[index];
        parentObject.insert(pathParts[index], nestedObject);
        nestedObject = parentObject;
    }

    obj = nestedObject;
}

void flattenJsonObject(const QJsonObject &obj, const QString &prefix, QHash<QString, QVariant> &out)
{
    // Iterative stack-based traversal using stack of (object, prefix) pairs
    QStack<QPair<QJsonObject, QString>> stack;
    stack.push(qMakePair(obj, prefix));

    while (!stack.isEmpty()) {
        QJsonObject currentObj = stack.top().first;
        QString currentPrefix = stack.top().second;
        stack.pop();

        for (auto it = currentObj.begin(); it != currentObj.end(); ++it) {
            QString key = it.key();
            QString fullPath = currentPrefix.isEmpty() ? key : currentPrefix + "/" + key;

            if (it.value().isObject()) {
                stack.push(qMakePair(it.value().toObject(), fullPath));
            } else {
                out.insert(fullPath, it.value().toVariant());
            }
        }
    }
}

auto toDisplayFormat(const QString &normalized) -> QString
{
    // Split on '-' (collapse consecutive dashes into single separator)
    QStringList segments = normalized.split('-', Qt::SkipEmptyParts);
    QStringList titleCased;
    titleCased.reserve(segments.size());
    for (const QString &seg : segments) {
        if (seg.isEmpty()) continue;
        QString title = seg;
        title[0] = title[0].toUpper();
        // Lowercase the rest of the segment (skip first char)
        for (int i = 1; i < title.size(); ++i) {
            title[i] = title[i].toLower();
        }
        titleCased.append(title);
    }
    return titleCased.join(' ');
}

auto toNormalizedFormat(const QString &display) -> QString
{
    // Split on whitespace (collapse consecutive spaces), lowercase each segment, join with '-'
    QStringList segments = display.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
    QStringList lower;
    lower.reserve(segments.size());
    for (const QString &seg : segments) {
        if (seg.isEmpty()) continue;
        lower.append(seg.toLower());
    }
    return lower.join('-');
}

} // anonymous namespace

auto SettingSchema::isValid(const QVariant &val) const -> bool
{
    // First check if conversion is even possible
    if (!val.canConvert(type)) return false;

    // Attempt the actual conversion and verify it succeeds
    QVariant converted = val;
    if (converted.metaType() != type && !converted.convert(type)) return false;

    // For numeric types: validate bounds against the CONVERTED value
    if (type == QMetaType::fromType<int>()) {
        int num = converted.toInt();
        if (min.isValid()) {
            int minVal = min.toInt();
            if (num < minVal) return false;
        }
        if (max.isValid()) {
            int maxVal = max.toInt();
            if (num > maxVal) return false;
        }
    } else if (type == QMetaType::fromType<double>()) {
        double num = converted.toDouble();
        if (min.isValid()) {
            double minVal = min.toDouble();
            if (num < minVal) return false;
        }
        if (max.isValid()) {
            double maxVal = max.toDouble();
            if (num > maxVal) return false;
        }
    }

    // For enum types: validate exact string membership in enumOptions
    if (!enumOptions.isEmpty()) {
        QString strVal = converted.toString();
        if (!enumOptions.contains(strVal)) {
            return false;
        }
    }

    return true;
}

auto convertToSchemaType(const SettingSchema &schema, const QVariant &value) -> std::optional<QVariant>
{
    QVariant converted = value;
    if (converted.metaType() == schema.type) {
        return converted;
    }
    if (!converted.canConvert(schema.type) || !converted.convert(schema.type)) {
        return std::nullopt;
    }
    return converted;
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
    // Enforce key syntax validation
    if (!isValidKeySyntax(key)) {
        return false;
    }

    // Reject unknown keys (must be registered in schema registry)
    if (!m_schemaRegistry.contains(key)) {
        return false;
    }

    // Validate and canonicalize against the registered schema.
    const SettingSchema &schema = m_schemaRegistry[key];
    if (!schema.isValid(value)) {
        return false; // Rejected!
    }

    const auto converted = convertToSchemaType(schema, value);
    if (!converted.has_value()) {
        return false;
    }

    // Store value if it changed
    if (m_values.value(key) != converted.value()) {
        m_values.insert(key, converted.value());
        emit settingChanged(key, converted.value());
    }

    return true;
}

// methods --------------------------------------------------------------------

auto SettingsManager::activeValues() const -> const QHash<QString, QVariant> &
{
    return m_values;
}

auto SettingsManager::registerSchema(const SettingSchema &_schema) -> void
{
    auto schema = _schema; // Make a copy to modify
    RUNTIME_ASSERT(isValidKeySyntax(schema.key));

    // Extract key prefix (first segment before '/')
    const QString keyPrefix = schema.key.section('/', 0, 0);

    if (schema.category.trimmed().isEmpty()) {
        // Auto-fill empty category using display-format conversion from key prefix
        schema.category = toDisplayFormat(keyPrefix);
    } else {
        // Consistency enforcement: normalize both category and key prefix for comparison
        const QString normalizedCategory = toNormalizedFormat(schema.category.trimmed());
        const QString normalizedPrefix = toNormalizedFormat(toDisplayFormat(keyPrefix));
        if (normalizedCategory != normalizedPrefix) {
            // Mismatched category — reject without side effects
            qWarning() << "Category mismatch for key" << schema.key
                       << "expected prefix" << keyPrefix << "with category" << schema.category.trimmed();
            return;
        }
    }

    // If the key already exists, revalidate its active value against the replacement.
    if (m_schemaRegistry.contains(schema.key)) {
        const bool hasActiveValue = m_values.contains(schema.key);
        const QVariant activeValue = m_values.value(schema.key);
        const bool activeValueIsValid = !hasActiveValue || schema.isValid(activeValue);

        m_schemaRegistry.insert(schema.key, schema);

        if (hasActiveValue && !activeValueIsValid) {
            m_values.remove(schema.key);
            emit settingChanged(schema.key, get(schema.key));
        }
        return;
    }

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

void SettingsManager::setTestBaseDirectory(const QString &path)
{
    testBaseDirectory() = path;
}

void SettingsManager::clearTestBaseDirectory()
{
    testBaseDirectory() = QString();
}

QString SettingsManager::testBaseDirectoryPath()
{
    return testBaseDirectory();
}

QString &SettingsManager::testBaseDirectory()
{
    static QString s_testBase;
    return s_testBase;
}

bool SettingsManager::save_to_file(const QString &filePath)
{
    const QString resolvedPath = resolvePersistencePath(filePath);
    if (resolvedPath.isEmpty()) {
        qWarning() << "Invalid save path:" << filePath;
        return false;
    }

    const SettingSchema *invalidSchema = nullptr;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        if (m_schemaRegistry.contains(it.key()) &&
            m_schemaRegistry.value(it.key()).category.trimmed().isEmpty()) {
            invalidSchema = &m_schemaRegistry[it.key()];
            break;
        }
    }
    if (invalidSchema != nullptr) {
        RUNTIME_ASSERT(false);
    }

    // Group values by category (only schema-backed values)
    QJsonObject categoryRoot;

    for (auto it = m_values.begin(); it != m_values.end(); ++it) {
        QString key = it.key();
        QVariant value = it.value();

        // Skip schema-less values (only serialize registered keys)
        if (!m_schemaRegistry.contains(key)) {
            continue;
        }

        // Determine category from schema — use normalized format for JSON keys
        const SettingSchema &schema = m_schemaRegistry[key];
        const QString displayCategory = schema.category.trimmed();
        const QString jsonKey = toNormalizedFormat(displayCategory);

        // Split key on "/" for nesting
        QStringList pathParts = key.split("/", Qt::KeepEmptyParts);
        // Compare normalized forms to handle case differences (key prefix is lowercase,
        // display category is title-case, but both normalize to the same form)
        const QString firstPartNormalized = toNormalizedFormat(pathParts.first());
        const QString displayCategoryNormalized = toNormalizedFormat(displayCategory);
        if (!pathParts.isEmpty() && firstPartNormalized == displayCategoryNormalized) {
            pathParts.removeFirst();
        }

        if (!categoryRoot.contains(jsonKey)) {
            categoryRoot.insert(jsonKey, QJsonObject{});
        }

        QJsonObject catObj = categoryRoot[jsonKey].toObject();
        insertNestedValue(catObj, pathParts, value);
        categoryRoot.insert(jsonKey, QJsonValue(catObj));
    }

    // Serialize with pretty-printing
    QJsonDocument doc(categoryRoot);
    QByteArray jsonBytes = doc.toJson(QJsonDocument::Indented);

    const QFileInfo outputInfo(resolvedPath);
    if (!outputInfo.dir().exists() && !QDir().mkpath(outputInfo.dir().absolutePath())) {
        qWarning() << "Failed to create settings directory:" << outputInfo.dir().absolutePath();
        return false;
    }

    // Write atomically using QSaveFile
    QSaveFile saveFile(resolvedPath);
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
    const QString resolvedPath = resolvePersistencePath(filePath);
    if (resolvedPath.isEmpty()) {
        qWarning() << "Invalid load path:" << filePath;
        return false;
    }

    // Open file
    QFile file(resolvedPath);
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
        return false; // File-level failure: no state change
    }

    if (!doc.isObject()) {
        qWarning() << "Top-level document is not a JSON object";
        return false; // Non-object top level: no state change
    }

    QJsonObject root = doc.object();

    // Flatten the nested JSON (produces keys like "Category/nested/key")
    QHash<QString, QVariant> loadedValues;
    flattenJsonObject(root, QString(), loadedValues);

    // Stage accepted values in a candidate map (atomic commit pattern)
    QHash<QString, QVariant> candidateValues;

    for (auto it = loadedValues.begin(); it != loadedValues.end(); ++it) {
        QString flatKey = it.key();
        QVariant value = it.value();

        // Split the flat key while retaining the category in the schema key.
        QStringList flatParts = flatKey.split("/", Qt::KeepEmptyParts);
        if (flatParts.size() < 2) {
            continue; // Skip empty keys
        }

        QString jsonCategory = flatParts.first();
        QString restOfPath = flatKey.mid(jsonCategory.size() + 1); // +1 for the '/'

        // Find a matching schema by trying normalized category comparison
        const SettingSchema *matchedSchema = nullptr;
        QString matchedSchemaKey;

        for (auto schIt = m_schemaRegistry.cbegin(); schIt != m_schemaRegistry.cend(); ++schIt) {
            const QString &registeredKey = schIt.key();
            QStringList regParts = registeredKey.split('/', Qt::KeepEmptyParts);
            if (regParts.isEmpty()) continue;

            // Compare normalized category forms
            const QString jsonCatNormalized = toNormalizedFormat(jsonCategory);
            const QString regCatNormalized = toNormalizedFormat(schIt->category);

            if (jsonCatNormalized == regCatNormalized) {
                // Check if the rest of the path matches
                QString regRest = registeredKey.mid(regParts.first().size() + 1);
                if (regRest == restOfPath) {
                    matchedSchema = &schIt.value();
                    matchedSchemaKey = registeredKey;
                    break;
                }
            }
        }

        if (!matchedSchema) {
            qWarning() << "No matching schema for" << flatKey << "skipping";
            continue;
        }

        // Validate value against schema
        if (!matchedSchema->isValid(value)) {
            qWarning() << "Invalid value for key" << matchedSchemaKey << "skipping";
            continue;
        }

        // Convert and stage the typed value in candidate map
        QVariant typedValue = value;
        if (typedValue.canConvert(matchedSchema->type)) {
            typedValue.convert(matchedSchema->type);
        }
        candidateValues.insert(matchedSchemaKey, typedValue);
    }

    // Determine which keys changed, were added, or disappeared
    QHash<QString, QVariant> oldValues = m_values;
    QStringList changedKeys;
    QStringList addedKeys;
    QStringList removedKeys;

    // Find added and changed keys
    for (auto it = candidateValues.begin(); it != candidateValues.end(); ++it) {
        QString key = it.key();
        if (!oldValues.contains(key)) {
            addedKeys.append(key);
        } else if (oldValues.value(key) != it.value()) {
            changedKeys.append(key);
        }
    }

    // Find removed keys
    for (auto it = oldValues.begin(); it != oldValues.end(); ++it) {
        QString key = it.key();
        if (!candidateValues.contains(key)) {
            removedKeys.append(key);
        }
    }

    // Atomically replace m_values with candidateValues
    m_values = candidateValues;

    // Emit signals for added and changed keys
    for (const QString &key : changedKeys) {
        emit settingChanged(key, m_values.value(key));
    }
    for (const QString &key : addedKeys) {
        emit settingChanged(key, m_values.value(key));
    }

    // Emit signals for removed keys (fall back to default)
    for (const QString &key : removedKeys) {
        emit settingChanged(key, get(key));
    }

    return true;
}

} // namespace dir2md::backend