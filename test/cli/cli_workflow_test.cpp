#include <backend/core/core_schema.hpp>
#include <backend/core/settings_manager.hpp>
#include <cli_common.hpp>
#include <chat_workflow.hpp>
#include <ocr_workflow.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace dir2md::cli;

class cli_workflow_test : public QObject {
    Q_OBJECT

private slots:
    // Workflow exit codes & option isolation (no network calls).
    void test_chat_missing_prompt();
    void test_ocr_missing_source();
    void test_chat_rejects_ocr_only_option();
    void test_ocr_rejects_chat_only_option();
    void test_chat_bad_temperature();
    void test_chat_help_exits_zero();
    void test_ocr_help_exits_zero();

    // cli_common resolvers (pure, network-free).
    void test_resolve_temperature();
    void test_resolve_model_name();
    void test_resolve_markdown_path();
    void test_assemble_pdf_markdown();
    void test_resolve_text_or_file();
};

// ============================================================================
// Workflow exit codes & option isolation
// ============================================================================

void cli_workflow_test::test_chat_missing_prompt() {
    QCOMPARE(execute_chat({ "chat" }), 1);
}

void cli_workflow_test::test_ocr_missing_source() {
    QCOMPARE(execute_ocr({ "ocr" }), 1);
}

void cli_workflow_test::test_chat_rejects_ocr_only_option() {
    // --source belongs to the ocr workflow; chat's parser must reject it.
    QCOMPARE(execute_chat({ "chat", "--source", "x" }), 1);
}

void cli_workflow_test::test_ocr_rejects_chat_only_option() {
    // --prompt belongs to the chat workflow; ocr's parser must reject it.
    QCOMPARE(execute_ocr({ "ocr", "--prompt", "x" }), 1);
}

void cli_workflow_test::test_chat_bad_temperature() {
    // A valid system prompt lets resolution reach temperature validation,
    // where the non-numeric value must be rejected (no request is sent).
    QCOMPARE(execute_chat({ "chat", "--prompt", "hi", "--system", "be nice",
                            "--temperature", "abc" }), 1);
}

void cli_workflow_test::test_chat_help_exits_zero() {
    QCOMPARE(execute_chat({ "chat", "--help" }), 0);
}

void cli_workflow_test::test_ocr_help_exits_zero() {
    QCOMPARE(execute_ocr({ "ocr", "--help" }), 0);
}

// ============================================================================
// cli_common resolvers
// ============================================================================

void cli_workflow_test::test_resolve_temperature() {
    dir2md::backend::SettingsManager settings;
    dir2md::backend::CoreSchema::registerSchemas(settings);

    // No option -> falls back to the cli/temperature default (0.7).
    auto fallback = resolve_temperature(std::nullopt, settings);
    QVERIFY(fallback.has_value());
    QCOMPARE(fallback.value(), 0.7);

    // Valid in-range value is accepted.
    auto valid = resolve_temperature(QString("1.5"), settings);
    QVERIFY(valid.has_value());
    QCOMPARE(valid.value(), 1.5);

    // Non-numeric value is rejected.
    auto not_a_number = resolve_temperature(QString("abc"), settings);
    QVERIFY(!not_a_number.has_value());

    // Out-of-range value is rejected.
    auto out_of_range = resolve_temperature(QString("3.0"), settings);
    QVERIFY(!out_of_range.has_value());
}

void cli_workflow_test::test_resolve_model_name() {
    dir2md::backend::SettingsManager settings;
    dir2md::backend::CoreSchema::registerSchemas(settings);

    // The schema default is empty -> hard error.
    auto missing = resolve_model_name("language-model/model-name", settings);
    QVERIFY(!missing.has_value());

    // A configured value resolves (trimmed).
    settings.set("language-model/model-name", "gpt-4o-mini");
    auto present = resolve_model_name("language-model/model-name", settings);
    QVERIFY(present.has_value());
    QCOMPARE(present.value(), QString("gpt-4o-mini"));

    // Whitespace-only is treated as empty -> hard error.
    settings.set("ocr-model/model-name", "   ");
    auto blank = resolve_model_name("ocr-model/model-name", settings);
    QVERIFY(!blank.has_value());
}

void cli_workflow_test::test_resolve_markdown_path() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString src = temp.filePath("doc.pdf");  // absolute path

    // Alongside the source by default.
    QCOMPARE(resolve_markdown_path(src, QString()), temp.filePath("doc.md"));

    // Into the output folder when given (base name preserved).
    const QString out = temp.filePath("out");
    QCOMPARE(resolve_markdown_path(src, out), QDir(out).absoluteFilePath("doc.md"));
}

void cli_workflow_test::test_assemble_pdf_markdown() {
    QCOMPARE(assemble_pdf_markdown({ "page one", "page two" }),
             QString("---\n**page 1**\n\npage one\n---\n**page 2**\n\npage two\n"));
}

void cli_workflow_test::test_resolve_text_or_file() {
    // Literal text (not an existing file) is returned verbatim.
    auto literal = resolve_text_or_file("just some text");
    QVERIFY(literal.has_value());
    QCOMPARE(literal.value(), QString("just some text"));

    // An existing readable file returns its contents.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath("prompt.txt");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("file contents here");
    }
    auto from_file = resolve_text_or_file(path);
    QVERIFY(from_file.has_value());
    QCOMPARE(from_file.value(), QString("file contents here"));
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    cli_workflow_test test;
    return QTest::qExec(&test, argc, argv);
}

#include "cli_workflow_test.moc"
