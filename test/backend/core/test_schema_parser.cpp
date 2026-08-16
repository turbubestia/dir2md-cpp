
#include <backend/core/model.hpp>

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
    void test_openai_construct_request_roles();

    // Native parser tests
    void test_native_parse_content_line();
    void test_native_parse_malformed_json();
    void test_native_parse_empty_line();
    void test_native_parse_usage_line();
    void test_native_construct_request_ignores_roles();

    // Schema registry tests
    void test_registry_default_is_openai();
    void test_registry_switch_to_native();
    void test_registry_switch_back_to_openai();
    void test_registry_create_parser_returns_correct_type();
};


using namespace dir2md::backend;

// ============================================================================
// OpenAI parser tests
// ============================================================================

void test_schema_parser::test_openai_parse_streaming_line() {
    openai_schema_parser parser;
    QString line = R"({"choices":[{"delta":{"content":"hello"}}]})";
    QCOMPARE(parser.parse_line(line), QString("hello"));
}

void test_schema_parser::test_openai_parse_non_streaming_line() {
    openai_schema_parser parser;
    QString line = R"({"choices":[{"text":"world"}]})";
    QCOMPARE(parser.parse_line(line), QString("world"));
}

void test_schema_parser::test_openai_parse_malformed_json() {
    openai_schema_parser parser;
    QString line = "this is not json";
    QCOMPARE(parser.parse_line(line), QString(""));
}

void test_schema_parser::test_openai_parse_empty_line() {
    openai_schema_parser parser;
    QCOMPARE(parser.parse_line(""), QString(""));
}

void test_schema_parser::test_openai_parse_usage_line() {
    openai_schema_parser parser;
    QString line = R"({"usage":{"total_tokens":100,"prompt_tokens":50}})";
    auto stats = parser.parse_usage(line);
    QCOMPARE(stats.total_tokens, 100);
    QCOMPARE(stats.prompt_tokens, 50);
}

void test_schema_parser::test_openai_construct_request_roles() {
    openai_schema_parser parser;
    std::vector<chat_message> messages = {
        { message_role::system, "You are a helpful assistant." },
        { message_role::user, "Hello there" },
    };

    QJsonDocument doc = parser.construct_request(messages, 0.7f);
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();

    // The system prompt must be carried with role "system", the user prompt
    // with role "user" (the Phase 5 role fix).
    const QJsonArray arr = root["messages"].toArray();
    QCOMPARE(arr.size(), 2);
    QCOMPARE(arr[0]["role"].toString(), QString("system"));
    QCOMPARE(arr[0]["content"].toString(), QString("You are a helpful assistant."));
    QCOMPARE(arr[1]["role"].toString(), QString("user"));
    QCOMPARE(arr[1]["content"].toString(), QString("Hello there"));

    // Tolerance compare: the payload stores a float, so 0.7f is not exactly 0.7.
    QVERIFY(qAbs(root["temperature"].toDouble() - 0.7) < 1e-6);
    QVERIFY(root["stream"].toBool());
}

// ============================================================================
// Native parser tests
// ============================================================================

void test_schema_parser::test_native_parse_content_line() {
    native_schema_parser parser;
    QString line = R"({"content":"test token"})";
    QCOMPARE(parser.parse_line(line), QString("test token"));
}

void test_schema_parser::test_native_parse_malformed_json() {
    native_schema_parser parser;
    QString line = "invalid json content";
    QCOMPARE(parser.parse_line(line), QString(""));
}

void test_schema_parser::test_native_parse_empty_line() {
    native_schema_parser parser;
    QCOMPARE(parser.parse_line(""), QString(""));
}

void test_schema_parser::test_native_parse_usage_line() {
    native_schema_parser parser;
    QString line = R"({"tokens_predicted":200,"prompt_tokens":80,"prompt_eval_time_ms":50,"eval_time_ms":1000})";
    auto stats = parser.parse_usage(line);
    QCOMPARE(stats.total_tokens, 200);
    QCOMPARE(stats.prompt_tokens, 80);
    QCOMPARE(stats.prompt_eval_time_ms, qint64(50));
    QCOMPARE(stats.generation_time_ms, qint64(1000));
    QVERIFY(stats.tokens_per_sec > 0);
}

void test_schema_parser::test_native_construct_request_ignores_roles() {
    native_schema_parser parser;
    std::vector<chat_message> messages = {
        { message_role::system, "system line" },
        { message_role::user, "user line" },
    };

    QJsonDocument doc = parser.construct_request(messages, 0.7f);
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();

    // The native path has no role concept: contents are concatenated with a
    // newline delimiter and roles are ignored (preserves pre-fix behavior).
    QCOMPARE(root["prompt"].toString(), QString("system line\nuser line"));
    QVERIFY(qAbs(root["temperature"].toDouble() - 0.7) < 1e-6);
    QVERIFY(root["stream"].toBool());
}

// ============================================================================
// Schema registry tests
// ============================================================================

void test_schema_parser::test_registry_default_is_openai() {
    // Reset to default first in case previous tests changed it
    schema_registry::set_active_schema(schema_type::openai);
    QCOMPARE(schema_registry::get_active_schema(), schema_type::openai);
}

void test_schema_parser::test_registry_switch_to_native() {
    schema_registry::set_active_schema(schema_type::native);
    QCOMPARE(schema_registry::get_active_schema(), schema_type::native);
}

void test_schema_parser::test_registry_switch_back_to_openai() {
    schema_registry::set_active_schema(schema_type::openai);
    QCOMPARE(schema_registry::get_active_schema(), schema_type::openai);
}

void test_schema_parser::test_registry_create_parser_returns_correct_type() {
    // Test OpenAI parser creation
    schema_registry::set_active_schema(schema_type::openai);
    auto openai_parser = schema_registry::create_parser();
    QVERIFY(openai_parser != nullptr);

    // Test Native parser creation
    schema_registry::set_active_schema(schema_type::native);
    auto native_parser = schema_registry::create_parser();
    QVERIFY(native_parser != nullptr);

    // Verify parsers work correctly
    QCOMPARE(openai_parser->parse_line(R"({"choices":[{"delta":{"content":"test"}}]})"), QString("test"));
    QCOMPARE(native_parser->parse_line(R"({"content":"native"})"), QString("native"));

    // Reset to default
    schema_registry::set_active_schema(schema_type::openai);
}

// AUTOMOC handles moc generation
QTEST_MAIN(test_schema_parser)
#include "test_schema_parser.moc"
