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
// Schema parser abstract interface
// ============================================================================

class api_schema_parser {
public:
    virtual ~api_schema_parser() = default;

    // Parse a single line of streamed data and extract text fragment
    virtual auto parse_line(const QString &line) -> QString = 0;

    // Extract token usage metadata from a final response object if present
    virtual auto parse_usage(const QString &line) -> token_stats { return token_stats{}; }

    // Construct the request payload as QJsonDocument
    virtual auto construct_request(const QStringList &messages, float temperature) -> QJsonDocument = 0;
};

// ============================================================================
// OpenAI schema parser
// ============================================================================

class openai_schema_parser : public api_schema_parser {
public:
    auto parse_line(const QString &line) -> QString override;
    auto parse_usage(const QString &line) -> token_stats override;
    auto construct_request(const QStringList &messages, float temperature) -> QJsonDocument override;
};

// ============================================================================
// Native (llama.cpp) schema parser
// ============================================================================

class native_schema_parser : public api_schema_parser {
public:
    auto parse_line(const QString &line) -> QString override;
    auto parse_usage(const QString &line) -> token_stats override;
    auto construct_request(const QStringList &messages, float temperature) -> QJsonDocument override;
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
    Q_PROPERTY(float temperature READ get_temperature WRITE set_temperature NOTIFY temperature_changed)
    Q_PROPERTY(int coalescing_interval_ms READ get_coalescing_interval_ms WRITE set_coalescing_interval_ms NOTIFY coalescing_interval_ms_changed)

public:
    explicit model_client_base(QObject *parent = nullptr);
    ~model_client_base() override = default;

    // Property accessors
    auto get_endpoint_url() const -> QString;
    auto set_endpoint_url(const QString &url) -> void;

    auto get_temperature() const -> float;
    auto set_temperature(float temp) -> void;

    auto get_coalescing_interval_ms() const -> int;
    auto set_coalescing_interval_ms(int ms) -> void;

    // State query
    auto is_busy() const -> bool;

    // Actions
    virtual auto send_request() -> void = 0;
    auto cancel() -> void;

signals:
    void incremental_chunk(const QString &text);
    void completion(const QString &full_text, const token_stats &stats);
    void cancelled();
    void error_occurred(const error_frame &err);
    void endpoint_url_changed(const QString &url);
    void temperature_changed(float temp);
    void coalescing_interval_ms_changed(int ms);

protected:
    QNetworkAccessManager *m_network_manager;
    QNetworkReply *m_network_reply;
    QTimer *m_coalescing_timer;

    QString m_endpoint_url;
    float m_temperature = 0.7f;
    int m_coalescing_interval_ms = 250;

    QString m_accumulated_text;
    QString m_chunk_buffer;
    token_stats m_current_stats;
    bool m_busy = false;
    bool m_cancelled = false;

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
};

// ============================================================================
// Image-to-text client
// ============================================================================

class image_to_text_client : public model_client_base {
    Q_OBJECT

public:
    explicit image_to_text_client(QObject *parent = nullptr);

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
