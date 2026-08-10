
#include <backend/core/model.hpp>

#include <QObject>
#include <QTest>

class test_prompt_sanitization : public QObject {
    Q_OBJECT

private slots:
    // Sanitization tests
    void test_sanitize_begin_of_text_token();
    void test_sanitize_end_of_text_token();
    void test_sanitize_normal_text_unchanged();
    void test_sanitize_multiple_tokens();

    // Post-processing tests
    void test_strip_zero_width_spaces();
    void test_strip_zero_width_spaces_no_effect_on_clean_text();
};

using namespace dir2md::backend;

// ============================================================================
// Sanitization tests
// ============================================================================

void test_prompt_sanitization::test_sanitize_begin_of_text_token() {
    QString input = "<|begin_of_text|>";
    QString sanitized = text_to_text_client::sanitize_prompt(input);

    // Should contain zero-width spaces
    QVERIFY(sanitized.contains(QChar(0x200B)));
    // Should still contain the original delimiters
    QVERIFY(sanitized.contains("<"));
    QVERIFY(sanitized.contains(">"));
}

void test_prompt_sanitization::test_sanitize_end_of_text_token() {
    QString input = "<|end_of_text|>";
    QString sanitized = text_to_text_client::sanitize_prompt(input);

    QVERIFY(sanitized.contains(QChar(0x200B)));
}

void test_prompt_sanitization::test_sanitize_normal_text_unchanged() {
    QString input = "This is normal text without special tokens.";
    QString sanitized = text_to_text_client::sanitize_prompt(input);

    QCOMPARE(sanitized, input);
    QVERIFY(!sanitized.contains(QChar(0x200B)));
}

void test_prompt_sanitization::test_sanitize_multiple_tokens() {
    QString input = "Hello <|begin_of_text|> world <|end_of_text|>!";
    QString sanitized = text_to_text_client::sanitize_prompt(input);

    // Should have two zero-width spaces (one per token, 2 per token = 4 total)
    int zwsp_count = 0;
    for (QChar c : sanitized) {
        if (c == QChar(0x200B)) {
            zwsp_count++;
        }
    }
    // Each token has 2 zero-width spaces inserted
    QCOMPARE(zwsp_count, 4);
}

// ============================================================================
// Post-processing tests
// ============================================================================

void test_prompt_sanitization::test_strip_zero_width_spaces() {
    QString input = "Hello\u200B world\u200B!";
    QString output = text_to_text_client::strip_zero_width_spaces(input);

    QCOMPARE(output, QString("Hello world!"));
    QVERIFY(!output.contains(QChar(0x200B)));
}

void test_prompt_sanitization::test_strip_zero_width_spaces_no_effect_on_clean_text() {
    QString input = "Clean text without special characters.";
    QString output = text_to_text_client::strip_zero_width_spaces(input);

    QCOMPARE(output, input);
}

// AUTOMOC handles moc generation
QTEST_MAIN(test_prompt_sanitization)
#include "test_prompt_sanitization.moc"
