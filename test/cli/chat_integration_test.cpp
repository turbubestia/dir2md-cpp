#include <backend/core/model.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace dir2md::backend;

// ============================================================================
// In-process mock model server
//
// A QTcpServer on an ephemeral localhost port that answers a single POST with
// a canned chat-completion body. Two response modes:
//   - ndjson: raw JSON lines (the format model_client_base currently expects,
//             mirroring build/smoke_mock_server.py).
//   - sse:    Server-Sent Events ("data: {...}\n\n" + "data: [DONE]"), the
//             format real OpenAI-compatible servers (vLLM/llama.cpp/LM Studio)
//             actually emit for stream:true requests.
//
// It records the raw request body so tests can assert on the outgoing payload.
// Runs in the same event loop as the client — no threads.
// ============================================================================

class mock_model_server : public QObject {
    Q_OBJECT

public:
    enum class response_mode { ndjson, sse };

    explicit mock_model_server(QObject *parent = nullptr)
        : QObject(parent), m_server(this) {}

    auto start() -> bool {
        if (!m_server.listen(QHostAddress::LocalHost, 0)) {
            return false;
        }
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket *socket = m_server.nextPendingConnection();
            if (!socket) {
                return;
            }
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                m_request_data.append(socket->readAll());
                // Respond once the request headers are complete. The test
                // request is small and arrives in a single read.
                if (!m_response_sent && m_request_data.contains("\r\n\r\n")) {
                    send_response(socket);
                }
            });
        });
        return true;
    }

    auto port() const -> quint16 {
        return m_server.serverPort();
    }

    auto last_request_body() const -> QString {
        return m_request_body;
    }

    void set_mode(response_mode mode) { m_mode = mode; }

private:
    void send_response(QTcpSocket *socket) {
        // Capture the request body (everything after the header terminator).
        const int header_end = m_request_data.indexOf("\r\n\r\n");
        if (header_end != -1) {
            m_request_body = QString::fromUtf8(m_request_data.mid(header_end + 4));
        }

        QByteArray body;
        if (m_mode == response_mode::ndjson) {
            // Mirrors build/smoke_mock_server.py: delta.content lines + usage.
            const QStringList chunks = { "Hello ", "pre<th", "ink>secret</thin", "k>world!" };
            for (const QString &chunk : chunks) {
                body += ndjson_delta_line(chunk);
            }
            QJsonObject usage;
            usage["total_tokens"] = 42;
            usage["prompt_tokens"] = 10;
            QJsonObject usage_line;
            usage_line["usage"] = usage;
            body += QJsonDocument(usage_line).toJson(QJsonDocument::Compact) + "\n";
        } else {
            // Real OpenAI-compatible streaming format: SSE "data:" lines.
            const QStringList chunks = { "Hello ", "world!" };
            for (const QString &chunk : chunks) {
                body += "data: " + ndjson_delta_line(chunk).trimmed() + "\n\n";
            }
            body += "data: [DONE]\n\n";
        }

        QByteArray response;
        response += "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: application/x-ndjson\r\n";
        response += "Connection: close\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "\r\n";
        response += body;

        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
        m_response_sent = true;
    }

    // Build one NDJSON line: {"choices":[{"delta":{"content":<chunk>}}]}
    static auto ndjson_delta_line(const QString &chunk) -> QByteArray {
        QJsonObject delta;
        delta["content"] = chunk;
        QJsonObject choice;
        choice["delta"] = delta;
        QJsonArray choices;
        choices.append(choice);
        QJsonObject line;
        line["choices"] = choices;
        return QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n";
    }

    QTcpServer m_server;
    response_mode m_mode = response_mode::ndjson;
    QByteArray m_request_data;
    QString m_request_body;
    bool m_response_sent = false;
};

// ============================================================================
// Integration test
//
// Drives text_to_text_client directly (not through execute_chat) so the drop,
// if any, is localized to the backend parser. The raw-line probe captures
// exactly what the server sent, letting us distinguish "lost on the wire" from
// "arrived but dropped by the parser".
// ============================================================================

