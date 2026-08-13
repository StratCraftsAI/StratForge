// SPDX-License-Identifier: MIT
//
//  P4: In-process ONNX inference engine + calibration + policy.
// PopInferenceContext is the single object that CandidateCollector calls
// for the enabled path. No Python, no IPC, no network (AC5).
//
// Compile guard: the ONNX Runtime dependency is optional (feature flag
// in vcpkg.json / CMake). When SF_HAS_ONNXRUNTIME is not defined,
// PopInferenceContext::create() always fails with a clear error.

#pragma once

#include <stratforge/corrective/corrective_contracts.hpp>
#include <stratforge/corrective/pop_calibrator.hpp>
#include <stratforge/corrective/pop_sizing_policy.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef SF_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace stratforge::corrective {

struct InferenceResult {
    float       raw_probability       = 0.0f;
    float       calibrated_probability = 0.0f;
    GateVerdict verdict               = GateVerdict::kPass;
    double      final_size            = 0.0;
};

struct ArtifactManifest {
    std::string  model_path;
    std::string  content_hash;
    std::uint32_t schema_version       = 1;
    std::uint32_t feature_schema_hash  = 0;
    std::size_t   feature_count        = kPopFeatureCountV1;

    CalibrationMethod calibration_method = CalibrationMethod::kIsotonic;
    std::vector<CalibrationBreakpoint> isotonic_breakpoints;
    float platt_a = 0.0f;
    float platt_b = 0.0f;

    GoldenVector golden_vector;
};

class PopInferenceContext {
public:
    ~PopInferenceContext() = default;

    PopInferenceContext(const PopInferenceContext&) = delete;
    PopInferenceContext& operator=(const PopInferenceContext&) = delete;
    PopInferenceContext(PopInferenceContext&&) = default;
    PopInferenceContext& operator=(PopInferenceContext&&) = default;

    /// Create and preflight an inference context from an artifact manifest.
    /// Returns nullptr + sets error_out on any failure.
    [[nodiscard]] static std::unique_ptr<PopInferenceContext> create(
        const ArtifactManifest& manifest,
        const PopSizingPolicy& policy,
        std::string& error_out);

    /// Run inference + calibration + policy on a feature vector.
    [[nodiscard]] InferenceResult infer(
        const std::array<float, kPopFeatureCountV1>& features,
        double proposed_size) const;

    [[nodiscard]] const PopCalibrator& calibrator() const noexcept {
        return calibrator_;
    }

    [[nodiscard]] const PopSizingPolicy& policy() const noexcept {
        return policy_;
    }

private:
    PopInferenceContext() = default;

    [[nodiscard]] float run_model_(
        const std::array<float, kPopFeatureCountV1>& features) const;

    PopCalibrator    calibrator_;
    PopSizingPolicy  policy_;

#ifdef SF_HAS_ONNXRUNTIME
    Ort::Env                         ort_env_{ORT_LOGGING_LEVEL_WARNING, "corrective"};
    std::unique_ptr<Ort::Session>    ort_session_;
    Ort::MemoryInfo                  memory_info_ = Ort::MemoryInfo::CreateCpu(
        OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
    std::vector<const char*>         input_names_;
    std::vector<const char*>         output_names_;
    std::vector<std::string>         input_name_strs_;
    std::vector<std::string>         output_name_strs_;
#endif
};

// -----------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------

#ifdef SF_HAS_ONNXRUNTIME

inline std::unique_ptr<PopInferenceContext> PopInferenceContext::create(
    const ArtifactManifest& manifest,
    const PopSizingPolicy& policy,
    std::string& error_out)
{
    auto ctx = std::unique_ptr<PopInferenceContext>(new PopInferenceContext());
    ctx->policy_ = policy;

    // Build calibrator from manifest
    if (manifest.calibration_method == CalibrationMethod::kIsotonic) {
        ctx->calibrator_ = PopCalibrator::from_isotonic(manifest.isotonic_breakpoints);
    } else {
        ctx->calibrator_ = PopCalibrator::from_platt(manifest.platt_a, manifest.platt_b);
    }

    // Validate feature schema
    if (manifest.feature_schema_hash != kPopFeatureSchemaHashV1) {
        error_out = std::string(error_code_string(
            CorrectiveErrorCode::kPreflightFeatureSchemaMismatch)) +
            ": artifact feature_schema_hash mismatch";
        return nullptr;
    }

    if (manifest.feature_count != kPopFeatureCountV1) {
        error_out = std::string(error_code_string(
            CorrectiveErrorCode::kPreflightFeatureSchemaMismatch)) +
            ": feature count mismatch";
        return nullptr;
    }

    // Load ONNX model
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        ctx->ort_session_ = std::make_unique<Ort::Session>(
            ctx->ort_env_, manifest.model_path.c_str(), options);

        // Cache input/output names
        Ort::AllocatorWithDefaultOptions allocator;
        const std::size_t num_inputs = ctx->ort_session_->GetInputCount();
        for (std::size_t i = 0; i < num_inputs; ++i) {
            auto name = ctx->ort_session_->GetInputNameAllocated(i, allocator);
            ctx->input_name_strs_.push_back(name.get());
        }
        const std::size_t num_outputs = ctx->ort_session_->GetOutputCount();
        for (std::size_t i = 0; i < num_outputs; ++i) {
            auto name = ctx->ort_session_->GetOutputNameAllocated(i, allocator);
            ctx->output_name_strs_.push_back(name.get());
        }
        for (auto& s : ctx->input_name_strs_) ctx->input_names_.push_back(s.c_str());
        for (auto& s : ctx->output_name_strs_) ctx->output_names_.push_back(s.c_str());

    } catch (const Ort::Exception& e) {
        error_out = std::string(error_code_string(
            CorrectiveErrorCode::kPreflightArtifactLoadFailed)) +
            ": " + e.what();
        return nullptr;
    }

