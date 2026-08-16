#include <backend/core/pdf_renderer.hpp>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

namespace {

// Build a minimal but valid N-page PDF (correct xref offsets) at path.
auto make_fixture_pdf(const QString &path, int page_count) -> bool {
    QByteArray pdf;
    pdf.append("%PDF-1.4\n");
    pdf.append("\xE2\xE3\xCF\xD3\n");

    QVector<int> offsets;
    auto append_obj = [&](int num, const QByteArray &body) {
        offsets.resize(num + 1);
        offsets[num] = pdf.size();
        pdf.append(QByteArray::number(num)).append(" 0 obj\n");
        pdf.append(body);
        pdf.append("\nendobj\n");
    };

    const int font_num = 3 + 2 * page_count;

    // Pages object (2)
    {
        QByteArray kids;
        for (int i = 0; i < page_count; ++i) {
            kids += QByteArray::number(3 + 2 * i) + " 0 R ";
        }
        append_obj(2, QByteArray("<< /Type /Pages /Kids [") + kids + "] /Count "
                       + QByteArray::number(page_count) + " >>");
    }

    // Catalog (1)
    append_obj(1, "<< /Type /Catalog /Pages 2 0 R >>");

    for (int i = 0; i < page_count; ++i) {
        const int page_num = 3 + 2 * i;
        const int content_num = 4 + 2 * i;

        append_obj(page_num,
                   QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents ")
                       + QByteArray::number(content_num)
                       + " 0 R /Resources << /Font << /F1 "
                       + QByteArray::number(font_num) + " 0 R >> >> >>");

        const QByteArray text =
            QByteArray("BT /F1 24 Tf 72 720 Td (Page ") + QByteArray::number(i + 1) + ") Tj ET";
        append_obj(content_num,
                   QByteArray("<< /Length ") + QByteArray::number(text.size())
                       + " >>\nstream\n" + text + "\nendstream");
    }

    append_obj(font_num, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

    const int xref_offset = pdf.size();
    const int total = font_num + 1;
    pdf.append("xref\n0 ").append(QByteArray::number(total)).append("\n");
    pdf.append("0000000000 65535 f \n");
    for (int i = 1; i < total; ++i) {
        pdf.append(QString("%1 00000 n \n").arg(offsets[i], 10, 10, '0').toLatin1());
    }
    pdf.append("trailer\n<< /Size ").append(QByteArray::number(total))
       .append(" /Root 1 0 R >>\nstartxref\n")
       .append(QByteArray::number(xref_offset)).append("\n%%EOF\n");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(pdf);
    file.close();
    return true;
}

// Count distinct colors in a coarse scan of the image (proves it is not blank).
auto distinct_color_count(const QImage &image) -> int {
    QSet<QRgb> seen;
    const int step = qMax(1, qMin(image.width(), image.height()) / 50);
    for (int y = 0; y < image.height(); y += step) {
        for (int x = 0; x < image.width(); x += step) {
            seen.insert(image.pixel(x, y));
        }
    }
    return seen.size();
}

} // namespace

class pdf_renderer_test : public QObject {
    Q_OBJECT

private slots:
    void test_open_missing_file();
    void test_open_invalid_file();
    void test_page_count_and_render();
    void test_render_without_open();
    void test_no_per_page_files_written();
};

using namespace dir2md::backend;

void pdf_renderer_test::test_open_missing_file() {
    pdf_renderer renderer;
    auto result = renderer.open("C:/definitely/not/a/real/file.pdf");
    QVERIFY(!result.has_value());
    QVERIFY(!result.error().description.isEmpty());
}

void pdf_renderer_test::test_open_invalid_file() {
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString path = temp_dir.filePath("not_a_pdf.pdf");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not a pdf document");
    file.close();

    pdf_renderer renderer;
    auto result = renderer.open(path);
    QVERIFY(!result.has_value());
    QVERIFY(!result.error().description.isEmpty());
}

void pdf_renderer_test::test_page_count_and_render() {
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString path = temp_dir.filePath("fixture.pdf");
    QVERIFY(make_fixture_pdf(path, 2));

    pdf_renderer renderer;
    auto opened = renderer.open(path);
    QVERIFY(opened.has_value());
    QCOMPARE(opened.value(), 2);
    QCOMPARE(renderer.page_count(), 2);

    // 612x792 pt at 150 dpi -> 1275x1650 px
    auto page0 = renderer.render_page(0);
    QVERIFY(page0.has_value());
    QVERIFY(!page0.value().isNull());
    QCOMPARE(page0.value().size(), QSize(1275, 1650));
    QVERIFY(distinct_color_count(page0.value()) > 1);

    auto page1 = renderer.render_page(1);
    QVERIFY(page1.has_value());
    QVERIFY(!page1.value().isNull());
    QCOMPARE(page1.value().size(), QSize(1275, 1650));
    QVERIFY(distinct_color_count(page1.value()) > 1);

    // Out-of-range page index yields a clear error.
    auto out_of_range = renderer.render_page(2);
    QVERIFY(!out_of_range.has_value());
    QVERIFY(!out_of_range.error().description.isEmpty());
}

void pdf_renderer_test::test_render_without_open() {
    pdf_renderer renderer;
    QCOMPARE(renderer.page_count(), 0);
    auto result = renderer.render_page(0);
    QVERIFY(!result.has_value());
    QVERIFY(!result.error().description.isEmpty());
}

void pdf_renderer_test::test_no_per_page_files_written() {
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString path = temp_dir.filePath("fixture.pdf");
    QVERIFY(make_fixture_pdf(path, 2));

    pdf_renderer renderer;
    QVERIFY(renderer.open(path).has_value());
    for (int i = 0; i < renderer.page_count(); ++i) {
        auto page = renderer.render_page(i);
        QVERIFY(page.has_value());
    }

    // Only the fixture PDF itself may exist in the directory.
    const QStringList entries = QDir(temp_dir.path()).entryList(QDir::Files);
    QCOMPARE(entries, QStringList{ "fixture.pdf" });
}

int main(int argc, char *argv[]) {
    // QtPDF rendering requires a QGuiApplication instance.
    QGuiApplication app(argc, argv);
    pdf_renderer_test test;
    return QTest::qExec(&test, argc, argv);
}

#include "pdf_renderer_test.moc"
