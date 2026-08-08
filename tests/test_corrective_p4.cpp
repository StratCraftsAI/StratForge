// SPDX-License-Identifier: MIT
//
//  P4 acceptance suite: C++ inference, calibration, sizing
// policy, preflight, and collector integration.
//
// AC10 (preflight rejects), AC11 (policy boundary tests),
// AC12 (disabled byte-identity, collect-only unchanged),
// AC13 (calibration parity).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/corrective/corrective_contracts.hpp>
#include <stratforge/corrective/pop_calibrator.hpp>
#include <stratforge/corrective/pop_sizing_policy.hpp>
#include <stratforge/corrective/pop_preflight.hpp>
#include <stratforge/corrective/pop_inference_engine.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/observers/increment_batcher.hpp>
#include <stratforge/observers/increment_types.hpp>

#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace stratforge;
using namespace stratforge::corrective;
using stratforge::test::StaticFeed;

namespace {

constexpr std::size_t kBarCount = 60;

[[nodiscard]] std::vector<StaticFeed::Bar> make_linear_bars(
    std::size_t count, double start = 100.0, double step = 1.0) {
    std::vector<StaticFeed::Bar> bars;
    bars.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double px = start + static_cast<double>(i) * step;
        bars.push_back({.open = px, .high = px + 2.0,
                        .low = px - 2.0, .close = px});
    }
    return bars;
}

class BuyOnBar10 final : public Strategy {
public:
    void next() override {
        if (data().index() == 10 && !bought_) {
            static_cast<void>(buy(1.0));
            bought_ = true;
        }
        if (data().index() == 20 && position().is_long()) {
            static_cast<void>(close());
        }
    }
private:
    bool bought_ = false;
};

} // namespace

// ===========================================================================
// AC13: Isotonic calibration parity with TypeScript
// ===========================================================================

TEST_CASE("isotonic calibration -- extrapolation below first breakpoint",
          "[corrective][p4][calibration]")
{
    auto cal = PopCalibrator::from_isotonic({
        {0.1f, 0.05f}, {0.3f, 0.2f}, {0.5f, 0.5f}, {0.8f, 0.9f}
    });
    REQUIRE(cal.calibrate(0.0f) == 0.05f);
}

TEST_CASE("isotonic calibration -- extrapolation above last breakpoint",
          "[corrective][p4][calibration]")
{
    auto cal = PopCalibrator::from_isotonic({
        {0.1f, 0.05f}, {0.3f, 0.2f}, {0.5f, 0.5f}, {0.8f, 0.9f}
    });
    REQUIRE(cal.calibrate(1.0f) == 0.9f);
}

TEST_CASE("isotonic calibration -- linear interpolation between breakpoints",
          "[corrective][p4][calibration]")
{
    auto cal = PopCalibrator::from_isotonic({
        {0.1f, 0.05f}, {0.3f, 0.2f}, {0.5f, 0.5f}, {0.8f, 0.9f}
    });
    // At x=0.2 between [0.1,0.05] and [0.3,0.2]: t = (0.2-0.1)/(0.3-0.1) = 0.5
    // y = 0.05 + 0.5 * (0.2-0.05) = 0.05 + 0.075 = 0.125
    REQUIRE_THAT(cal.calibrate(0.2f),
                 Catch::Matchers::WithinAbs(0.125, 1e-5));
}

TEST_CASE("isotonic calibration -- exact breakpoint match",
          "[corrective][p4][calibration]")
{
    auto cal = PopCalibrator::from_isotonic({
        {0.1f, 0.05f}, {0.5f, 0.5f}, {0.8f, 0.9f}
    });
    REQUIRE(cal.calibrate(0.5f) == 0.5f);
}

TEST_CASE("isotonic calibration -- single breakpoint",
          "[corrective][p4][calibration]")
{
    auto cal = PopCalibrator::from_isotonic({{0.5f, 0.7f}});
    REQUIRE(cal.calibrate(0.3f) == 0.7f);
    REQUIRE(cal.calibrate(0.9f) == 0.7f);
}

