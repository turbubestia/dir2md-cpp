
#include "settings_manager.hpp"

namespace dir2md::backend {

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

} // namespace dir2md::backend