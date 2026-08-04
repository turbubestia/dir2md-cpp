#include "fzy.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace dir2md::backend::fzy {

auto has_match(const std::string &needle, const std::string &haystack) -> int {
    if (needle.empty()) {
        return 0;
    }
    return ::has_match(needle.c_str(), haystack.c_str());
}

auto match(const std::string &needle, const std::string &haystack) -> score_t {
    if (needle.empty() || has_match(needle, haystack) == 0) {
        return SCORE_MIN;
    }
    return ::match(needle.c_str(), haystack.c_str());
}

auto match_positions(const std::string &needle, const std::string &haystack)
    -> std::pair<score_t, std::vector<size_t>> {
    if (needle.empty() || has_match(needle, haystack) == 0) {
        return {SCORE_MIN, std::vector<size_t>{}};
    }

    std::vector<size_t> positions(needle.size());
    score_t score = ::match_positions(needle.c_str(), haystack.c_str(), positions.data());

    if (score == SCORE_MIN) {
        return {SCORE_MIN, std::vector<size_t>{}};
    }

    return {score, std::move(positions)};
}

auto match(const std::string &needle, const std::vector<std::string> &haystack)
    -> std::vector<size_t> {
    if (needle.empty()) {
        return {};
    }

    // Collect (original_index, score) for all matches.
    struct match_entry {
        size_t index;
        score_t score;
    };

    std::vector<match_entry> matches;
    matches.reserve(haystack.size());

    for (size_t i = 0; i < haystack.size(); ++i) {
        score_t s = match(needle, haystack[i]);
        if (s > SCORE_MIN) {
            matches.push_back({i, s});
        }
    }

    // Stable sort by descending score so equal scores retain input order.
    std::stable_sort(matches.begin(), matches.end(),
                     [](const match_entry &a, const match_entry &b) {
                         return a.score > b.score;
                     });

    // Extract indices.
    std::vector<size_t> result;
    result.reserve(matches.size());
    for (const auto &entry : matches) {
        result.push_back(entry.index);
    }

    return result;
}

auto match_positions(const std::string &needle, const std::vector<std::string> &haystack)
    -> std::vector<std::pair<size_t, std::vector<size_t>>> {
    if (needle.empty()) {
        return {};
    }

    // Collect (original_index, score) for all matches using same logic as batch match.
    struct match_entry {
        size_t index;
        score_t score;
    };

    std::vector<match_entry> matches;
    matches.reserve(haystack.size());

    for (size_t i = 0; i < haystack.size(); ++i) {
        score_t s = match(needle, haystack[i]);
        if (s > SCORE_MIN) {
            matches.push_back({i, s});
        }
    }

    // Stable sort by descending score.
    std::stable_sort(matches.begin(), matches.end(),
                     [](const match_entry &a, const match_entry &b) {
                         return a.score > b.score;
                     });

    // For each matched entry, get the position vector.
    std::vector<std::pair<size_t, std::vector<size_t>>> result;
    result.reserve(matches.size());
    for (const auto &entry : matches) {
        auto [score, positions] = match_positions(needle, haystack[entry.index]);
        result.push_back({entry.index, std::move(positions)});
    }

    return result;
}

} // namespace dir2md::backend::fzy
