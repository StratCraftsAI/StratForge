// SPDX-License-Identifier: MIT
//
//  P4: Preflight -- validates artifact before execution starts.
// Parses the corrective JSON config block from the runner config,
// loads and validates the artifact, and returns a ready-to-use
// CandidateCollector with inference context attached.
//
// Preflight failures emit [CORRECTIVE_ERROR] on stdout for the
// executor-service.ts error propagation contract (D8).

#pragma once

#include <stratforge/corrective/candidate_collector.hpp>
#include <stratforge/corrective/corrective_contracts.hpp>
#include <stratforge/corrective/pop_calibrator.hpp>
#include <stratforge/corrective/pop_inference_engine.hpp>
#include <stratforge/corrective/pop_sizing_policy.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace stratforge::corrective {

// -----------------------------------------------------------------------
// Minimal JSON value extraction (avoids circular dep on backtest_runner.hpp)
// -----------------------------------------------------------------------

namespace detail {

[[nodiscard]] inline std::string json_get_string(
    std::string_view json, std::string_view key,
    std::string_view fallback = "") noexcept
{
    const std::string needle = "\"" + std::string(key) + "\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return std::string(fallback);
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return std::string(fallback);
    auto q1 = json.find('"', pos + 1);
    if (q1 == std::string_view::npos) return std::string(fallback);
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string_view::npos) return std::string(fallback);
    return std::string(json.substr(q1 + 1, q2 - q1 - 1));
}

[[nodiscard]] inline double json_get_double(
    std::string_view json, std::string_view key,
    double fallback = 0.0) noexcept
{
    const std::string needle = "\"" + std::string(key) + "\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return fallback;
    auto begin = json.find_first_of("-0123456789", pos + 1);
    if (begin == std::string_view::npos) return fallback;
    auto end = json.find_first_not_of("-+0123456789.eE", begin);
    try {
        return std::stod(std::string(json.substr(begin, end - begin)));
    } catch (...) {
        return fallback;
    }
}

[[nodiscard]] inline std::vector<float> parse_float_csv(std::string_view csv) {
    std::vector<float> result;
    std::size_t start = 0;
    while (start < csv.size()) {
        auto end = csv.find(',', start);
        if (end == std::string_view::npos) end = csv.size();
        try {
            result.push_back(std::stof(std::string(csv.substr(start, end - start))));
        } catch (...) {
            result.push_back(0.0f);
        }
        start = end + 1;
    }
    return result;
}

[[nodiscard]] inline std::vector<CalibrationBreakpoint> parse_breakpoints(
    std::string_view x_csv, std::string_view y_csv)
{
    auto xs = parse_float_csv(x_csv);
    auto ys = parse_float_csv(y_csv);
    std::vector<CalibrationBreakpoint> bps;
    const std::size_t n = std::min(xs.size(), ys.size());
    bps.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        bps.push_back({xs[i], ys[i]});
    }
    return bps;
}

} // namespace detail

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

struct CorrectiveSetup {
    std::unique_ptr<CandidateCollector> collector;
    std::unique_ptr<PopInferenceContext> inference_ctx;
};

[[nodiscard]] inline CorrectiveRuntimeConfig parse_corrective_config(
    std::string_view config_json)
{
    CorrectiveRuntimeConfig cfg;

    const auto state_str = detail::json_get_string(config_json, "corrective_state", "disabled");
    if (state_str == "collect_only") {
        cfg.state = GateVerdict::kCollectOnly;
    } else if (state_str == "enabled") {
        cfg.state = GateVerdict::kPass;
    } else {
        cfg.state = GateVerdict::kDisabled;
    }

    const auto mode_str = detail::json_get_string(config_json, "corrective_mode", "gate");
    if (mode_str == "sizing") {
        cfg.mode = SizingPolicy::kSizing;
    } else if (mode_str == "hybrid") {
        cfg.mode = SizingPolicy::kHybrid;
    } else {
        cfg.mode = SizingPolicy::kGate;
    }

    cfg.threshold = static_cast<float>(
        detail::json_get_double(config_json, "corrective_threshold", 0.5));
    cfg.sizing_exponent = static_cast<float>(
        detail::json_get_double(config_json, "corrective_sizing_exponent", 1.0));
    cfg.shadow_horizon_bars = static_cast<std::uint32_t>(
        detail::json_get_double(config_json, "corrective_shadow_horizon_bars", 50.0));

    return cfg;
}

