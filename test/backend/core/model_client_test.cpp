
#include <backend/core/model.hpp>
#include <backend/core/mock_model_server.hpp>

#include <QSignalSpy>
#include <QObject>
#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

class model_client_test : public QObject {
    Q_OBJECT

private slots:
    // Property tests
    void test_endpoint_url_property();
    void test_model_name_property();
    void test_temperature_property();
    void test_coalescing_interval_property();

    // Stream property tests (R1)
    void test_stream_property_default_false();
    void test_stream_property_set_emits_once();
    void test_stream_property_same_value_no_emit();

    // Request payload stream flag tests (R2)
    void test_payload_carries_stream_true_openai();
    void test_payload_carries_stream_false_openai();
    void test_payload_carries_stream_true_native();
    void test_payload_carries_stream_false_native();
    void test_image_payload_carries_stream_true();
    void test_image_payload_carries_stream_false();

    // Response format matrix tests (R3)
    void test_sse_openai_streaming();
    void test_ndjson_openai_streaming();
    void test_nonstreaming_openai_body();
    void test_sse_native_streaming();
    void test_nonstreaming_native_body();
    void test_dedicated_reasoning_discarded();
    void test_inline_tags_preserved_verbatim();

    // Robustness tests (R4)
    void test_split_line_reassembled_exactly_once();
    void test_malformed_line_warns_and_completes();
    void test_done_sentinel_completion_fires_once();
    void test_stop_true_completion_fires_once();

    // Coalescing gating tests (R5)
    void test_incremental_chunks_emitted_when_streaming();
    void test_no_incremental_chunks_when_nonstreaming();

    // Busy state tests
    void test_initial_not_busy();
    void test_cancel_resets_busy();

    // Cancel tests
    void test_cancel_signal_emitted();

    // Token stats tests
    void test_token_stats_default_values();
};

using namespace dir2md::backend;

// ============================================================================
// Request driver helpers
//
// Point a real client at the in-process mock, drive send_request(), pump the
// event loop until completion/error (with a safety timeout), and capture the
// completion text, its emission count, every incremental_chunk emission, and
// the error count.
// ============================================================================

struct request_result {
    QString completion_text;
    int completion_count = 0;
    QStringList chunks;
    int error_count = 0;
};

// Connect result capture to the client. Call before send_request().
auto attach_result(model_client_base &client, request_result &result) -> void {
    QObject::connect(&client, &model_client_base::completion,
                     [&result](const QString &text, const token_stats &) {
        result.completion_text = text;
        ++result.completion_count;
    });
    QObject::connect(&client, &model_client_base::incremental_chunk,
                     [&result](const QString &chunk) {
        result.chunks.append(chunk);
    });
    QObject::connect(&client, &model_client_base::error_occurred,
                     [&result](const error_frame &) {
        ++result.error_count;
    });
}

