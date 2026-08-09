#include "model.hpp"

#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QByteArray>
#include <QBuffer>
#include <QRegularExpression>
#include <QDebug>
#include <stdexcept>

#include "backend/core/assert.hpp"

namespace dir2md::backend {

// ============================================================================
// Schema registry (static initialization)
// ============================================================================

schema_type schema_registry::s_active_schema = schema_type::openai;

auto schema_registry::set_active_schema(schema_type type) -> void {
    s_active_schema = type;
}

auto schema_registry::get_active_schema() -> schema_type {
    return s_active_schema;
}

auto schema_registry::create_parser() -> std::unique_ptr<api_schema_parser> {
    switch (s_active_schema) {
        case schema_type::native:
            return std::make_unique<native_schema_parser>();
        case schema_type::openai:
        default:
            return std::make_unique<openai_schema_parser>();
    }
}

// ============================================================================
// OpenAI schema parser implementation
// ============================================================================

auto openai_schema_parser::parse_line(const QString &line) -> QString {
    if (line.isEmpty()) {
        return "";
    }

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return "";
    }

    if (!doc.isObject()) {
        return "";
    }

    QJsonObject root = doc.object();

    // Try streaming format: choices[0].delta.content
    if (root.contains("choices")) {
        QJsonArray choices = root["choices"].toArray();
        if (!choices.isEmpty() && choices[0].isObject()) {
            QJsonObject first_choice = choices[0].toObject();
            if (first_choice.contains("delta")) {
                QJsonObject delta = first_choice["delta"].toObject();
                if (delta.contains("content")) {
                    return delta["content"].toString("");
                }
            }
            // Non-streaming fallback: choices[0].text
            if (first_choice.contains("text")) {
                return first_choice["text"].toString("");
            }
        }
    }

    return "";
}

auto openai_schema_parser::parse_usage(const QString &line) -> token_stats {
    token_stats stats {};
    if (line.isEmpty()) {
        return stats;
    }

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return stats;
    }

    if (!doc.isObject()) {
        return stats;
    }

    QJsonObject root = doc.object();
    if (!root.contains("usage")) {
        return stats;
    }

    QJsonObject usage = root["usage"].toObject();
    if (usage.contains("total_tokens")) {
        stats.total_tokens = usage["total_tokens"].toInt(0);
    }
    if (usage.contains("prompt_tokens")) {
        stats.prompt_tokens = usage["prompt_tokens"].toInt(0);
    }

    return stats;
}

auto openai_schema_parser::construct_request(const QStringList &messages, float temperature) -> QJsonDocument {
    QJsonObject root;
    QJsonArray messages_array;

    for (const QString &msg : messages) {
        QJsonObject msg_obj;
        msg_obj["content"] = msg;
        // Default to "user" role; the caller should structure messages appropriately
        msg_obj["role"] = "user";
        messages_array.append(msg_obj);
    }

    root["messages"] = messages_array;
    root["temperature"] = temperature;
    root["stream"] = true;

    return QJsonDocument(root);
}

// ============================================================================
// Native (llama.cpp) schema parser implementation
// ============================================================================

auto native_schema_parser::parse_line(const QString &line) -> QString {
    if (line.isEmpty()) {
        return "";
    }

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return "";
    }

    if (!doc.isObject()) {
        return "";
    }

    QJsonObject root = doc.object();
    if (root.contains("content")) {
        return root["content"].toString("");
    }

    return "";
}