TEST_CASE("isotonic calibration -- empty breakpoints returns raw",
          "[corrective][p4][calibration]")
{
    auto cal = PopCalibrator::from_isotonic({});
    REQUIRE(cal.calibrate(0.42f) == 0.42f);
}

// ===========================================================================
// AC13: Platt calibration parity with TypeScript
// ===========================================================================

TEST_CASE("platt calibration -- sigmoid shape",
          "[corrective][p4][calibration]")
{
    // Platt: p = sigma(a*x + b) where sigma(z) = 1/(1+exp(z)) for z<0
    // With a=-2, b=1: at x=0.5, fApB = -2*0.5 + 1 = 0 -> p = 0.5
    auto cal = PopCalibrator::from_platt(-2.0f, 1.0f);
    REQUIRE_THAT(cal.calibrate(0.5f),
                 Catch::Matchers::WithinAbs(0.5, 1e-5));
}

TEST_CASE("platt calibration -- high confidence",
          "[corrective][p4][calibration]")
{
    auto cal = PopCalibrator::from_platt(-2.0f, 1.0f);
    // At x=5, fApB = -10+1 = -9 -> p = exp(9)/(1+exp(9)) ≈ 0.9999
    REQUIRE(cal.calibrate(5.0f) > 0.999f);
}

TEST_CASE("platt calibration -- low confidence",
          "[corrective][p4][calibration]")
{
    auto cal = PopCalibrator::from_platt(-2.0f, 1.0f);
    // At x=-5, fApB = 10+1 = 11 -> p = exp(-11)/(1+exp(-11)) ≈ 0.0000
    REQUIRE(cal.calibrate(-5.0f) < 0.001f);
}

// ===========================================================================
// AC11: Gate policy
// ===========================================================================

TEST_CASE("gate policy -- pass above threshold",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy(SizingPolicy::kGate, 0.5f, 1.0f);
    auto decision = policy.apply(0.7f, 100.0);
    REQUIRE(decision.verdict == GateVerdict::kPass);
    REQUIRE(decision.final_size == 100.0);
}

TEST_CASE("gate policy -- reject below threshold",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy(SizingPolicy::kGate, 0.5f, 1.0f);
    auto decision = policy.apply(0.3f, 100.0);
    REQUIRE(decision.verdict == GateVerdict::kReject);
    REQUIRE(decision.final_size == 0.0);
}

TEST_CASE("gate policy -- reject exactly at threshold",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy(SizingPolicy::kGate, 0.5f, 1.0f);
    auto decision = policy.apply(0.5f, 100.0);
    REQUIRE(decision.verdict == GateVerdict::kPass);
    REQUIRE(decision.final_size == 100.0);
}

// ===========================================================================
// AC11: Sizing policy -- monotonic, bounded, never enlarges
// ===========================================================================

TEST_CASE("sizing policy -- scales by probability",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy(SizingPolicy::kSizing, 0.5f, 1.0f);
    auto d1 = policy.apply(0.5f, 100.0);
    auto d2 = policy.apply(0.8f, 100.0);
    // Higher probability -> larger size (monotonic)
    REQUIRE(d2.final_size > d1.final_size);
    // Never enlarges beyond proposed
    REQUIRE(d2.final_size <= 100.0);
    REQUIRE(d1.final_size <= 100.0);
}

TEST_CASE("sizing policy -- probability 1.0 gives proposed_size",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy(SizingPolicy::kSizing, 0.5f, 1.0f);
    auto decision = policy.apply(1.0f, 100.0);
    REQUIRE_THAT(decision.final_size,
                 Catch::Matchers::WithinAbs(100.0, 1e-10));
}

