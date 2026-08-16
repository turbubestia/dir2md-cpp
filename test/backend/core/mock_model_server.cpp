#include "mock_model_server.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTcpSocket>

namespace dir2md::backend {

namespace {

// The answer is streamed in three fragments so tests can observe multiple
// incremental chunks; dedicated reasoning (when enabled) precedes it.
const QStringList answer_chunks = { "Hello", " world", "!" };
const QString reasoning_text = "Step 1: think...";
const QString inline_think_prefix = "<think>\n" + reasoning_text + "\n</think>";

auto join_answer() -> QString {
    QString result;
    for (const QString &chunk : answer_chunks) {
        result += chunk;
    }
    return result;
}

// Full answer content for a reasoning mode (inline tags preserved verbatim).
auto full_content(mock_model_server::reasoning r) -> QString {
    if (r == mock_model_server::reasoning::inline_tags) {
        return inline_think_prefix + join_answer();
    }
    return join_answer();
}

// Content chunks for streaming: inline tags ride in the first content chunk.
auto content_chunks(mock_model_server::reasoning r) -> QStringList {
    if (r == mock_model_server::reasoning::inline_tags) {
        return { inline_think_prefix + answer_chunks.first(), " world", "!" };
    }
    return answer_chunks;
}

// One OpenAI streaming delta line as compact JSON + newline.
auto openai_delta_line(const QString &content, const QString &reasoning) -> QByteArray {
    QJsonObject delta;
    if (!reasoning.isEmpty()) {
        delta["reasoning_content"] = reasoning;
    }
    if (!content.isEmpty()) {
        delta["content"] = content;
    }
    QJsonObject choice;
    choice["delta"] = delta;
    QJsonArray choices;
    choices.append(choice);
    QJsonObject line;
    line["choices"] = choices;
    return QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n";
}

// One native streaming chunk as compact JSON + newline.
auto native_chunk_line(const QString &content, const QString &reasoning, bool stop) -> QByteArray {
    QJsonObject line;
    if (!reasoning.isEmpty()) {
        line["reasoning_content"] = reasoning;
    }
    if (!content.isEmpty()) {
        line["content"] = content;
    }
    line["stop"] = stop;
    if (stop) {
        // Final chunk carries run metadata, mirroring llama-server.
        line["tokens_predicted"] = 3;
        line["prompt_tokens"] = 5;
        line["eval_time_ms"] = 100.0;
    }
    return QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n";
}

} // namespace

mock_model_server::mock_model_server(QObject *parent)
    : QObject(parent), m_server(this) {}

auto mock_model_server::start() -> bool {
    if (!m_server.listen(QHostAddress::LocalHost, 0)) {
        return false;
    }
    connect(&m_server, &QTcpServer::newConnection, this, &mock_model_server::on_new_connection);
    return true;
}

auto mock_model_server::port() const -> quint16 {
    return m_server.serverPort();
}

void mock_model_server::set_transport(transport t) {
    m_transport = t;
}

void mock_model_server::set_schema(schema s) {
    m_schema = s;
}

void mock_model_server::set_reasoning(reasoning r) {
    m_reasoning = r;
}

auto mock_model_server::active_schema() const -> schema {
    return m_schema;
}

void mock_model_server::set_split_at(int body_offset) {
    m_split_at = body_offset;
}

void mock_model_server::set_inject_malformed_line(bool enabled) {
    m_inject_malformed_line = enabled;
}

auto mock_model_server::last_request_body() const -> QString {
    return m_request_body;
}

auto mock_model_server::expected_completion_text(reasoning r) -> QString {
    return full_content(r);
}

void mock_model_server::on_new_connection() {
    QTcpSocket *socket = m_server.nextPendingConnection();
    if (!socket) {
        return;
    }
    // Fresh request capture per connection.
    m_request_data.clear();
    socket->setProperty("response_sent", false);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        on_socket_ready_read(socket);
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void mock_model_server::on_socket_ready_read(QTcpSocket *socket) {
    if (socket->property("response_sent").toBool()) {
        return;
    }
    m_request_data.append(socket->readAll());
    if (!request_complete()) {
        return;
    }
    capture_request_body();
    socket->setProperty("response_sent", true);
    send_response(socket);
}

auto mock_model_server::request_complete() -> bool {
    const int header_end = m_request_data.indexOf("\r\n\r\n");
    if (header_end == -1) {
        return false;
    }

    bool chunked = false;
    int content_length = 0;
    const QString headers = QString::fromUtf8(m_request_data.left(header_end));
    for (const QString &raw_line : headers.split('\n')) {
        const QString line = raw_line.trimmed();
        if (line.startsWith("Transfer-Encoding:", Qt::CaseInsensitive)
            && line.contains("chunked", Qt::CaseInsensitive)) {
            chunked = true;
        } else if (line.startsWith("Content-Length:", Qt::CaseInsensitive)) {
            content_length = line.mid(15).trimmed().toInt();
        }
    }

    if (chunked) {
        // The final chunk is a zero-length chunk: "0\r\n\r\n".
        return m_request_data.contains("\r\n0\r\n\r\n");
    }
    return static_cast<int>(m_request_data.size()) >= header_end + 4 + content_length;
}

void mock_model_server::capture_request_body() {
    const int header_end = m_request_data.indexOf("\r\n\r\n");
    if (header_end == -1) {
        return;
    }

    bool chunked = false;
    int content_length = 0;
    const QString headers = QString::fromUtf8(m_request_data.left(header_end));
    for (const QString &raw_line : headers.split('\n')) {
        const QString line = raw_line.trimmed();
        if (line.startsWith("Transfer-Encoding:", Qt::CaseInsensitive)
            && line.contains("chunked", Qt::CaseInsensitive)) {
            chunked = true;
        } else if (line.startsWith("Content-Length:", Qt::CaseInsensitive)) {
            content_length = line.mid(15).trimmed().toInt();
        }
    }

    QByteArray payload = m_request_data.mid(header_end + 4);
    if (chunked) {
        // Decode chunked framing: <hex-size>\r\n<data>\r\n ... 0\r\n\r\n
        QByteArray decoded;
        int pos = 0;
        while (true) {
            const int line_end = payload.indexOf("\r\n", pos);
            if (line_end == -1) {
                break;
            }
            const QString size_str = QString::fromUtf8(payload.mid(pos, line_end - pos)).trimmed();
            const uint chunk_size = size_str.section(';', 0, 0).toUInt(nullptr, 16);
            if (chunk_size == 0) {
                break;
            }
            const int data_start = line_end + 2;
            decoded.append(payload.mid(data_start, static_cast<int>(chunk_size)));
            pos = data_start + static_cast<int>(chunk_size) + 2;
        }
        payload = decoded;
    } else {
        payload = payload.left(content_length);
    }

    m_request_body = QString::fromUtf8(payload);
}

void mock_model_server::send_response(QTcpSocket *socket) {
    const QByteArray body = build_body();

    QByteArray content_type = "application/json";
    if (m_transport == transport::sse) {
        content_type = "text/event-stream";
    } else if (m_transport == transport::ndjson) {
        content_type = "application/x-ndjson";
    }

    QByteArray response;
    response += "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Connection: close\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "\r\n";
    response += body;

    if (m_split_at > 0 && m_split_at < static_cast<int>(response.size())) {
        // Split across two TCP sends so the client's carry-over buffer is
        // exercised: the first send ends mid-line.
        socket->write(response.left(m_split_at));
        socket->flush();
        QTimer::singleShot(25, this, [this, socket, response]() {
            socket->write(response.mid(m_split_at));
            socket->flush();
            socket->disconnectFromHost();
        });
    } else {
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }
}

auto mock_model_server::build_body() -> QByteArray {
    // Non-streaming: a single JSON object, no framing.
    if (m_transport == transport::single_body) {
        if (m_schema == schema::openai) {
            QJsonObject message;
            message["role"] = "assistant";
            message["content"] = full_content(m_reasoning);
            if (m_reasoning == reasoning::dedicated) {
                message["reasoning_content"] = reasoning_text;
            }
            QJsonObject choice;
            choice["message"] = message;
            choice["finish_reason"] = "stop";
            QJsonArray choices;
            choices.append(choice);
            QJsonObject usage;
            usage["total_tokens"] = 18;
            usage["prompt_tokens"] = 10;
            QJsonObject root;
            root["choices"] = choices;
            root["usage"] = usage;
            return QJsonDocument(root).toJson(QJsonDocument::Compact);
        }

        QJsonObject root;
        root["content"] = full_content(m_reasoning);
        if (m_reasoning == reasoning::dedicated) {
            root["reasoning_content"] = reasoning_text;
        }
        root["stop"] = true;
        root["tokens_predicted"] = 3;
        root["prompt_tokens"] = 5;
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    const bool framed = (m_transport == transport::sse);
    QByteArray body;

    auto emit_line = [&](const QByteArray &json_line) {
        if (framed) {
            body += "data: " + json_line.trimmed() + "\n\n";
        } else {
            body += json_line; // already newline-terminated
        }
    };

    auto emit_malformed = [&]() {
        if (framed) {
            body += "data: {this is not json\n\n";
        } else {
            body += "{this is not json\n";
        }
    };

    if (m_schema == schema::openai) {
        if (m_reasoning == reasoning::dedicated) {
            emit_line(openai_delta_line("", reasoning_text));
        }
        const QStringList chunks = content_chunks(m_reasoning);
        for (int i = 0; i < chunks.size(); ++i) {
            emit_line(openai_delta_line(chunks[i], ""));
            if (m_inject_malformed_line && i == 0) {
                emit_malformed();
            }
        }
        if (framed) {
            body += "data: [DONE]\n\n";
        }
    } else {
        if (m_reasoning == reasoning::dedicated) {
            emit_line(native_chunk_line("", reasoning_text, false));
        }
        const QStringList chunks = content_chunks(m_reasoning);
        for (int i = 0; i < chunks.size(); ++i) {
            const bool stop = (i == chunks.size() - 1);
            emit_line(native_chunk_line(chunks[i], "", stop));
            if (m_inject_malformed_line && i == 0) {
                emit_malformed();
            }
        }
    }

    return body;
}

} // namespace dir2md::backend