    // Golden vector preflight test
    const auto& gv = manifest.golden_vector;
    const float raw = ctx->run_model_(gv.input_features);
    const float calibrated = ctx->calibrator_.calibrate(raw);
    const float diff = std::abs(calibrated - gv.expected_probability);
    if (diff > kGoldenVectorTolerance) {
        error_out = std::string(error_code_string(
            CorrectiveErrorCode::kPreflightGoldenVectorDiverged)) +
            ": expected " + std::to_string(gv.expected_probability) +
            " got " + std::to_string(calibrated) +
            " (diff=" + std::to_string(diff) + ")";
        return nullptr;
    }

    error_out.clear();
    return ctx;
}

inline float PopInferenceContext::run_model_(
    const std::array<float, kPopFeatureCountV1>& features) const
{
    const std::array<int64_t, 2> shape = {1, static_cast<int64_t>(kPopFeatureCountV1)};
    auto input_tensor = Ort::Value::CreateTensor<float>(
        memory_info_,
        const_cast<float*>(features.data()),
        features.size(),
        shape.data(),
        shape.size());

    auto outputs = ort_session_->Run(
        Ort::RunOptions{nullptr},
        input_names_.data(), &input_tensor, 1,
        output_names_.data(), output_names_.size());

    // XGBoost ONNX exports class probabilities as shape [1, 2];
    // index 1 is the positive class probability.
    const float* output_data = outputs[outputs.size() > 1 ? 1 : 0]
        .GetTensorData<float>();
    const auto output_shape = outputs[outputs.size() > 1 ? 1 : 0]
        .GetTensorTypeAndShapeInfo().GetShape();
    if (output_shape.size() == 2 && output_shape[1] == 2) {
        return output_data[1];
    }
    return output_data[0];
}

inline InferenceResult PopInferenceContext::infer(
    const std::array<float, kPopFeatureCountV1>& features,
    double proposed_size) const
{
    // Validate inputs are finite
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (!std::isfinite(features[i])) {
            return InferenceResult{
                0.0f, 0.0f, GateVerdict::kReject, 0.0};
        }
    }

    const float raw = run_model_(features);
    if (!std::isfinite(raw)) {
        return InferenceResult{
            raw, 0.0f, GateVerdict::kReject, 0.0};
    }

    const float calibrated = calibrator_.calibrate(raw);
    const auto decision = policy_.apply(calibrated, proposed_size);

    return InferenceResult{
        raw, calibrated, decision.verdict, decision.final_size};
}

#else // !SF_HAS_ONNXRUNTIME

inline std::unique_ptr<PopInferenceContext> PopInferenceContext::create(
    const ArtifactManifest& /*manifest*/,
    const PopSizingPolicy& /*policy*/,
    std::string& error_out)
{
    error_out = "E_PRE_001: ONNX Runtime not available in this build";
    return nullptr;
}

inline float PopInferenceContext::run_model_(
    const std::array<float, kPopFeatureCountV1>& /*features*/) const
{
    return 0.0f;
}

inline InferenceResult PopInferenceContext::infer(
    const std::array<float, kPopFeatureCountV1>& /*features*/,
    double proposed_size) const
{
    return InferenceResult{0.0f, 0.0f, GateVerdict::kPass, proposed_size};
}

#endif // SF_HAS_ONNXRUNTIME

} // namespace stratforge::corrective
