#pragma once

#include <QObject>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QImage>
#include <memory>
#include <vector>
#include <functional>
#include <iostream>

#include "backend/core/expected.hpp"

namespace dir2md::backend {

// ============================================================================
// Schema types
// ============================================================================

enum class schema_type {
    openai,
    native
};

// ============================================================================
// Token statistics
// ============================================================================

struct token_stats {
    int total_tokens = 0;
    double tokens_per_sec = 0.0;
    qint64 generation_time_ms = 0;
    int prompt_tokens = 0;
    qint64 prompt_eval_time_ms = 0;
};

// ============================================================================
// Chat message (role + content)
// ============================================================================

enum class message_role {
    system,
    user,
    assistant
};

struct chat_message {
    message_role role = message_role::user;
    QString content;
};

// ============================================================================
// Schema parser abstract interface
// ============================================================================

class api_schema_parser {
public:
    virtual ~api_schema_parser() = default;

    // Parse a single line of streamed data and extract text fragment
    virtual auto parse_line(const QString &line) -> QString = 0;

    // Extract token usage metadata from a final response object if present
    virtual auto parse_usage(const QString &line) -> token_stats { return token_stats{}; }

    // Extract the answer text from a complete non-streaming response body.
    // Returns an empty string when the body is not valid JSON, is not an
    // object, or lacks the schema's answer path.
    virtual auto parse_full_response(const QString &body) -> QString = 0;

    // Construct the request payload as QJsonDocument. The stream mode is
    // carried into the payload so the server responds in the matching format.
    virtual auto construct_request(const std::vector<chat_message> &messages, float temperature, bool stream) -> QJsonDocument = 0;
};

// ============================================================================
// OpenAI schema parser
// ============================================================================

class openai_schema_parser : public api_schema_parser {
public:
    auto parse_line(const QString &line) -> QString override;
    auto parse_usage(const QString &line) -> token_stats override;
    auto parse_full_response(const QString &body) -> QString override;
    auto construct_request(const std::vector<chat_message> &messages, float temperature, bool stream) -> QJsonDocument override;
};

// ============================================================================
// Native (llama.cpp) schema parser
// ============================================================================

class native_schema_parser : public api_schema_parser {
public:
    auto parse_line(const QString &line) -> QString override;
    auto parse_usage(const QString &line) -> token_stats override;
    auto parse_full_response(const QString &body) -> QString override;
    auto construct_request(const std::vector<chat_message> &messages, float temperature, bool stream) -> QJsonDocument override;
};

// ============================================================================
// Schema registry (static factory)
// ============================================================================

class schema_registry {
public:
    static auto set_active_schema(schema_type type) -> void;
    static auto get_active_schema() -> schema_type;
    static auto create_parser() -> std::unique_ptr<api_schema_parser>;

private:
    static schema_type s_active_schema;
};

// ============================================================================
// Model client base class (abstract QObject)
// ============================================================================

class model_client_base : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString endpoint_url READ get_endpoint_url WRITE set_endpoint_url NOTIFY endpoint_url_changed)
    Q_PROPERTY(QString model_name READ get_model_name WRITE set_model_name NOTIFY model_name_changed)
    Q_PROPERTY(float temperature READ get_temperature WRITE set_temperature NOTIFY temperature_changed)
    Q_PROPERTY(int coalescing_interval_ms READ get_coalescing_interval_ms WRITE set_coalescing_interval_ms NOTIFY coalescing_interval_ms_changed)
    Q_PROPERTY(bool stream READ get_stream WRITE set_stream NOTIFY stream_changed)

public:
    explicit model_client_base(QObject *parent = nullptr);
    ~model_client_base() override = default;

    // Property accessors
    auto get_endpoint_url() const -> QString;
    auto set_endpoint_url(const QString &url) -> void;

    auto get_model_name() const -> QString;
    auto set_model_name(const QString &name) -> void;

    auto get_temperature() const -> float;
    auto set_temperature(float temp) -> void;

    auto get_coalescing_interval_ms() const -> int;
    auto set_coalescing_interval_ms(int ms) -> void;

    auto get_stream() const -> bool;
    auto set_stream(const bool &stream) -> void;

    // State query
    auto is_busy() const -> bool;

    // Test-only diagnostics: capture the raw response lines exactly as they
    // arrive from the server (before schema parsing). Mirrors the
    // SettingsManager::setTestBaseDirectory pattern — a process-global hook
    // that is a no-op unless explicitly set. Used by integration tests to see
    // what the endpoint actually sends (e.g. SSE "data:" prefixes vs NDJSON).
    using raw_line_probe_fn = std::function<void(const QString &raw_line)>;
    static auto set_raw_line_probe(raw_line_probe_fn probe) -> void;
    static auto clear_raw_line_probe() -> void;

    // Actions
    virtual auto send_request() -> void = 0;
    auto cancel() -> void;

