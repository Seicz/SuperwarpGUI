// fuzzy.h - port of the in-game addon's fuzzyfind (lean).
// Verified byte-identical to the Lua scorer via the Rust port; this file is
// a direct C++ transliteration of the Rust impl so the same property holds.
//
// Three tiers via dest_fuzzy/dest_approx:
//   exact:  cleaned candidate == cleaned needle  -> FUZZY_EXACT
//   fuzzy:  needle is a subseq of cleaned cand   -> gap-aware fzf-style score
//   approx: subseq of nothing                    -> closest by edit distance
//
// Lineage: research+assembly by Lili; gap-aware scorer + lean build by Eric
// Strawser (Seicz@Bahamut). Ported to Rust by ES; ported to C++ here.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "proto.h"

namespace fuzzy
{
    inline constexpr int64_t F_MATCH        = 16;
    inline constexpr int64_t F_GAP_START    = -3;
    inline constexpr int64_t F_GAP_EXT      = -1;
    inline constexpr int64_t F_BON_BOUNDARY = 8;
    inline constexpr int64_t F_BON_CAMEL    = 7;
    inline constexpr int64_t F_BON_CONSEC   = 8;
    inline constexpr int64_t F_FIRST_MULT   = 2;
    inline constexpr int64_t F_NEG          = -1'000'000'000;
    inline constexpr int64_t FUZZY_EXACT    = 1'000'000;

    // Lowercase + alphanumeric-only filter.
    std::string clean(const std::string& s);

    // Gap-aware subsequence score. Returns nullopt when `needle` is not a
    // subsequence of `text`'s alphanumeric content.
    // `needle` MUST be pre-cleaned; `text` is raw (case + separators matter).
    std::optional<int64_t> score(const std::string& needle, const std::string& text);

    // Wagner-Fischer Levenshtein (two-row), for the approx fallback tier.
    size_t levenshtein(const std::string& a, const std::string& b);

    // Best-of-fields score for a destination, or nullopt if no field matches.
    std::optional<int64_t> dest_fuzzy(const proto::Dest& d, const std::string& needle_clean);

    // Closeness in 0..=10000 (higher = closer), for the no-subsequence tier.
    int64_t dest_approx(const proto::Dest& d, const std::string& needle_clean);
}
