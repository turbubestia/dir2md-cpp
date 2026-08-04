#pragma once

#include <string>
#include <vector>
#include <utility>

#include <fzy/match.h>

namespace dir2md::backend::fzy {

using score_t = ::score_t;

/**
 * @brief Check if a needle matches a haystack using fzy fuzzy matching.
 *
 * Returns non-zero if the needle matches the haystack, zero otherwise.
 * An empty needle always returns zero (never considered a match).
 *
 * @param needle The pattern to search for.
 * @param haystack The string to search in.
 * @return Non-zero if matched, zero otherwise.
 */
auto has_match(const std::string &needle, const std::string &haystack) -> int;

/**
 * @brief Compute the fzy match score for a needle against a single haystack.
 *
 * Returns the fzy score for the match. An empty needle or a non-matching
 * target returns SCORE_MIN.
 *
 * @param needle The pattern to search for.
 * @param haystack The string to search in.
 * @return The match score, or SCORE_MIN if no match.
 */
auto match(const std::string &needle, const std::string &haystack) -> score_t;

/**
 * @brief Compute the fzy match score and character positions for a single target.
 *
 * Returns the score and a vector of character positions in needle order.
 * An empty needle or non-match returns SCORE_MIN with an empty position vector.
 *
 * @param needle The pattern to search for.
 * @param haystack The string to search in.
 * @return A pair of (score, position_vector). Score is SCORE_MIN and positions
 *         are empty if no match.
 */
auto match_positions(const std::string &needle, const std::string &haystack)
    -> std::pair<score_t, std::vector<size_t>>;

/**
 * @brief Batch match a needle against multiple targets.
 *
 * Returns original indices of matching targets, sorted by descending score
 * with stable ordering for ties (equal scores retain input order).
 * An empty needle returns an empty vector. Non-matching targets are excluded.
 *
 * @param needle The pattern to search for.
 * @param haystack Vector of strings to search in.
 * @return Vector of original indices sorted by descending score.
 */
auto match(const std::string &needle, const std::vector<std::string> &haystack)
    -> std::vector<size_t>;

/**
 * @brief Batch match with positions for multiple targets.
 *
 * Returns pairs of (original_index, position_vector) for matching targets
 * in the same descending-score order as the batch match overload.
 * An empty needle returns an empty vector. Non-matching targets are excluded.
 *
 * @param needle The pattern to search for.
 * @param haystack Vector of strings to search in.
 * @return Vector of (index, positions) pairs sorted by descending score.
 */
auto match_positions(const std::string &needle, const std::vector<std::string> &haystack)
    -> std::vector<std::pair<size_t, std::vector<size_t>>>;

} // namespace dir2md::backend::fzy
