#pragma once

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
