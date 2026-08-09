#include "fzy_test.hpp"

#include <backend/core/fzy.hpp>

#include <cfloat>

using namespace dir2md::backend::fzy;

// ============================================================================
// Single-target has_match tests
// ============================================================================

void fzy_test::test_has_match_empty_needle() {
    // Empty needle always returns zero (never a match).
    QCOMPARE(dir2md::backend::fzy::has_match("", "any_target"), 0);
    QCOMPARE(dir2md::backend::fzy::has_match("", ""), 0);
    QCOMPARE(dir2md::backend::fzy::has_match("", "editor.fontSize"), 0);
}

void fzy_test::test_has_match_non_matching() {
    // Completely non-matching needle returns zero.
    QCOMPARE(has_match("xyz", "no_match_here"), 0);
    QCOMPARE(has_match("zzz", "abcdef"), 0);
}

void fzy_test::test_has_match_matching() {
    // Valid fuzzy match returns non-zero.
    QVERIFY(has_match("edf", "editor.fontSize") != 0);
    QVERIFY(has_match("efs", "editor.fontSize") != 0);
    QVERIFY(has_match("files", "files.autoSave") != 0);
}

// ============================================================================
// Single-target match score tests
// ============================================================================

void fzy_test::test_match_empty_needle() {
    // Empty needle returns SCORE_MIN.
    QCOMPARE(match("", "any_target"), SCORE_MIN);
    QCOMPARE(match("", ""), SCORE_MIN);
}

void fzy_test::test_match_non_matching() {
    // Non-matching target returns SCORE_MIN.
    QCOMPARE(match("xyz", "no_match_here"), SCORE_MIN);
    QCOMPARE(match("zzz", "abcdef"), SCORE_MIN);
}

void fzy_test::test_match_matching() {
    // Valid match returns score greater than SCORE_MIN.
    QVERIFY(match("edf", "editor.fontSize") > SCORE_MIN);
    QVERIFY(match("efs", "editor.fontSize") > SCORE_MIN);
    QVERIFY(match("files", "files.autoSave") > SCORE_MIN);
}

// ============================================================================
// Single-target match_positions tests
// ============================================================================

void fzy_test::test_match_positions_empty_needle() {
    // Empty needle returns {SCORE_MIN, {}}.
    auto [score, positions] = match_positions("", "any_target");
    QCOMPARE(score, SCORE_MIN);
    QVERIFY(positions.empty());
}

void fzy_test::test_match_positions_non_matching() {
    // Non-match returns {SCORE_MIN, {}}.
    auto [score, positions] = match_positions("xyz", "no_match_here");
    QCOMPARE(score, SCORE_MIN);
    QVERIFY(positions.empty());
}

void fzy_test::test_match_positions_matching() {
    // Valid match returns score and position vector with correct size.
    auto [score, positions] = match_positions("edf", "editor.fontSize");
    QVERIFY(score > SCORE_MIN);
    QCOMPARE(static_cast<int>(positions.size()), 3); // One position per needle char.

    // Verify positions identify the correct characters in order.
    std::string haystack = "editor.fontSize";
    QCOMPARE(haystack[positions[0]], 'e');
    QCOMPARE(haystack[positions[1]], 'd');
    QCOMPARE(haystack[positions[2]], 'f');
}

// ============================================================================
// Batch match tests
// ============================================================================

void fzy_test::test_batch_match_sample() {
    // The supplied sample from the plan: "edfont" against three targets.
    std::string needle = "edfont";
    std::vector<std::string> targets = {
        "files.autoSave",
        "terminal.integrated.fontSize",
        "editor.fontSize"
    };

    auto result = match(needle, targets);

    // Expected: indices [2, 1] (editor.fontSize and terminal.integrated.fontSize).
    QCOMPARE(result.size(), 2u);
    QCOMPARE(result[0], 2u);
    QCOMPARE(result[1], 1u);
}

void fzy_test::test_batch_match_empty_collection() {
    // Empty target vector returns empty result.
    std::vector<std::string> targets;
    auto result = match("test", targets);
    QVERIFY(result.empty());
}

void fzy_test::test_batch_match_empty_target() {
    // Non-empty needle against an empty target string does not match.
    std::vector<std::string> targets = {"", "editor.fontSize", "files.autoSave"};
    auto result = match("efs", targets);

    // Empty string should not appear in results.
    for (size_t idx : result) {
        QVERIFY(targets[idx].size() > 0);
    }
}

void fzy_test::test_batch_match_no_matches() {
    // Needle that matches nothing returns empty result.
    std::vector<std::string> targets = {"abc", "def", "ghi"};
    auto result = match("xyz", targets);
    QVERIFY(result.empty());
}

