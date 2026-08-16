#pragma once

#include <QString>

namespace dir2md::backend {

// The indicator emitted (exactly once per block) on the fragment where a
// think/reasoning block first opens. Consumers that accumulate text for file
// output should strip this constant so files contain only the stripped text.
inline const QString thinking_indicator = "thinking ...";

// Stateful stripper that consumes streamed text fragments and emits only the
// non-thinking text. Recognizes `think` and `reasoning` block tags (open and
// close forms, case-insensitive) and is safe across chunk boundaries: a tag
// split across multiple fragments is carried in a pending buffer between
// calls, so partial tags are neither dropped nor leaked.
//
// The `thinking ...` indicator is emitted exactly once on the fragment where a
// block first opens (not repeated on subsequent fragments while the block is
// open). Call flush() at end of stream to release any held partial-tag tail,
// and reset() to reuse one instance for the next source/request.
class thinking_stripper {
public:
    // Process one streamed fragment and return the text to emit for it.
    auto process(const QString &fragment) -> QString;

    // Release any pending tail held back as a potential partial tag. Call at
    // end of stream so trailing text is not lost from accumulated output.
    auto flush() -> QString;

    // True while a think/reasoning block is in progress.
    auto is_thinking() const -> bool;

    // Clear all internal state so the instance can be reused for the next
    // source/request.
    auto reset() -> void;

private:
    // Length of the longest suffix of buffer that could grow into a tag of
    // the kind relevant to the current state (opening tags outside a block,
    // the matching closing tag inside one).
    auto pending_tail_length(const QString &buffer, bool in_block) const -> int;

    bool m_in_block = false;
    QString m_open_name;  // bare tag name ("think"/"reasoning") of the open block
    QString m_pending;    // trailing input that could be the start of a tag
};

} // namespace dir2md::backend