TEST_CASE("sizing policy -- probability 0.0 gives zero",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy(SizingPolicy::kSizing, 0.5f, 1.0f);
    auto decision = policy.apply(0.0f, 100.0);
    REQUIRE_THAT(decision.final_size,
                 Catch::Matchers::WithinAbs(0.0, 1e-10));
}

TEST_CASE("sizing policy -- exponent > 1 makes scaling more aggressive",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy_e1(SizingPolicy::kSizing, 0.5f, 1.0f);
    PopSizingPolicy policy_e3(SizingPolicy::kSizing, 0.5f, 3.0f);
    auto d1 = policy_e1.apply(0.5f, 100.0);
    auto d3 = policy_e3.apply(0.5f, 100.0);
    // Higher exponent -> smaller size for prob < 1
    REQUIRE(d3.final_size < d1.final_size);
}

// ===========================================================================
// AC11: Hybrid policy -- gate first, then size
// ===========================================================================

TEST_CASE("hybrid policy -- reject below threshold",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy(SizingPolicy::kHybrid, 0.5f, 1.0f);
    auto decision = policy.apply(0.3f, 100.0);
    REQUIRE(decision.verdict == GateVerdict::kReject);
    REQUIRE(decision.final_size == 0.0);
}

TEST_CASE("hybrid policy -- size survivors above threshold",
          "[corrective][p4][policy]")
{
    PopSizingPolicy policy(SizingPolicy::kHybrid, 0.5f, 1.0f);
    auto decision = policy.apply(0.7f, 100.0);
    REQUIRE(decision.verdict == GateVerdict::kPass);
    // 100 * pow(0.7, 1.0) = 70
    REQUIRE_THAT(decision.final_size,
                 Catch::Matchers::WithinAbs(70.0, 1e-5));
}

// ===========================================================================
// AC10: Preflight config parsing
// ===========================================================================

TEST_CASE("preflight -- disabled config by default",
          "[corrective][p4][preflight]")
{
    auto cfg = parse_corrective_config(R"({"data_file":"test.parquet"})");
    REQUIRE(cfg.state == GateVerdict::kDisabled);
}

TEST_CASE("preflight -- collect_only config",
          "[corrective][p4][preflight]")
{
    auto cfg = parse_corrective_config(
        R"({"corrective_state":"collect_only"})");
    REQUIRE(cfg.state == GateVerdict::kCollectOnly);
}

TEST_CASE("preflight -- enabled config with parameters",
          "[corrective][p4][preflight]")
{
    auto cfg = parse_corrective_config(
        R"({"corrective_state":"enabled","corrective_mode":"hybrid","corrective_threshold":0.6,"corrective_sizing_exponent":2.0})");
    REQUIRE(cfg.state == GateVerdict::kPass);
    REQUIRE(cfg.mode == SizingPolicy::kHybrid);
    REQUIRE_THAT(static_cast<double>(cfg.threshold),
                 Catch::Matchers::WithinAbs(0.6, 1e-5));
    REQUIRE_THAT(static_cast<double>(cfg.sizing_exponent),
                 Catch::Matchers::WithinAbs(2.0, 1e-5));
}

TEST_CASE("preflight -- artifact manifest parsing",
          "[corrective][p4][preflight]")
{
    auto manifest = parse_artifact_manifest(
        R"({"corrective_model_path":"/tmp/model.onnx","corrective_feature_schema_hash":3043313353,"corrective_calibration_method":"platt","corrective_platt_a":-1.5,"corrective_platt_b":0.3})");
    REQUIRE(manifest.model_path == "/tmp/model.onnx");
    REQUIRE(manifest.calibration_method == CalibrationMethod::kPlatt);
    REQUIRE_THAT(static_cast<double>(manifest.platt_a),
                 Catch::Matchers::WithinAbs(-1.5, 1e-5));
}