auto native_schema_parser::parse_usage(const QString &line) -> token_stats {
    token_stats stats {};
    if (line.isEmpty()) {
        return stats;
    }

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return stats;
    }

    if (!doc.isObject()) {
        return stats;
    }

    QJsonObject root = doc.object();

    // llama.cpp native timing fields
    if (root.contains("tokens_predicted")) {
        stats.total_tokens = root["tokens_predicted"].toInt(0);
    }
    if (root.contains("prompt_tokens")) {
        stats.prompt_tokens = root["prompt_tokens"].toInt(0);
    }
    if (root.contains("prompt_eval_time_ms")) {
        stats.prompt_eval_time_ms = static_cast<qint64>(root["prompt_eval_time_ms"].toDouble(0));
    }
    if (root.contains("eval_time_ms")) {
        stats.generation_time_ms = static_cast<qint64>(root["eval_time_ms"].toDouble(0));
    }

    // Calculate tokens_per_sec if we have enough data
    if (stats.total_tokens > 0 && stats.generation_time_ms > 0) {
        stats.tokens_per_sec = static_cast<double>(stats.total_tokens) / (static_cast<double>(stats.generation_time_ms) / 1000.0);
    }

    return stats;
}

auto native_schema_parser::construct_request(const QStringList &messages, float temperature) -> QJsonDocument {
    QJsonObject root;

    // Native format uses a single prompt string with <|...|> delimiters
    QString prompt;
    for (int i = 0; i < messages.size(); ++i) {
        if (i > 0) {
            prompt += "\n";
        }
        prompt += messages[i];
    }

    root["prompt"] = prompt;
    root["temperature"] = temperature;
    root["stream"] = true;

    return QJsonDocument(root);
}

// ============================================================================
// Model client base class implementation
// ============================================================================

model_client_base::model_client_base(QObject *parent)
    : QObject(parent),
      m_network_manager(new QNetworkAccessManager(this)),
      m_network_reply(nullptr),
      m_coalescing_timer(new QTimer(this)) {
    m_coalescing_timer->setSingleShot(true);
    connect(m_coalescing_timer, &QTimer::timeout, this, &model_client_base::on_coalescing_timeout);
}

auto model_client_base::get_endpoint_url() const -> QString {
    return m_endpoint_url;
}

auto model_client_base::set_endpoint_url(const QString &url) -> void {
    if (m_endpoint_url != url) {
        m_endpoint_url = url;
        emit endpoint_url_changed(url);
    }
}

auto model_client_base::get_temperature() const -> float {
    return m_temperature;
}

auto model_client_base::set_temperature(float temp) -> void {
    if (m_temperature != temp) {
        m_temperature = temp;
        emit temperature_changed(temp);
    }
}

auto model_client_base::get_coalescing_interval_ms() const -> int {
    return m_coalescing_interval_ms;
}

auto model_client_base::set_coalescing_interval_ms(int ms) -> void {
    if (m_coalescing_interval_ms != ms) {
        m_coalescing_interval_ms = ms;
        emit coalescing_interval_ms_changed(ms);
    }
}

auto model_client_base::is_busy() const -> bool {
    return m_busy;
}

auto model_client_base::cancel() -> void {
    m_cancelled = true;
    if (m_network_reply) {
        m_network_reply->abort();
    }
    m_busy = false;
    emit cancelled();
}

auto model_client_base::start_request(const QJsonDocument &payload) -> void {
    DEBUG_ASSERT(!m_busy);

    m_busy = true;
    m_cancelled = false;
    m_accumulated_text.clear();
    m_chunk_buffer.clear();
    m_current_stats = token_stats{};

    QNetworkRequest request((QUrl(m_endpoint_url)));
    request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader, "application/json");

    if (m_network_reply) {
        m_network_reply->deleteLater();
    }

    m_network_reply = m_network_manager->post(request, payload.toJson());

    connect(m_network_reply, &QNetworkReply::readyRead, this, &model_client_base::on_ready_read);
    connect(m_network_reply, &QNetworkReply::finished, this, &model_client_base::on_finished);
    connect(m_network_reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &model_client_base::on_error_occurred);
}

auto model_client_base::on_ready_read() -> void {
    if (!m_network_reply || m_cancelled) {
        return;
    }

    QByteArray data = m_network_reply->readAll();
    QString text(data);

    // Process line by line
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        process_line(line.trimmed());
    }
}

