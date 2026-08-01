
#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QStringList>
#include <optional>

namespace dir2md::backend {
// Metadata definition for a setting key
struct SettingSchema {
    QString key;          // e.g., "editor/tab_size"
    QString title;        // e.g., "Tab Size"
    QString description;  // e.g., "Number of spaces per tab"
    QString category;     // e.g., "Editor"
    QVariant defaultValue;
    QMetaType type; // QMetaType::Int, QMetaType::Bool, etc.

    // Optional constraints
    QVariant min = QVariant(); // For numeric types
    QVariant max = QVariant(); // For numeric types
    QStringList enumOptions;

    // Validate a candidate value against this schema
    auto isValid(const QVariant &val) const -> bool;
};

class SettingsManager : public QObject {
    Q_OBJECT

public:
    // ctor & dtor ------------------------------------------------------------

    explicit SettingsManager(QObject *parent = nullptr) : QObject(parent) {}

    // accessors --------------------------------------------------------------

    auto get(const QString &key) const -> QVariant;

    auto set(const QString &key, const QVariant &value) -> bool;

    // methods ----------------------------------------------------------------

    auto activeValues() const -> const QHash<QString, QVariant> &;

    void registerSchema(const SettingSchema &schema);

    auto schema(const QString &key) const -> std::optional<SettingSchema>;

    auto schemas() const -> const QHash<QString, SettingSchema> &;

signals:
    void settingChanged(const QString &key, const QVariant &newValue);

private:
    QHash<QString, QVariant> m_values;          // Flat value store ($O(1)$)
    QHash<QString, SettingSchema> m_schemaRegistry; // Schema metadata ($O(1)$)
};

} // namespace dir2md::backend