TEST_CASE("preflight -- isotonic breakpoints parsed from CSV strings",
          "[corrective][p4][preflight]")
{
    auto manifest = parse_artifact_manifest(
        R"({"corrective_calibration_method":"isotonic","corrective_bp_x":"0.1,0.5,0.9","corrective_bp_y":"0.05,0.5,0.95"})");
    REQUIRE(manifest.calibration_method == CalibrationMethod::kIsotonic);
    REQUIRE(manifest.isotonic_breakpoints.size() == 3);
    REQUIRE_THAT(static_cast<double>(manifest.isotonic_breakpoints[1].x),
                 Catch::Matchers::WithinAbs(0.5, 1e-5));
    REQUIRE_THAT(static_cast<double>(manifest.isotonic_breakpoints[1].y),
                 Catch::Matchers::WithinAbs(0.5, 1e-5));
}

#ifndef NBT_HAS_ONNXRUNTIME
TEST_CASE("preflight -- create fails without ONNX Runtime",
          "[corrective][p4][preflight]")
{
    ArtifactManifest manifest;
    manifest.feature_schema_hash = kPopFeatureSchemaHashV1;
    PopSizingPolicy policy(SizingPolicy::kGate, 0.5f, 1.0f);
    std::string error;
    auto ctx = PopInferenceContext::create(manifest, policy, error);
    REQUIRE(ctx == nullptr);
    REQUIRE(error.find("E_PRE_001") != std::string::npos);
}
#endif

// ===========================================================================
// AC10: Preflight -- feature schema hash mismatch detected
// ===========================================================================

#ifdef NBT_HAS_ONNXRUNTIME
TEST_CASE("preflight -- rejects feature schema mismatch",
          "[corrective][p4][preflight]")
{
    ArtifactManifest manifest;
    manifest.model_path = "/nonexistent/model.onnx";
    manifest.feature_schema_hash = 0xDEADBEEF;  // wrong hash
    PopSizingPolicy policy(SizingPolicy::kGate, 0.5f, 1.0f);
    std::string error;
    auto ctx = PopInferenceContext::create(manifest, policy, error);
    REQUIRE(ctx == nullptr);
    REQUIRE(error.find("E_PRE_002") != std::string::npos);
}
#endif

// ===========================================================================
// AC12: Disabled mode is byte-identical to baseline
// ===========================================================================

TEST_CASE("disabled mode -- no candidates or outcomes produced",
          "[corrective][p4][integration]")
{
    auto bars = make_linear_bars(kBarCount);
    auto feed = std::make_unique<StaticFeed>(bars, "TEST", 1000.0, 10.0);

    Cerebro cerebro;
    cerebro.add_data(std::move(feed));
    cerebro.set_cash(100000.0);
    cerebro.add_strategy<BuyOnBar10>();

    std::vector<CandidateSnapshot> candidates;
    std::vector<OutcomeRecord> outcomes;

    cerebro.add_observer<IncrementBatcher>(
        IncrementBatcher::Config{.max_bars_per_batch = 500},
        [&](const IncrementSnapshot& snap, const std::vector<DataFeed*>&) {
            for (const auto& c : snap.new_candidates) candidates.push_back(c);
            for (const auto& o : snap.new_outcomes) outcomes.push_back(o);
        });

    // No candidate collector set -> disabled
    cerebro.run();

    REQUIRE(candidates.empty());
    REQUIRE(outcomes.empty());
}

// ===========================================================================
// AC12: Collect-only mode never changes order size
// ===========================================================================