auto model_client_base::process_line(const QString &line) -> void {
    if (line.isEmpty()) {
        return;
    }

    auto parser = schema_registry::create_parser();

    // Try to extract text content
    QString token = parser->parse_line(line);
    if (!token.isEmpty()) {
        m_accumulated_text += token;
        m_chunk_buffer += token;

        // Restart coalescing timer
        m_coalescing_timer->start(m_coalescing_interval_ms);
    }

    // Try to extract usage stats
    auto usage_stats = parser->parse_usage(line);
    if (usage_stats.total_tokens > 0) {
        m_current_stats = usage_stats;
    }
}

auto model_client_base::on_finished() -> void {
    if (m_cancelled) {
        return;
    }

    // Emit any remaining chunk buffer
    if (!m_chunk_buffer.isEmpty()) {
        emit incremental_chunk(m_chunk_buffer);
        m_chunk_buffer.clear();
    }

    m_busy = false;

    if (m_network_reply) {
        m_network_reply->deleteLater();
        m_network_reply = nullptr;
    }

    emit completion(m_accumulated_text, m_current_stats);
}

auto model_client_base::on_error_occurred(QNetworkReply::NetworkError code) -> void {
    QString error_desc;
    if (m_network_reply) {
        error_desc = m_network_reply->errorString();
    }

    error_frame err(static_cast<int>(code), error_desc);
    emit error_occurred(err);

    m_busy = false;
}

auto model_client_base::on_coalescing_timeout() -> void {
    if (!m_chunk_buffer.isEmpty()) {
        emit incremental_chunk(m_chunk_buffer);
        m_chunk_buffer.clear();
    }
}

// ============================================================================
// Image-to-text client implementation
// ============================================================================

image_to_text_client::image_to_text_client(QObject *parent)
    : model_client_base(parent) {}

auto image_to_text_client::send_request(const QString &file_path, const QString &prompt) -> void {
    DEBUG_ASSERT(!m_busy);

    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
        error_frame err(-1, QString("Cannot open file: %1").arg(file_path));
        emit error_occurred(err);
        return;
    }

    QByteArray raw_data = file.readAll();
    file.close();

    QString mime_type = detect_mime_from_extension(file_path);
    m_image_data_uri = image_to_data_uri(raw_data, mime_type);
    m_text_prompt = prompt;

    auto payload = format_payload();
    start_request(payload);
}

auto image_to_text_client::send_request(const QImage &image, const QString &prompt) -> void {
    DEBUG_ASSERT(!m_busy);

    QByteArray raw_data;
    QBuffer buffer(&raw_data);

    QString mime_type = detect_mime_from_qimage_format(image);
    if (!image.save(&buffer, mime_type.toUtf8().constData())) {
        error_frame err(-1, "Cannot encode QImage to bytes");
        emit error_occurred(err);
        return;
    }

    m_image_data_uri = image_to_data_uri(raw_data, mime_type);
    m_text_prompt = prompt;

    auto payload = format_payload();
    start_request(payload);
}

auto image_to_text_client::format_payload() -> QJsonDocument {
    // Construct vision-specific request with image data URI
    QJsonObject root;

    QJsonArray messages_array;
    QJsonObject msg_obj;

    QJsonArray content_array;

    // Text prompt if provided
    if (!m_text_prompt.isEmpty()) {
        QJsonObject text_part;
        text_part["type"] = "text";
        text_part["text"] = m_text_prompt;
        content_array.append(text_part);
    }

    // Image part
    QJsonObject image_part;
    image_part["type"] = "image_url";
    QJsonObject image_url_obj;
    image_url_obj["url"] = m_image_data_uri;
    image_part["image_url"] = image_url_obj;
    content_array.append(image_part);

    msg_obj["role"] = "user";
    msg_obj["content"] = content_array;
    messages_array.append(msg_obj);

    root["messages"] = messages_array;
    root["temperature"] = m_temperature;
    root["stream"] = true;

    return QJsonDocument(root);
}

auto image_to_text_client::image_to_data_uri(const QByteArray &raw_data, const QString &mime_type) -> QString {
    QByteArray encoded = raw_data.toBase64();
    return QString("data:%1;base64,%2").arg(mime_type, QString(encoded));
}

