#pragma once

#include <QImage>
#include <QString>
#include <memory>

#include <QtPdf/QPdfDocument>

#include "backend/core/expected.hpp"

namespace dir2md::backend {

// Renders a PDF into in-memory page images using QtPDF (QPdfDocument).
// No per-page image files are ever written to disk.
//
// Precondition: a QGuiApplication instance must exist before any method is
// called (QtPDF rendering requirement). In the CLI this is satisfied by the
// QGuiApplication created in main(); in tests, construct one first.
//
// Pages are rendered one at a time so peak memory is bounded by a single page
// image; release each page image after its consumer (e.g. OCR) is done with it.
class pdf_renderer {
public:
    // Load a PDF file by path. On success the value is the document's page
    // count; on failure the error describes why the file could not be opened
    // or parsed (missing file, invalid format, password, ...).
    auto open(const QString &file_path) -> expected<int>;

    // Number of pages in the open document, or 0 when no document is open.
    auto page_count() const -> int;

    // Render a single page (0-based index) into an in-memory QImage at the
    // given resolution (dots per inch, default 150). Returns a clear error
    // when no document is open, the index is out of range, or rendering fails.
    auto render_page(int page_index, int dpi = 150) -> expected<QImage>;

private:
    std::unique_ptr<QPdfDocument> m_document;
};

} // namespace dir2md::backend
