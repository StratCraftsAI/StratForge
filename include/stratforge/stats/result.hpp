// SPDX-License-Identifier: MIT
//
// include/stratforge/stats/result.hpp — Hypothesis test single-row result.
//
// : Output of an LLM-generated Round 2 test function
// `test_<id>(std::span<const stratforge::Bar>)`. A test returns a vector of
// these. Owned std::string (not string_view) because LLM emits string literals
// that need to outlive the originating expression.

#pragma once

#include <string>

namespace stratforge::stats {

struct HypothesisResult {
    std::string hypothesis_id;
    std::string test_name;
    double      statistic = 0.0;
    double      p_value   = 0.0;
    bool        passed    = false;
    std::string note;
};

}  // namespace stratforge::stats
