#include "expected.hpp"

namespace dir2md::backend {

auto error_stack::push(const error_frame &frame) -> void {
    m_frames.push_back(frame);
}

auto error_stack::frames() const -> const std::vector<error_frame> & {
    return m_frames;
}

auto error_stack::clear() -> void {
    m_frames.clear();
}

auto error_stack::to_string() const -> QString {
    if (m_frames.empty()) {
        return "";
    }

    QString result;
    for (const auto &frame : m_frames) {
        result += QString("Error %1: %2\n")
                      .arg(frame.error_code)
                      .arg(frame.description);
        if (frame.location.file_name()) {
            result += QString("  at %1:%2\n")
                          .arg(QString::fromStdString(frame.location.file_name()))
                          .arg(frame.location.line());
        }
    }
    return result;
}

} // namespace dir2md::backend
