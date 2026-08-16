#include "thinking_stripper.hpp"

#include <QStringList>

namespace dir2md::backend {
namespace {

auto open_tag(const QString &name) -> QString {
    return "<" + name + ">";
}

auto close_tag(const QString &name) -> QString {
    return "</" + name + ">";
}

// Tags that may open a block, and the bare names they carry.
auto opening_candidates() -> QStringList {
    return { "think", "reasoning" };
}

// Find needle in haystack at or after from (case-insensitive); -1 if absent.
auto find_tag(const QString &haystack, const QString &needle, int from) -> int {
    if (from < 0 || from > haystack.size()) {
        return -1;
    }
    return haystack.indexOf(needle, from, Qt::CaseInsensitive);
}

} // namespace

auto thinking_stripper::pending_tail_length(const QString &buffer, bool in_block) const -> int {
    QStringList candidates;
    if (in_block) {
        candidates << close_tag(m_open_name);
    } else {
        for (const QString &name : opening_candidates()) {
            candidates << open_tag(name);
        }
    }

    int max_len = 0;
    for (const QString &tag : candidates) {
        max_len = qMax(max_len, tag.size());
    }

    for (int len = qMin(buffer.size(), max_len); len >= 1; --len) {
        const QString suffix = buffer.right(len);
        for (const QString &tag : candidates) {
            if (tag.startsWith(suffix)) {
                return len;
            }
        }
    }
    return 0;
}

auto thinking_stripper::process(const QString &fragment) -> QString {
    m_pending += fragment;

    QString out;
    int pos = 0;
    while (pos < m_pending.size()) {
        if (!m_in_block) {
            // Locate the next complete opening tag at or after pos.
            int open_pos = -1;
            QString open_name;
            for (const QString &name : opening_candidates()) {
                const int found = find_tag(m_pending, open_tag(name), pos);
                if (found >= 0 && (open_pos < 0 || found < open_pos)) {
                    open_pos = found;
                    open_name = name;
                }
            }

            if (open_pos < 0) {
                // No complete opening tag: emit the provably-outside text and
                // hold back a tail that could grow into an opening tag.
                const int safe_end = m_pending.size() - pending_tail_length(m_pending, false);
                if (safe_end > pos) {
                    out += m_pending.mid(pos, safe_end - pos);
                }
                m_pending = m_pending.mid(safe_end);
                return out;
            }

            if (open_pos > pos) {
                out += m_pending.mid(pos, open_pos - pos);
            }
            // Indicator exactly once, on the fragment where the block opens.
            out += thinking_indicator;
            m_in_block = true;
            m_open_name = open_name;
            pos = open_pos + open_tag(open_name).size();
        } else {
            // Discard block content until the matching close tag is fully seen.
            const QString closing = close_tag(m_open_name);
            const int close_pos = find_tag(m_pending, closing, pos);
            if (close_pos < 0) {
                // Hold back only a tail that could grow into the close tag.
                const int safe_end = m_pending.size() - pending_tail_length(m_pending, true);
                m_pending = m_pending.mid(safe_end);
                return out;
            }
            pos = close_pos + closing.size();
            m_in_block = false;
            m_open_name.clear();
        }
    }

    m_pending.clear();
    return out;
}

auto thinking_stripper::flush() -> QString {
    if (m_in_block) {
        // Unclosed block at end of stream: the held tail is a tag fragment,
        // not text — discard it rather than leak it.
        m_pending.clear();
        return "";
    }
    const QString tail = m_pending;
    m_pending.clear();
    return tail;
}

auto thinking_stripper::is_thinking() const -> bool {
    return m_in_block;
}

auto thinking_stripper::reset() -> void {
    m_in_block = false;
    m_open_name.clear();
    m_pending.clear();
}

} // namespace dir2md::backend