// Pump the event loop until completion/error or a 5 s safety timeout.
auto pump_until_done(request_result &result) -> void {
    QElapsedTimer timer;
    timer.start();
    while (result.completion_count == 0 && result.error_count == 0 && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

// Point a pre-configured client at the mock, drive send_request(), and pump
// until completion/error. Resets the process-global schema_registry active
// schema to match the mock's response schema to avoid cross-slot
// contamination.
auto run_request(model_client_base &client, mock_model_server &server) -> request_result {
    schema_registry::set_active_schema(
        server.active_schema() == mock_model_server::schema::native ? schema_type::native : schema_type::openai);

    request_result result;
    attach_result(client, result);
    client.send_request();
    pump_until_done(result);
    return result;
}

// Configure a text client pointed at the mock with the given stream mode.
auto configure_text_client(text_to_text_client &client, mock_model_server &server, bool stream) -> void {
    client.set_endpoint_url(QString("http://127.0.0.1:%1").arg(server.port()));
    client.set_model_name("mock-model");
    client.set_system_prompt("be nice");
    client.set_user_prompt("hi");
    client.set_stream(stream);
    client.set_coalescing_interval_ms(50);
}

// Write a tiny PNG to a temp file and return its path. Used by the image
// payload slots via the file-path overload (the QImage overload has a
// pre-existing format-string bug that is out of scope for this task). A fixed
// name is fine: each run overwrites the previous file.
auto make_temp_png() -> QString {
    const QString path = QDir::tempPath() + "/dir2md_test_img.png";

    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        image.save(&out, "PNG");
        out.close();
    }
    return path;
}

// Parse the mock's captured request body; fails the test if it is not JSON.
// The QVERIFY runs in a nested void lambda because Qt's test macros expand to
// a bare `return`, which is only valid in void functions.
auto parse_request_body(const mock_model_server &server) -> QJsonObject {
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(server.last_request_body().toUtf8(), &err);
    auto check = [&]() -> void {
        QVERIFY2(err.error == QJsonParseError::NoError,
                 qPrintable(QString("request body is not valid JSON: %1")
                                .arg(server.last_request_body().left(200))));
    };
    check();
    return doc.object();
}

// ============================================================================
// Property tests
// ============================================================================

void model_client_test::test_endpoint_url_property() {
    text_to_text_client client;
    QCOMPARE(client.get_endpoint_url(), QString(""));

    client.set_endpoint_url("http://localhost:8080");
    QCOMPARE(client.get_endpoint_url(), QString("http://localhost:8080"));
}

void model_client_test::test_model_name_property() {
    text_to_text_client client;
    QCOMPARE(client.get_model_name(), QString(""));

    client.set_model_name("gpt-4o-mini");
    QCOMPARE(client.get_model_name(), QString("gpt-4o-mini"));
}

void model_client_test::test_temperature_property() {
    text_to_text_client client;
    QCOMPARE(client.get_temperature(), 0.7f);

    client.set_temperature(0.5f);
    QCOMPARE(client.get_temperature(), 0.5f);
}

void model_client_test::test_coalescing_interval_property() {
    text_to_text_client client;
    QCOMPARE(client.get_coalescing_interval_ms(), 250);

    client.set_coalescing_interval_ms(100);
    QCOMPARE(client.get_coalescing_interval_ms(), 100);
}

// ============================================================================
// Busy state tests
// ============================================================================

void model_client_test::test_initial_not_busy() {
    text_to_text_client client;
    QVERIFY(!client.is_busy());
}

void model_client_test::test_cancel_resets_busy() {
    text_to_text_client client;
    // Initially not busy
    QVERIFY(!client.is_busy());

    // Cancel should emit signal and reset busy flag
    QSignalSpy spy(&client, &text_to_text_client::cancelled);
    client.cancel();

    QCOMPARE(spy.count(), 1);
    QVERIFY(!client.is_busy());
}

// ============================================================================
// Cancel tests
// ============================================================================

void model_client_test::test_cancel_signal_emitted() {
    text_to_text_client client;
    QSignalSpy spy(&client, &text_to_text_client::cancelled);

    client.cancel();
    QCOMPARE(spy.count(), 1);
}

// ============================================================================
// Token stats tests
// ============================================================================

void model_client_test::test_token_stats_default_values() {
    token_stats stats;
    QCOMPARE(stats.total_tokens, 0);
    QCOMPARE(stats.tokens_per_sec, 0.0);
    QCOMPARE(stats.generation_time_ms, qint64(0));
    QCOMPARE(stats.prompt_tokens, 0);
    QCOMPARE(stats.prompt_eval_time_ms, qint64(0));
}

// ============================================================================
// Stream property tests (R1)
// ============================================================================

void model_client_test::test_stream_property_default_false() {
    text_to_text_client client;
    QCOMPARE(client.get_stream(), false);

    image_to_text_client image_client;
    QCOMPARE(image_client.get_stream(), false);
}

void model_client_test::test_stream_property_set_emits_once() {
    text_to_text_client client;
    QSignalSpy spy(&client, &model_client_base::stream_changed);

    client.set_stream(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    QCOMPARE(client.get_stream(), true);
}

void model_client_test::test_stream_property_same_value_no_emit() {
    text_to_text_client client;
    QSignalSpy spy(&client, &model_client_base::stream_changed);

    client.set_stream(true);
    QCOMPARE(spy.count(), 1);

    // Setting the same value again must not emit.
    client.set_stream(true);
    QCOMPARE(spy.count(), 1);

    // Changing back emits once more with the new value.
    client.set_stream(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

// ============================================================================
// Request payload stream flag tests (R2)
// ============================================================================

void model_client_test::test_payload_carries_stream_true_openai() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    const QJsonObject root = parse_request_body(server);
    QCOMPARE(root["stream"].toBool(false), true);
}

void model_client_test::test_payload_carries_stream_false_openai() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, false);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    const QJsonObject root = parse_request_body(server);
    QCOMPARE(root["stream"].toBool(true), false);
}

void model_client_test::test_payload_carries_stream_true_native() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::native);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    const QJsonObject root = parse_request_body(server);
    QCOMPARE(root["stream"].toBool(false), true);
}

void model_client_test::test_payload_carries_stream_false_native() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::native);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, false);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    const QJsonObject root = parse_request_body(server);
    QCOMPARE(root["stream"].toBool(true), false);
}

