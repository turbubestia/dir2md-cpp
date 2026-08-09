#pragma once

#include <QObject>
#include <QTest>

class test_schema_parser : public QObject {
    Q_OBJECT

private slots:
    // OpenAI parser tests
    void test_openai_parse_streaming_line();
    void test_openai_parse_non_streaming_line();
    void test_openai_parse_malformed_json();
    void test_openai_parse_empty_line();
    void test_openai_parse_usage_line();

    // Native parser tests
    void test_native_parse_content_line();
    void test_native_parse_malformed_json();
    void test_native_parse_empty_line();
    void test_native_parse_usage_line();

    // Schema registry tests
    void test_registry_default_is_openai();
    void test_registry_switch_to_native();
    void test_registry_switch_back_to_openai();
    void test_registry_create_parser_returns_correct_type();
};
