#pragma once

#include <QObject>
#include <QTest>

class fzy_test : public QObject {
    Q_OBJECT

private slots:
    // Single-target has_match tests -----------------------------------------

    void test_has_match_empty_needle();
    void test_has_match_non_matching();
    void test_has_match_matching();

    // Single-target match score tests ----------------------------------------

    void test_match_empty_needle();
    void test_match_non_matching();
    void test_match_matching();

    // Single-target match_positions tests ------------------------------------

    void test_match_positions_empty_needle();
    void test_match_positions_non_matching();
    void test_match_positions_matching();

    // Batch match tests ------------------------------------------------------

    void test_batch_match_sample();
    void test_batch_match_empty_collection();
    void test_batch_match_empty_target();
    void test_batch_match_no_matches();
    void test_batch_match_duplicate_targets();
    void test_batch_match_equal_scores();

    // Batch match_positions tests --------------------------------------------

    void test_batch_match_positions_sample();
    void test_batch_match_positions_consistency();
    void test_batch_match_positions_position_validity();

    // Edge case tests --------------------------------------------------------

    void test_needle_longer_than_target();
    void test_empty_needle_batch_all_overloads();
};
