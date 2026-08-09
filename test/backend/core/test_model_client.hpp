#pragma once

#include <QObject>
#include <QTest>

class test_model_client : public QObject {
    Q_OBJECT

private slots:
    // Property tests
    void test_endpoint_url_property();
    void test_temperature_property();
    void test_coalescing_interval_property();

    // Busy state tests
    void test_initial_not_busy();
    void test_cancel_resets_busy();

    // Cancel tests
    void test_cancel_signal_emitted();

    // Token stats tests
    void test_token_stats_default_values();
};