auto image_to_text_client::detect_mime_from_extension(const QString &file_path) -> QString {
    QFileInfo info(file_path);
    QString suffix = info.suffix().toLower();

    if (suffix == "png") {
        return "image/png";
    } else if (suffix == "jpg" || suffix == "jpeg") {
        return "image/jpeg";
    } else if (suffix == "gif") {
        return "image/gif";
    } else if (suffix == "webp") {
        return "image/webp";
    }
    // Default fallback
    return "image/png";
}

auto image_to_text_client::detect_mime_from_qimage_format(const QImage &image) -> QString {
    switch (image.format()) {
        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied:
        case QImage::Format_RGB32:
        case QImage::Format_RGB888:
            return "image/png";
        default:
            return "image/png";
    }
}

// ============================================================================
// Text-to-text client implementation
// ============================================================================

text_to_text_client::text_to_text_client(QObject *parent)
    : model_client_base(parent) {}

auto text_to_text_client::get_system_prompt() const -> QString {
    return m_system_prompt;
}

auto text_to_text_client::set_system_prompt(const QString &prompt) -> void {
    if (m_system_prompt != prompt) {
        m_system_prompt = prompt;
        emit system_prompt_changed(prompt);
    }
}

auto text_to_text_client::get_user_prompt() const -> QString {
    return m_user_prompt;
}

auto text_to_text_client::set_user_prompt(const QString &prompt) -> void {
    if (m_user_prompt != prompt) {
        m_user_prompt = prompt;
        emit user_prompt_changed(prompt);
    }
}

auto text_to_text_client::get_assistant_prompt() const -> QString {
    return m_assistant_prompt;
}

auto text_to_text_client::set_assistant_prompt(const QString &prompt) -> void {
    if (m_assistant_prompt != prompt) {
        m_assistant_prompt = prompt;
        emit assistant_prompt_changed(prompt);
    }
}

auto text_to_text_client::send_request() -> void {
    DEBUG_ASSERT(!m_busy);

    auto payload = format_payload();
    start_request(payload);
}

auto text_to_text_client::sanitize_prompt(const QString &input) -> QString {
    // Match <|...|> patterns and insert zero-width space to neutralize
    QRegularExpression special_token_pattern(R"(<\|.*?\|>)");
    QString result = input;

    // Replace each match by inserting zero-width spaces
    int offset = 0;
    auto matches = special_token_pattern.globalMatch(input);
    while (matches.hasNext()) {
        auto match = matches.next();
        int start = match.capturedStart() + offset;
        int length = match.capturedLength();
        QString original = match.captured();
        // Insert zero-width space after <| and before |>
        QString sanitized = original.replace("<|", "<\u200B|").replace("|>", "|\u200B>");
        result.remove(start, length);
        result.insert(start, sanitized);
        offset += sanitized.length() - length;
    }

    return result;
}

auto text_to_text_client::strip_zero_width_spaces(const QString &input) -> QString {
    QString result = input;
    result.remove(QChar(0x200B));
    return result;
}

auto text_to_text_client::format_payload() -> QJsonDocument {
    auto parser = schema_registry::create_parser();

    // Sanitize user prompt to prevent prompt injection
    QString sanitized_user = sanitize_prompt(m_user_prompt);

    if (schema_registry::get_active_schema() == schema_type::native) {
        // Native format: concatenate with <|...|> delimiters
        QStringList messages;
        if (!m_system_prompt.isEmpty()) {
            messages.append(m_system_prompt);
        }
        messages.append(sanitized_user);
        if (!m_assistant_prompt.isEmpty()) {
            messages.append(m_assistant_prompt);
        }

        return parser->construct_request(messages, m_temperature);
    } else {
        // OpenAI format: message array with role/content
        QStringList messages;
        if (!m_system_prompt.isEmpty()) {
            messages.append(m_system_prompt);
        }
        messages.append(sanitized_user);
        // assistant_prompt is discarded for OpenAI format

        return parser->construct_request(messages, m_temperature);
    }
}

} // namespace dir2md::backend
