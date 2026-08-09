#pragma once

#include <QObject>
#include <QTest>

class test_expected : public QObject {
    Q_OBJECT

private slots:
    // expected<T> success state tests
    void test_make_expected_has_value();
    void test_make_expected_value_returns_stored();
    void test_make_expected_value_or_returns_value();

    // expected<T> error state tests
    void test_make_expected_error_no_value();
    void test_make_expected_error_code_and_desc();
    void test_make_expected_error_value_or_returns_default();
    void test_make_expected_error_value_throws();

    // Copy semantics tests
    void test_copy_success_state();
    void test_copy_error_state();

    // Move semantics tests
    void test_move_success_state();
    void test_move_error_state();

    // error_frame tests
    void test_error_frame_source_location();

    // error_stack tests
    void test_error_stack_push_and_frames();
    void test_error_stack_clear();
    void test_error_stack_to_string();
};
