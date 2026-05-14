// SPDX-License-Identifier: MIT
//
// include/stratforge/stats/result.hpp — Hypothesis test single-row result.
//
// 8-field shape locked by the Signal Discovery Round 2 contract.
// LLM-generated `test_<id>(std::span<const stratforge::Bar>)` returns a
// vector of these; downstream consumers serialise them to JSON.
//
// Field names, order, and types are load-bearing — they must match the
// upstream contract exactly. Renaming or reordering breaks LLM output
// compilation.

#pragma once

#include <map>
#include <string>
#include <variant>

namespace stratforge::stats {

/// Value type carried by HypothesisResult::parameters.
/// Mirrors the JSON-serialisable scalar set the LLM is allowed to emit
/// (numeric, boolean, or short string). No nested objects, no arrays.
using HypothesisParameterValue = std::variant<double, long long, bool, std::string>;

/// Single-row output of one LLM-generated Round 2 test function.
///
/// Memory: not trivially copyable (std::string / std::map). Move is cheap;
/// copies are fine for the typical < 100 results per batch.
struct HypothesisResult {
    std::string                                     hypothesis_id;     // matches Round 1 hypothesis id
    std::string                                     test_name;         // e.g. "adf", "hurst_rs"
    double                                          p_value         = 1.0;
    double                                          test_statistic  = 0.0;
    double                                          effect_size     = 0.0;
    bool                                            is_significant  = false;
    std::string                                     summary;           // short human-readable verdict
    std::map<std::string, HypothesisParameterValue> parameters;        // hypothesis-specific knobs
};

}  // namespace stratforge::stats
