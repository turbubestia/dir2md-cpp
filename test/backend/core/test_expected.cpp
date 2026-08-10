
#include <backend/core/expected.hpp>

#include <QObject>
#include <QTest>

#include <stdexcept>

using namespace dir2md::backend;

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

// ============================================================================
// expected<T> success state tests
// ============================================================================

void test_expected::test_make_expected_has_value() {
    auto exp = expected<int>::make_expected(42);
    QVERIFY(exp.has_value());
}

void test_expected::test_make_expected_value_returns_stored() {
    auto exp = expected<int>::make_expected(42);
    QCOMPARE(exp.value(), 42);
}

void test_expected::test_make_expected_value_or_returns_value() {
    auto exp = expected<QString>::make_expected(QString("hello"));
    QCOMPARE(exp.value_or("default"), QString("hello"));
}

// ============================================================================
// expected<T> error state tests
// ============================================================================

void test_expected::test_make_expected_error_no_value() {
    auto exp = expected<int>::make_expected_error(404, "Not found");
    QVERIFY(!exp.has_value());
}

void test_expected::test_make_expected_error_code_and_desc() {
    auto exp = expected<int>::make_expected_error(500, "Server error");
    QCOMPARE(exp.error().error_code, 500);
    QCOMPARE(exp.error().description, QString("Server error"));
}

void test_expected::test_make_expected_error_value_or_returns_default() {
    auto exp = expected<QString>::make_expected_error(404, "Not found");
    QCOMPARE(exp.value_or("default"), QString("default"));
}

void test_expected::test_make_expected_error_value_throws() {
    auto exp = expected<int>::make_expected_error(500, "Server error");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, exp.value());
}

// ============================================================================
// Copy semantics tests
// ============================================================================

void test_expected::test_copy_success_state() {
    auto original = expected<int>::make_expected(42);
    auto copy = original;
    QVERIFY(copy.has_value());
    QCOMPARE(copy.value(), 42);
}

void test_expected::test_copy_error_state() {
    auto original = expected<int>::make_expected_error(500, "Error");
    auto copy = original;
    QVERIFY(!copy.has_value());
    QCOMPARE(copy.error().error_code, 500);
}

// ============================================================================
// Move semantics tests
// ============================================================================

void test_expected::test_move_success_state() {
    auto original = expected<QString>::make_expected(QString("moved"));
    auto moved = std::move(original);
    QVERIFY(moved.has_value());
    QCOMPARE(moved.value(), QString("moved"));
}

void test_expected::test_move_error_state() {
    auto original = expected<int>::make_expected_error(500, "Error");
    auto moved = std::move(original);
    QVERIFY(!moved.has_value());
    QCOMPARE(moved.error().error_code, 500);
}

// ============================================================================
// error_frame tests
// ============================================================================

void test_expected::test_error_frame_source_location() {
    error_frame frame(42, "Test error");
    QCOMPARE(frame.error_code, 42);
    QCOMPARE(frame.description, QString("Test error"));
    // source_location should capture this file and a non-zero line
    QVERIFY(frame.location.line() > 0);
}

// ============================================================================
// error_stack tests
// ============================================================================

void test_expected::test_error_stack_push_and_frames() {
    error_stack stack;
    stack.push(error_frame(1, "First"));
    stack.push(error_frame(2, "Second"));

    auto frames = stack.frames();
    QCOMPARE(static_cast<int>(frames.size()), 2);
    QCOMPARE(frames[0].error_code, 1);
    QCOMPARE(frames[1].error_code, 2);
}

void test_expected::test_error_stack_clear() {
    error_stack stack;
    stack.push(error_frame(1, "First"));
    stack.clear();
    QVERIFY(stack.frames().empty());
}

void test_expected::test_error_stack_to_string() {
    error_stack stack;
    stack.push(error_frame(404, "Not found"));

    QString output = stack.to_string();
    QVERIFY(!output.isEmpty());
    QVERIFY(output.contains("404"));
    QVERIFY(output.contains("Not found"));
}

// AUTOMOC handles moc generation
QTEST_MAIN(test_expected)
#include "test_expected.moc"