class chat_integration_test : public QObject {
    Q_OBJECT

private slots:
    void test_ndjson_response_completes_with_text();
    void test_sse_response_completes_with_text();
    void test_probe_captures_raw_lines();
    void test_request_payload_is_openai_format();

private:
    // Run one chat request against the mock and return the completion text.
    // raw_lines receives every raw line the client received (via the probe).
    QString run_chat(mock_model_server &server, const QString &model_name,
                     const QString &system_prompt, const QString &user_prompt,
                     QStringList &raw_lines);
};

QString chat_integration_test::run_chat(mock_model_server &server, const QString &model_name,
                                        const QString &system_prompt, const QString &user_prompt,
                                        QStringList &raw_lines) {
    model_client_base::set_raw_line_probe([&raw_lines](const QString &line) {
        raw_lines.append(line);
    });

    text_to_text_client client;
    client.set_endpoint_url(QString("http://127.0.0.1:%1").arg(server.port()));
    client.set_model_name(model_name);
    client.set_system_prompt(system_prompt);
    client.set_user_prompt(user_prompt);
    // The mock always answers in streaming format; the client's stream
    // property defaults to false, so enable it explicitly.
    client.set_stream(true);

    QString full_text;
    bool finished = false;
    QObject::connect(&client, &model_client_base::completion,
                     [&full_text, &finished](const QString &text, const token_stats &) {
        full_text = text;
        finished = true;
    });
    QObject::connect(&client, &model_client_base::error_occurred,
                     [&finished](const error_frame &) {
        finished = true;
    });

    client.send_request();

    // Pump the event loop until completion/error or a 5 s safety timeout.
    QElapsedTimer timer;
    timer.start();
    while (!finished && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    model_client_base::clear_raw_line_probe();
    return full_text;
}

// ============================================================================
// Slots
// ============================================================================

void chat_integration_test::test_ndjson_response_completes_with_text() {
    mock_model_server server;
    server.set_mode(mock_model_server::response_mode::ndjson);
    QVERIFY(server.start());

    QStringList raw_lines;
    const QString full_text = run_chat(server, "mock-model", "be nice", "hi", raw_lines);

    // The NDJSON format the client expects must produce text (happy path).
    QVERIFY(full_text.contains("Hello"));
    QVERIFY(full_text.contains("world!"));
}

void chat_integration_test::test_sse_response_completes_with_text() {
    mock_model_server server;
    server.set_mode(mock_model_server::response_mode::sse);
    QVERIFY(server.start());

    QStringList raw_lines;
    const QString full_text = run_chat(server, "mock-model", "be nice", "hi", raw_lines);

    // The SSE "data:" prefix is stripped before parsing, so the streamed
    // text reaches completion (the former bug: everything was dropped).
    QVERIFY(full_text.contains("Hello"));
    QVERIFY(full_text.contains("world!"));

    // The probe proves the lines DID arrive with their "data:" framing.
    QVERIFY(!raw_lines.isEmpty());
    for (const QString &line : raw_lines) {
        QVERIFY(line.startsWith("data:"));
    }
    QVERIFY(raw_lines.last().contains("[DONE]"));
}

void chat_integration_test::test_probe_captures_raw_lines() {
    mock_model_server server;
    server.set_mode(mock_model_server::response_mode::ndjson);
    QVERIFY(server.start());

    QStringList raw_lines;
    run_chat(server, "mock-model", "be nice", "hi", raw_lines);

    // 4 content lines + 1 usage line.
    QCOMPARE(raw_lines.size(), 5);
    QVERIFY(raw_lines.first().contains("Hello"));
    QVERIFY(raw_lines.last().contains("usage"));
}

void chat_integration_test::test_request_payload_is_openai_format() {
    mock_model_server server;
    server.set_mode(mock_model_server::response_mode::ndjson);
    QVERIFY(server.start());

    QStringList raw_lines;
    run_chat(server, "mock-model", "be nice", "hi", raw_lines);

    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(server.last_request_body().toUtf8(), &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    const QJsonObject root = doc.object();
    QCOMPARE(root["model"].toString(), QString("mock-model"));
    QVERIFY(root["stream"].toBool());
    const QJsonArray messages = root["messages"].toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages[0].toObject()["role"].toString(), QString("system"));
    QCOMPARE(messages[1].toObject()["role"].toString(), QString("user"));
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    chat_integration_test test;
    return QTest::qExec(&test, argc, argv);
}

#include "chat_integration_test.moc"
