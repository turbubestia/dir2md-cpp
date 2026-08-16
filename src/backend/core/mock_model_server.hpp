#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTcpServer>

class QTcpSocket;

namespace dir2md::backend {

// ============================================================================
// Reusable in-process mock model server
//
// A QTcpServer bound to a localhost ephemeral port (port 0) that answers each
// POST with a canned model response. Any test slot can configure it to emit
// any combination of the response matrix:
//
//   transport: sse | ndjson | single_body
//   schema:    openai | native
//   reasoning: plain | dedicated channel | inline tags
//
// Framing per mode:
//   - sse + openai:    "data: {...}" lines ending with "data: [DONE]"
//   - sse + native:    "data: {...}" lines, final object carries stop: true
//   - ndjson:          bare JSON lines (no framing, no sentinel)
//   - single_body:     one JSON object (OpenAI choices[0].message.content or
//                      native top-level content)
//
// It records the raw request body so tests can assert on the outgoing payload,
// and can optionally split the response across two TCP sends to exercise the
// client's line reassembly. Runs in the same event loop as the client — no
// threads, no external processes, no network access beyond localhost.
// ============================================================================

class mock_model_server : public QObject {
    Q_OBJECT

public:
    enum class transport { sse, ndjson, single_body };
    enum class schema { openai, native };
    enum class reasoning { plain, dedicated, inline_tags };

    explicit mock_model_server(QObject *parent = nullptr);

    // Bind to a localhost ephemeral port. Returns false on failure.
    auto start() -> bool;

    // The bound port (valid after start()).
    auto port() const -> quint16;

    // Per-test configuration.
    void set_transport(transport t);
    void set_schema(schema s);
    void set_reasoning(reasoning r);

    // Current schema (tests use this to sync the process-global
    // schema_registry active schema with the mock's response format).
    auto active_schema() const -> schema;

    // Split the response write across two TCP sends: the first send carries
    // everything up to (header size + body_offset) bytes, the rest follows a
    // short delay later. A body_offset landing inside a JSON line exercises
    // the client's carry-over buffer. 0 disables splitting (default).
    void set_split_at(int body_offset);

    // Inject one malformed data line after the first content line so tests can
    // verify mid-stream tolerance. Off by default.
    void set_inject_malformed_line(bool enabled);

    // The raw request body of the most recent connection (empty if none).
    auto last_request_body() const -> QString;

    // Expected completion text for a reasoning mode: dedicated reasoning is
    // discarded, inline tags are preserved verbatim in order.
    static auto expected_completion_text(reasoning r) -> QString;

private:
    void on_new_connection();
    void on_socket_ready_read(QTcpSocket *socket);
    void send_response(QTcpSocket *socket);

    auto build_body() -> QByteArray;
    auto request_complete() -> bool;
    void capture_request_body();

    QTcpServer m_server;
    transport m_transport = transport::sse;
    schema m_schema = schema::openai;
    reasoning m_reasoning = reasoning::plain;
    int m_split_at = 0;
    bool m_inject_malformed_line = false;

    QByteArray m_request_data;
    QString m_request_body;
};

} // namespace dir2md::backend