void model_client_test::test_image_payload_carries_stream_true() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    image_to_text_client client;
    client.set_endpoint_url(QString("http://127.0.0.1:%1").arg(server.port()));
    client.set_model_name("mock-model");
    client.set_stream(true);

    const QString png_path = make_temp_png();
    request_result result;
    attach_result(client, result);
    client.send_request(png_path, "describe");
    pump_until_done(result);

    QCOMPARE(result.error_count, 0);
    const QJsonObject root = parse_request_body(server);
    QCOMPARE(root["stream"].toBool(false), true);
}

void model_client_test::test_image_payload_carries_stream_false() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    image_to_text_client client;
    client.set_endpoint_url(QString("http://127.0.0.1:%1").arg(server.port()));
    client.set_model_name("mock-model");
    // Default (unset) stream mode must be carried as false.

    const QString png_path = make_temp_png();
    request_result result;
    attach_result(client, result);
    client.send_request(png_path, "describe");
    pump_until_done(result);

    QCOMPARE(result.error_count, 0);
    const QJsonObject root = parse_request_body(server);
    QCOMPARE(root["stream"].toBool(true), false);
}

// ============================================================================
// Response format matrix tests (R3)
// ============================================================================

void model_client_test::test_sse_openai_streaming() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::plain));
}

void model_client_test::test_ndjson_openai_streaming() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::ndjson);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::plain));
}

void model_client_test::test_nonstreaming_openai_body() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, false);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::plain));
}

void model_client_test::test_sse_native_streaming() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::native);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::plain));
}

void model_client_test::test_nonstreaming_native_body() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::native);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, false);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::plain));
}

void model_client_test::test_dedicated_reasoning_discarded() {
    // Dedicated reasoning channel (reasoning_content) must be discarded: the
    // completion text contains only the final answer.
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::openai);
    server.set_reasoning(mock_model_server::reasoning::dedicated);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::dedicated));
    QVERIFY(!result.completion_text.contains("Step 1: think..."));
}

void model_client_test::test_inline_tags_preserved_verbatim() {
    // Inline think tags inside content must pass through verbatim, in order.
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::openai);
    server.set_reasoning(mock_model_server::reasoning::inline_tags);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::inline_tags));
    QVERIFY(result.completion_text.contains("<think>"));
    QVERIFY(result.completion_text.contains("</think>"));
}

// ============================================================================
// Robustness tests (R4)
// ============================================================================

void model_client_test::test_split_line_reassembled_exactly_once() {
    // Split the response mid-line across two TCP sends; the reassembled line
    // must be extracted exactly once (no loss, no duplication).
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::openai);
    server.set_split_at(40); // inside the first data: line
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::plain));
}

void model_client_test::test_malformed_line_warns_and_completes() {
    // A malformed data line mid-stream must not abort: completion still fires
    // with the text from all valid lines.
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::openai);
    server.set_inject_malformed_line(true);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
    QCOMPARE(result.completion_text,
             mock_model_server::expected_completion_text(mock_model_server::reasoning::plain));
}

void model_client_test::test_done_sentinel_completion_fires_once() {
    // The [DONE] sentinel (OpenAI) must result in completion firing exactly once.
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
}

void model_client_test::test_stop_true_completion_fires_once() {
    // The stop: true object (native) must result in completion firing exactly once.
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::native);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    QCOMPARE(result.completion_count, 1);
}

// ============================================================================
// Coalescing gating tests (R5)
// ============================================================================

void model_client_test::test_incremental_chunks_emitted_when_streaming() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::sse);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, true);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    // At least one incremental_chunk emission when streaming.
    QVERIFY(!result.chunks.isEmpty());
    // The emissions reassemble to exactly the completion text.
    QString joined;
    for (const QString &chunk : result.chunks) {
        joined += chunk;
    }
    QCOMPARE(joined, result.completion_text);
}

void model_client_test::test_no_incremental_chunks_when_nonstreaming() {
    mock_model_server server;
    server.set_transport(mock_model_server::transport::single_body);
    server.set_schema(mock_model_server::schema::openai);
    QVERIFY(server.start());

    text_to_text_client client;
    configure_text_client(client, server, false);
    const request_result result = run_request(client, server);

    QCOMPARE(result.error_count, 0);
    // No incremental_chunk at all when non-streaming.
    QVERIFY(result.chunks.isEmpty());
    QCOMPARE(result.completion_count, 1);
}

// AUTOMOC handles moc generation
QTEST_MAIN(model_client_test)
#include "model_client_test.moc"
