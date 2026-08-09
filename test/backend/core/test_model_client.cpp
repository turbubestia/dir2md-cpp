#include "test_model_client.hpp"

#include <backend/core/model.hpp>

#include <QSignalSpy>

using namespace dir2md::backend;

// ============================================================================
// Property tests
// ============================================================================

void test_model_client::test_endpoint_url_property() {
    text_to_text_client client;
    QCOMPARE(client.get_endpoint_url(), QString(""));

    client.set_endpoint_url("http://localhost:8080");
    QCOMPARE(client.get_endpoint_url(), QString("http://localhost:8080"));
}

void test_model_client::test_temperature_property() {
    text_to_text_client client;
    QCOMPARE(client.get_temperature(), 0.7f);

    client.set_temperature(0.5f);
    QCOMPARE(client.get_temperature(), 0.5f);
}

void test_model_client::test_coalescing_interval_property() {
    text_to_text_client client;
    QCOMPARE(client.get_coalescing_interval_ms(), 250);

    client.set_coalescing_interval_ms(100);
    QCOMPARE(client.get_coalescing_interval_ms(), 100);
}

// ============================================================================
// Busy state tests
// ============================================================================

void test_model_client::test_initial_not_busy() {
    text_to_text_client client;
    QVERIFY(!client.is_busy());
}

void test_model_client::test_cancel_resets_busy() {
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

void test_model_client::test_cancel_signal_emitted() {
    text_to_text_client client;
    QSignalSpy spy(&client, &text_to_text_client::cancelled);

    client.cancel();
    QCOMPARE(spy.count(), 1);
}

// ============================================================================
// Token stats tests
// ============================================================================

void test_model_client::test_token_stats_default_values() {
    token_stats stats;
    QCOMPARE(stats.total_tokens, 0);
    QCOMPARE(stats.tokens_per_sec, 0.0);
    QCOMPARE(stats.generation_time_ms, qint64(0));
    QCOMPARE(stats.prompt_tokens, 0);
    QCOMPARE(stats.prompt_eval_time_ms, qint64(0));
}

// AUTOMOC handles moc generation