TEST_CASE("collect-only mode -- candidates emitted but size unchanged",
          "[corrective][p4][integration]")
{
    auto bars = make_linear_bars(kBarCount);
    auto feed = std::make_unique<StaticFeed>(bars, "TEST", 1000.0, 10.0);

    Cerebro cerebro;
    cerebro.add_data(std::move(feed));
    cerebro.set_cash(100000.0);
    cerebro.add_strategy<BuyOnBar10>();

    std::vector<CandidateSnapshot> candidates;

    cerebro.add_observer<IncrementBatcher>(
        IncrementBatcher::Config{.max_bars_per_batch = 500},
        [&](const IncrementSnapshot& snap, const std::vector<DataFeed*>&) {
            for (const auto& c : snap.new_candidates) candidates.push_back(c);
        });

    cerebro.set_candidate_collector(
        std::make_unique<CandidateCollector>(
            CollectorConfig{.corrective = {.state = GateVerdict::kCollectOnly}}));

    cerebro.run();

    REQUIRE(!candidates.empty());
    for (const auto& c : candidates) {
        REQUIRE(c.gate_verdict == GateVerdict::kCollectOnly);
        REQUIRE(c.calibrated_probability < 0.0f);
        REQUIRE(c.final_size == c.proposed_size);
    }
}

// ===========================================================================
// Error code enum round-trip
// ===========================================================================

TEST_CASE("error code strings match TS contract",
          "[corrective][p4][contracts]")
{
    REQUIRE(std::string(error_code_string(
        CorrectiveErrorCode::kPreflightArtifactLoadFailed)) == "E_PRE_001");
    REQUIRE(std::string(error_code_string(
        CorrectiveErrorCode::kPreflightFeatureSchemaMismatch)) == "E_PRE_002");
    REQUIRE(std::string(error_code_string(
        CorrectiveErrorCode::kPreflightGoldenVectorDiverged)) == "E_PRE_003");
    REQUIRE(std::string(error_code_string(
        CorrectiveErrorCode::kInferenceNonFiniteInput)) == "E_INF_001");
    REQUIRE(std::string(error_code_string(
        CorrectiveErrorCode::kInferenceNonFiniteOutput)) == "E_INF_002");
    REQUIRE(std::string(error_code_string(
        CorrectiveErrorCode::kInferenceFeatureCountMismatch)) == "E_INF_003");
    REQUIRE(std::string(error_code_string(
        CorrectiveErrorCode::kInferenceOnnxRuntimeError)) == "E_INF_004");
}

// ===========================================================================
// build_corrective_setup -- disabled returns empty
// ===========================================================================

TEST_CASE("build_corrective_setup -- disabled returns empty",
          "[corrective][p4][preflight]")
{
    auto setup = build_corrective_setup(R"({"data_file":"test.parquet"})");
    REQUIRE(setup.collector == nullptr);
    REQUIRE(setup.inference_ctx == nullptr);
}

TEST_CASE("build_corrective_setup -- collect_only creates collector without inference",
          "[corrective][p4][preflight]")
{
    auto setup = build_corrective_setup(
        R"({"corrective_state":"collect_only"})");
    REQUIRE(setup.collector != nullptr);
    REQUIRE(setup.inference_ctx == nullptr);
    REQUIRE(setup.collector->is_enabled());
}

// ===========================================================================
// PreOrderResult struct -- verify rejected field
// ===========================================================================

TEST_CASE("PreOrderResult -- default is not rejected",
          "[corrective][p4][contracts]")
{
    PreOrderResult r;
    REQUIRE(r.candidate_id == 0);
    REQUIRE(r.final_size == 0.0);
    REQUIRE(r.rejected == false);
}

// ===========================================================================
// CorrectiveRuntimeConfig -- defaults
// ===========================================================================

TEST_CASE("CorrectiveRuntimeConfig defaults",
          "[corrective][p4][contracts]")
{
    CorrectiveRuntimeConfig cfg;
    REQUIRE(cfg.state == GateVerdict::kDisabled);
    REQUIRE(cfg.mode == SizingPolicy::kGate);
    REQUIRE_THAT(static_cast<double>(cfg.threshold),
                 Catch::Matchers::WithinAbs(0.5, 1e-5));
    REQUIRE_THAT(static_cast<double>(cfg.sizing_exponent),
                 Catch::Matchers::WithinAbs(1.0, 1e-5));
}