void fzy_test::test_batch_match_duplicate_targets() {
    // Duplicate target strings are distinguished by original index.
    std::vector<std::string> targets = {"editor.fontSize", "editor.fontSize", "files.autoSave"};
    auto result = match("efs", targets);

    // Both indices 0 and 1 should appear since they have identical content.
    QVERIFY(result.size() >= 2u);
    bool has_0 = false, has_1 = false;
    for (size_t idx : result) {
        if (idx == 0) has_0 = true;
        if (idx == 1) has_1 = true;
    }
    QVERIFY(has_0);
    QVERIFY(has_1);
}

void fzy_test::test_batch_match_equal_scores() {
    // Equal scores retain input order (ascending original index).
    std::vector<std::string> targets = {"abc", "abc", "abc"};
    auto result = match("ac", targets);

    QCOMPARE(result.size(), 3u);
    QCOMPARE(result[0], 0u);
    QCOMPARE(result[1], 1u);
    QCOMPARE(result[2], 2u);
}

// ============================================================================
// Batch match_positions tests
// ============================================================================

void fzy_test::test_batch_match_positions_sample() {
    // The supplied sample: "edfont" against three targets.
    std::string needle = "edfont";
    std::vector<std::string> targets = {
        "files.autoSave",
        "terminal.integrated.fontSize",
        "editor.fontSize"
    };

    auto result = match_positions(needle, targets);

    // Expected: two pairs indexed 2 and 1.
    QCOMPARE(result.size(), 2u);
    QCOMPARE(result[0].first, 2u); // editor.fontSize
    QCOMPARE(result[1].first, 1u); // terminal.integrated.fontSize

    // Verify position vectors match expected offsets.
    // editor.fontSize: "edfont" -> positions [0, 1, 7, 8, 9, 10]
    std::vector<size_t> expected_pos_0 = {0, 1, 7, 8, 9, 10};
    QCOMPARE(result[0].second, expected_pos_0);

    // terminal.integrated.fontSize: "edfont" -> positions [17, 18, 20, 21, 22, 23]
    std::vector<size_t> expected_pos_1 = {17, 18, 20, 21, 22, 23};
    QCOMPARE(result[1].second, expected_pos_1);
}

void fzy_test::test_batch_match_positions_consistency() {
    // Batch index order matches batch position order for the same input.
    std::string needle = "efs";
    std::vector<std::string> targets = {
        "files.autoSave",
        "terminal.integrated.fontSize",
        "editor.fontSize"
    };

    auto indices = match(needle, targets);
    auto positions = match_positions(needle, targets);

    QCOMPARE(indices.size(), positions.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        QCOMPARE(indices[i], positions[i].first);
    }
}

void fzy_test::test_batch_match_positions_position_validity() {
    // Every returned position is in range for its indexed target and identifies
    // the corresponding needle character.
    std::string needle = "edfont";
    std::vector<std::string> targets = {
        "files.autoSave",
        "terminal.integrated.fontSize",
        "editor.fontSize"
    };

    auto result = match_positions(needle, targets);

    for (const auto &[idx, positions] : result) {
        const std::string &target = targets[idx];
        QCOMPARE(static_cast<size_t>(positions.size()), needle.size());

        for (size_t i = 0; i < positions.size(); ++i) {
            QVERIFY(positions[i] < target.size());
            QCOMPARE(target[positions[i]], needle[i]);
        }
    }
}

// ============================================================================
// Edge case tests
// ============================================================================

void fzy_test::test_needle_longer_than_target() {
    // Needle longer than target does not cause undefined behavior.
    std::string long_needle = "verylongneedle";
    std::string short_target = "ab";

    // Single-target: should not match.
    QCOMPARE(has_match(long_needle, short_target), 0);
    QCOMPARE(match(long_needle, short_target), SCORE_MIN);

    auto [score, positions] = match_positions(long_needle, short_target);
    QCOMPARE(score, SCORE_MIN);
    QVERIFY(positions.empty());

    // Batch: should not match.
    std::vector<std::string> targets = {"ab", "cd", "ef"};
    auto result = match(long_needle, targets);
    QVERIFY(result.empty());

    auto pos_result = match_positions(long_needle, targets);
    QVERIFY(pos_result.empty());
}

void fzy_test::test_empty_needle_batch_all_overloads() {
    // Empty needle produces empty results from all batch overloads.
    std::vector<std::string> targets = {"abc", "def", "ghi"};

    auto indices = match("", targets);
    QVERIFY(indices.empty());

    auto positions = match_positions("", targets);
    QVERIFY(positions.empty());
}
