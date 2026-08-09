#pragma once

#include <QString>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>
#include <source_location>
#include <sstream>

namespace dir2md::backend {

struct error_frame {
    int error_code;
    QString description;
    std::source_location location;

    error_frame(int code, const QString &desc,
                const std::source_location &loc = std::source_location::current())
        : error_code(code), description(desc), location(loc) {}
};

template <typename T>
class expected {
public:
    expected() : m_has_value(false), m_error(0, "") {}

    explicit expected(T value) : m_has_value(true), m_value(std::move(value)), m_error(0, "") {}

    expected(int code, const QString &desc,
             const std::source_location &loc = std::source_location::current())
        : m_has_value(false), m_error(code, desc, loc) {}

    // State query
    auto has_value() const -> bool { return m_has_value; }

    // Accessors
    auto value_or(T default_value) -> T {
        if (m_has_value) {
            return m_value;
        }
        return std::move(default_value);
    }

    auto value() -> T {
        if (!m_has_value) {
            throw std::runtime_error(m_error.description.toStdString());
        }
        return m_value;
    }

    auto error() const -> const error_frame & { return m_error; }

    // Factory functions
    static auto make_expected(T value) -> expected<T> {
        return expected<T>(std::move(value));
    }

    static auto make_expected_error(int code, const QString &desc,
                                    const std::source_location &loc = std::source_location::current())
        -> expected<T> {
        return expected<T>(code, desc, loc);
    }

    // Copy semantics
    expected(const expected &other) = default;
    auto operator=(const expected &other) -> expected & = default;

    // Move semantics
    expected(expected &&other) noexcept = default;
    auto operator=(expected &&other) noexcept -> expected & = default;

private:
    bool m_has_value;
    T m_value {};
    error_frame m_error;
};

class error_stack {
public:
    auto push(const error_frame &frame) -> void;
    auto frames() const -> const std::vector<error_frame> &;
    auto clear() -> void;
    auto to_string() const -> QString;

private:
    std::vector<error_frame> m_frames;
};

} // namespace dir2md::backend