signals:
    void incremental_chunk(const QString &text);
    void completion(const QString &full_text, const token_stats &stats);
    void cancelled();
    void error_occurred(const error_frame &err);
    void endpoint_url_changed(const QString &url);
    void model_name_changed(const QString &name);
    void temperature_changed(float temp);
    void coalescing_interval_ms_changed(int ms);
    void stream_changed(bool stream);

protected:
    QNetworkAccessManager *m_network_manager;
    QNetworkReply *m_network_reply;
    QTimer *m_coalescing_timer;

    QString m_endpoint_url;
    QString m_model_name;
    float m_temperature = 0.7f;
    int m_coalescing_interval_ms = 250;
    bool m_stream = false;

    QString m_accumulated_text;
    QString m_chunk_buffer;
    // Carry-over buffer holding the trailing partial line from the previous
    // read, so a JSON line split across two TCP reads is reassembled exactly
    // once before parsing.
    QString m_line_carryover;
    // Full response body accumulator for the non-streaming path (stream=false).
    QString m_full_body;
    token_stats m_current_stats;
    bool m_busy = false;
    bool m_cancelled = false;
    // Set once a termination signal ([DONE] or stop: true) is observed; no
    // further lines are expected after this.
    bool m_stream_finished = false;

    // Internal helpers
    auto start_request(const QJsonDocument &payload) -> void;
    virtual auto format_payload() -> QJsonDocument = 0;

private slots:
    void on_ready_read();
    void on_finished();
    void on_error_occurred(QNetworkReply::NetworkError code);
    void on_coalescing_timeout();

private:
    auto process_line(const QString &line) -> void;

    // Test-only raw-line probe (process-global, no-op unless set).
    static raw_line_probe_fn s_raw_line_probe;
};

// ============================================================================
// Image-to-text client
// ============================================================================

class image_to_text_client : public model_client_base {
    Q_OBJECT

public:
    explicit image_to_text_client(QObject *parent = nullptr);

    // Send the previously configured image (file path or QImage) and prompt.
    auto send_request() -> void override;

    auto send_request(const QString &file_path, const QString &prompt = "") -> void;
    auto send_request(const QImage &image, const QString &prompt = "") -> void;

protected:
    auto format_payload() -> QJsonDocument override;

private:
    QString m_image_data_uri;
    QString m_text_prompt;

    auto image_to_data_uri(const QByteArray &raw_data, const QString &mime_type) -> QString;
    auto detect_mime_from_extension(const QString &file_path) -> QString;
    auto detect_mime_from_qimage_format(const QImage &image) -> QString;
};

// ============================================================================
// Text-to-text client
// ============================================================================

class text_to_text_client : public model_client_base {
    Q_OBJECT

    Q_PROPERTY(QString system_prompt READ get_system_prompt WRITE set_system_prompt NOTIFY system_prompt_changed)
    Q_PROPERTY(QString user_prompt READ get_user_prompt WRITE set_user_prompt NOTIFY user_prompt_changed)
    Q_PROPERTY(QString assistant_prompt READ get_assistant_prompt WRITE set_assistant_prompt NOTIFY assistant_prompt_changed)

public:
    explicit text_to_text_client(QObject *parent = nullptr);

    // Property accessors
    auto get_system_prompt() const -> QString;
    auto set_system_prompt(const QString &prompt) -> void;

    auto get_user_prompt() const -> QString;
    auto set_user_prompt(const QString &prompt) -> void;

    auto get_assistant_prompt() const -> QString;
    auto set_assistant_prompt(const QString &prompt) -> void;

    // Override send_request
    auto send_request() -> void override;

    // Sanitization utility
    static auto sanitize_prompt(const QString &input) -> QString;

signals:
    void system_prompt_changed(const QString &prompt);
    void user_prompt_changed(const QString &prompt);
    void assistant_prompt_changed(const QString &prompt);

protected:
    auto format_payload() -> QJsonDocument override;

private:
    QString m_system_prompt;
    QString m_user_prompt;
    QString m_assistant_prompt;

public:
    static auto strip_zero_width_spaces(const QString &input) -> QString;

private:
};

} // namespace dir2md::backend
