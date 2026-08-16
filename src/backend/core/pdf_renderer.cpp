#include "pdf_renderer.hpp"

#include <QSizeF>

namespace dir2md::backend {
namespace {

// Error codes surfaced through expected<> for pdf_renderer failures.
inline const int err_open_failed = -1;
inline const int err_no_document = -2;
inline const int err_page_out_of_range = -3;
inline const int err_render_failed = -4;

auto describe_load_error(QPdfDocument::Error error, const QString &file_path) -> QString {
    switch (error) {
        case QPdfDocument::Error::FileNotFound:
            return QString("PDF file not found: %1").arg(file_path);
        case QPdfDocument::Error::InvalidFileFormat:
            return QString("Invalid or corrupted PDF file: %1").arg(file_path);
        case QPdfDocument::Error::IncorrectPassword:
            return QString("PDF file is password protected: %1").arg(file_path);
        case QPdfDocument::Error::UnsupportedSecurityScheme:
            return QString("PDF file uses an unsupported security scheme: %1").arg(file_path);
        case QPdfDocument::Error::DataNotYetAvailable:
            return QString("PDF data not yet available: %1").arg(file_path);
        case QPdfDocument::Error::Unknown:
        case QPdfDocument::Error::None:
        default:
            return QString("Failed to open PDF file: %1").arg(file_path);
    }
}

} // namespace

auto pdf_renderer::open(const QString &file_path) -> expected<int> {
    m_document = std::make_unique<QPdfDocument>();

    const QPdfDocument::Error load_error = m_document->load(file_path);
    if (load_error != QPdfDocument::Error::None) {
        m_document.reset();
        return expected<int>(err_open_failed, describe_load_error(load_error, file_path));
    }

    if (m_document->status() != QPdfDocument::Status::Ready) {
        const QPdfDocument::Error status_error = m_document->error();
        m_document.reset();
        return expected<int>(err_open_failed, describe_load_error(status_error, file_path));
    }

    return expected<int>(m_document->pageCount());
}

auto pdf_renderer::page_count() const -> int {
    if (!m_document) {
        return 0;
    }
    return m_document->pageCount();
}

auto pdf_renderer::render_page(int page_index, int dpi) -> expected<QImage> {
    if (!m_document) {
        return expected<QImage>(err_no_document, "No PDF document is open");
    }

    const int count = m_document->pageCount();
    if (page_index < 0 || page_index >= count) {
        return expected<QImage>(err_page_out_of_range,
                                QString("Page index %1 out of range (0..%2)")
                                    .arg(page_index)
                                    .arg(count - 1));
    }

    // Convert the page size from PDF points (72/inch) to pixels at the
    // requested resolution.
    const QSizeF point_size = m_document->pagePointSize(page_index);
    const qreal scale = static_cast<qreal>(dpi) / 72.0;
    const int width = qMax(1, qRound(point_size.width() * scale));
    const int height = qMax(1, qRound(point_size.height() * scale));

    QImage image = m_document->render(page_index, QSize(width, height));
    if (image.isNull()) {
        return expected<QImage>(err_render_failed,
                                QString("Failed to render page %1").arg(page_index));
    }

    return expected<QImage>(std::move(image));
}

} // namespace dir2md::backend
