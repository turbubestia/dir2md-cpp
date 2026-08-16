#include <backend/core/thinking_stripper.hpp>

#include <QObject>
#include <QTest>

class thinking_stripper_test : public QObject {
    Q_OBJECT

private slots:
    void test_plain_text_passthrough();
    void test_complete_think_block();
    void test_complete_reasoning_block();
    void test_tag_split_across_fragments();
    void test_adjacent_blocks();
    void test_nested_blocks();
    void test_indicator_emitted_once_per_block();
    void test_case_insensitive_tags();
    void test_unrecognized_tags_pass_through();
    void test_flush_releases_pending_tail();
    void test_flush_discards_unclosed_block_tail();
    void test_reset_reuse();
};

using namespace dir2md::backend;

void thinking_stripper_test::test_plain_text_passthrough() {
    thinking_stripper stripper;
    QCOMPARE(stripper.process("hello world"), QString("hello world"));
    QCOMPARE(stripper.process("more text"), QString("more text"));
    QVERIFY(!stripper.is_thinking());
}

void thinking_stripper_test::test_complete_think_block() {
    thinking_stripper stripper;
    QCOMPARE(stripper.process("a<think>secret</think>b"), QString("athinking ...b"));
    QVERIFY(!stripper.is_thinking());
}

void thinking_stripper_test::test_complete_reasoning_block() {
    thinking_stripper stripper;
    QCOMPARE(stripper.process("x<reasoning>r</reasoning>y"), QString("xthinking ...y"));
    QVERIFY(!stripper.is_thinking());
}

void thinking_stripper_test::test_tag_split_across_fragments() {
    thinking_stripper stripper;
    // Opening tag split across two fragments, closing tag split across two more.
    QCOMPARE(stripper.process("hello <th"), QString("hello "));
    QCOMPARE(stripper.process("ink>secret</thin"), QString("thinking ..."));
    QVERIFY(stripper.is_thinking());
    QCOMPARE(stripper.process("k>world"), QString("world"));
    QVERIFY(!stripper.is_thinking());
}

void thinking_stripper_test::test_adjacent_blocks() {
    thinking_stripper stripper;
    // Two back-to-back blocks: indicator emitted once per block.
    QCOMPARE(stripper.process("<think>a</think>b<think>c</think>d"),
             QString("thinking ...bthinking ...d"));
    QVERIFY(!stripper.is_thinking());
}

void thinking_stripper_test::test_nested_blocks() {
    thinking_stripper stripper;
    // Inner open tag is content of the outer block; first matching close ends it.
    QCOMPARE(stripper.process("<think>a<think>b</think>c"), QString("thinking ...c"));
    QVERIFY(!stripper.is_thinking());
}

void thinking_stripper_test::test_indicator_emitted_once_per_block() {
    thinking_stripper stripper;
    QCOMPARE(stripper.process("p<think>q"), QString("pthinking ..."));
    QVERIFY(stripper.is_thinking());
    // Subsequent fragments while the block is open emit nothing, no repeat indicator.
    QCOMPARE(stripper.process("more"), QString(""));
    QVERIFY(stripper.is_thinking());
    QCOMPARE(stripper.process("r</think>end"), QString("end"));
    QVERIFY(!stripper.is_thinking());
}

void thinking_stripper_test::test_case_insensitive_tags() {
    thinking_stripper stripper;
    QCOMPARE(stripper.process("a<THINK>b</THINK>c"), QString("athinking ...c"));
    QCOMPARE(stripper.process("d<Reasoning>e</REASONING>f"), QString("dthinking ...f"));
}

void thinking_stripper_test::test_unrecognized_tags_pass_through() {
    thinking_stripper stripper;
    // `thinking` is not a recognized block tag; it passes through unchanged.
    QCOMPARE(stripper.process("<thinking>hi</thinking>"), QString("<thinking>hi</thinking>"));
}

void thinking_stripper_test::test_flush_releases_pending_tail() {
    thinking_stripper stripper;
    // Trailing partial opening tag is held back, then released at end of stream.
    QCOMPARE(stripper.process("a <th"), QString("a "));
    QCOMPARE(stripper.flush(), QString("<th"));
    QVERIFY(!stripper.is_thinking());
}

void thinking_stripper_test::test_flush_discards_unclosed_block_tail() {
    thinking_stripper stripper;
    // Unclosed block at end of stream: held tag fragment must not leak.
    QCOMPARE(stripper.process("a<think>b</thin"), QString("athinking ..."));
    QVERIFY(stripper.is_thinking());
    QCOMPARE(stripper.flush(), QString(""));
}

void thinking_stripper_test::test_reset_reuse() {
    thinking_stripper stripper;
    QCOMPARE(stripper.process("x<think>secret"), QString("xthinking ..."));
    QVERIFY(stripper.is_thinking());

    stripper.reset();
    QVERIFY(!stripper.is_thinking());

    // Reused instance behaves like a fresh one: no indicator, plain passthrough.
    QCOMPARE(stripper.process("hello"), QString("hello"));
}

QTEST_MAIN(thinking_stripper_test)
#include "thinking_stripper_test.moc"