[[nodiscard]] inline ArtifactManifest parse_artifact_manifest(
    std::string_view config_json)
{
    ArtifactManifest manifest;

    manifest.model_path = detail::json_get_string(config_json, "corrective_model_path");
    manifest.content_hash = detail::json_get_string(config_json, "corrective_content_hash");
    manifest.schema_version = static_cast<std::uint32_t>(
        detail::json_get_double(config_json, "corrective_schema_version", 1.0));
    manifest.feature_schema_hash = static_cast<std::uint32_t>(
        detail::json_get_double(config_json, "corrective_feature_schema_hash", 0.0));
    manifest.feature_count = static_cast<std::size_t>(
        detail::json_get_double(config_json, "corrective_feature_count",
                                static_cast<double>(kPopFeatureCountV1)));

    const auto cal_method = detail::json_get_string(config_json, "corrective_calibration_method", "isotonic");
    if (cal_method == "platt") {
        manifest.calibration_method = CalibrationMethod::kPlatt;
        manifest.platt_a = static_cast<float>(
            detail::json_get_double(config_json, "corrective_platt_a", 0.0));
        manifest.platt_b = static_cast<float>(
            detail::json_get_double(config_json, "corrective_platt_b", 0.0));
    } else {
        manifest.calibration_method = CalibrationMethod::kIsotonic;
        const auto bp_x_str = detail::json_get_string(config_json, "corrective_bp_x");
        const auto bp_y_str = detail::json_get_string(config_json, "corrective_bp_y");
        if (!bp_x_str.empty() && !bp_y_str.empty()) {
            manifest.isotonic_breakpoints = detail::parse_breakpoints(bp_x_str, bp_y_str);
        }
    }

    const auto gv_features_str = detail::json_get_string(config_json, "corrective_gv_features");
    if (!gv_features_str.empty()) {
        auto values = detail::parse_float_csv(gv_features_str);
        for (std::size_t i = 0; i < kPopFeatureCountV1 && i < values.size(); ++i) {
            manifest.golden_vector.input_features[i] = values[i];
        }
    }
    manifest.golden_vector.expected_probability = static_cast<float>(
        detail::json_get_double(config_json, "corrective_gv_expected_prob", 0.0));

    const auto gv_verdict = detail::json_get_string(config_json, "corrective_gv_expected_verdict", "pass");
    if (gv_verdict == "reject") {
        manifest.golden_vector.expected_verdict = GateVerdict::kReject;
    } else {
        manifest.golden_vector.expected_verdict = GateVerdict::kPass;
    }

    return manifest;
}

[[nodiscard]] inline CorrectiveSetup build_corrective_setup(
    std::string_view config_json)
{
    CorrectiveSetup setup;
    auto cfg = parse_corrective_config(config_json);

    if (cfg.state == GateVerdict::kDisabled) {
        return setup;
    }

    CollectorConfig ccfg;
    ccfg.corrective = cfg;
    ccfg.shadow_horizon_bars = cfg.shadow_horizon_bars;

    setup.collector = std::make_unique<CandidateCollector>(ccfg);

    if (cfg.state == GateVerdict::kPass) {
        auto manifest = parse_artifact_manifest(config_json);
        PopSizingPolicy policy(cfg.mode, cfg.threshold, cfg.sizing_exponent);

        std::string error;
        setup.inference_ctx = PopInferenceContext::create(manifest, policy, error);
        if (!setup.inference_ctx) {
            std::cout << "[CORRECTIVE_ERROR] " << error << "\n";
            std::cout.flush();
            setup.collector.reset();
            return setup;
        }
        setup.collector->set_inference_context(setup.inference_ctx.get());
    }

    return setup;
}

} // namespace stratforge::corrective
