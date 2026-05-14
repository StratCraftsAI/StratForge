// SPDX-License-Identifier: MIT
//
// include/stratforge/stats/result_json.hpp — Canonical JSON serialisation
// for HypothesisResult, owned by StratForge so the StratCraft Round 3 host
// binary does not redeclare the wire shape.
//
// Output is a single-line JSON object with the 8 field names exactly as
// declared in result.hpp. `parameters` values are inlined per their variant
// type (number, boolean, or quoted string). Iteration order of `parameters`
// follows std::map (lexicographic by key) so output is deterministic.

#pragma once

#include <stratforge/stats/result.hpp>

#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace stratforge::stats {

namespace detail::result_json {

inline void append_escaped(std::string& out, std::string_view s) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b");  break;
            case '\f': out.append("\\f");  break;
            case '\n': out.append("\\n");  break;
            case '\r': out.append("\\r");  break;
            case '\t': out.append("\\t");  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out.append(buf);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

inline void append_double(std::string& out, double v) {
    // NaN / Inf are not valid JSON numbers. Emit null so downstream parsers
    // do not reject the entire row; the consumer can re-flag as
    // "not significant" via p_value/is_significant fields.
    if (!std::isfinite(v)) {
        out.append("null");
        return;
    }
    char buf[32];
    // %.17g round-trips IEEE-754 doubles exactly.
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    out.append(buf);
}

inline void append_param_value(std::string& out, const HypothesisParameterValue& v) {
    std::visit([&out](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, double>) {
            append_double(out, x);
        } else if constexpr (std::is_same_v<T, long long>) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%lld", x);
            out.append(buf);
        } else if constexpr (std::is_same_v<T, bool>) {
            out.append(x ? "true" : "false");
        } else if constexpr (std::is_same_v<T, std::string>) {
            append_escaped(out, x);
        }
    }, v);
}

}  // namespace detail::result_json

/// Serialise to single-line JSON. Keys match the 8 field names exactly.
[[nodiscard]] inline std::string to_json(const HypothesisResult& r) {
    using detail::result_json::append_escaped;
    using detail::result_json::append_double;
    using detail::result_json::append_param_value;

    std::string out;
    out.reserve(128 + r.summary.size() + r.parameters.size() * 32);

    out.append("{\"hypothesis_id\":");
    append_escaped(out, r.hypothesis_id);
    out.append(",\"test_name\":");
    append_escaped(out, r.test_name);
    out.append(",\"p_value\":");
    append_double(out, r.p_value);
    out.append(",\"test_statistic\":");
    append_double(out, r.test_statistic);
    out.append(",\"effect_size\":");
    append_double(out, r.effect_size);
    out.append(",\"is_significant\":");
    out.append(r.is_significant ? "true" : "false");
    out.append(",\"summary\":");
    append_escaped(out, r.summary);
    out.append(",\"parameters\":{");
    bool first = true;
    for (const auto& [k, v] : r.parameters) {
        if (!first) out.push_back(',');
        first = false;
        append_escaped(out, k);
        out.push_back(':');
        append_param_value(out, v);
    }
    out.append("}}");
    return out;
}

/// Convenience: serialise a vector/span to a JSON array.
[[nodiscard]] inline std::string to_json(std::span<const HypothesisResult> rs) {
    std::string out;
    out.reserve(rs.size() * 192 + 2);
    out.push_back('[');
    bool first = true;
    for (const auto& r : rs) {
        if (!first) out.push_back(',');
        first = false;
        out.append(to_json(r));
    }
    out.push_back(']');
    return out;
}

}  // namespace stratforge::stats
