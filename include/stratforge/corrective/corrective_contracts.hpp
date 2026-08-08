// -----------------------------------------------------------------------
//  P0: Corrective Layer contracts -- C++ mirror
//
// This header is the C++ side of the versioned contract defined in
// packages/corrective-ai-core/src/contracts.ts. Both sides MUST stay
// aligned. Contract tests in the TS package verify field order, count,
// enum values, and schema hash against this header.
//
// POD and trivially-copyable constraints are enforced via static_assert
// so CandidateSnapshot and OutcomeRecord remain SPSC-safe.
// -----------------------------------------------------------------------
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace stratforge::corrective {

// -- Schema version ------------------------------------------------------

inline constexpr std::uint32_t kCorrectiveSchemaVersion = 1;

// -- Feature contract ----------------------------------------------------

inline constexpr std::uint32_t kPopFeatureSchemaVersion = 1;
inline constexpr std::size_t   kPopFeatureCountV1       = 14;

enum class PopFeatureIndex : std::uint8_t {
    kProposedSizeNormalized = 0,
    kEntryPrice             = 1,
    kBidAskSpreadBps        = 2,
    kAtrRatio               = 3,
    kVolatilityZ            = 4,
    kRsi14                  = 5,
    kBbPercentB             = 6,
    kMacdHistogram          = 7,
    kVolumeRatio            = 8,
    kUnrealizedPnlNorm      = 9,
    kOpenPositionCount      = 10,
    kBarsSinceLastTrade     = 11,
    kCurrentDrawdownPct     = 12,
    kSideEncoded            = 13,
};

// -- Enums ---------------------------------------------------------------

enum class CandidateSide : std::uint8_t {
    kLong  = 0,
    kShort = 1,
};

enum class GateVerdict : std::uint8_t {
    kPass        = 0,
    kReject      = 1,
    kCollectOnly = 2,
    kDisabled    = 3,
};

enum class OutcomeType : std::uint8_t {
    kActual   = 0,
    kShadow   = 1,
    kCensored = 2,
};

enum class CompletionStatus : std::uint8_t {
    kComplete = 0,
    kCensored = 1,
};

enum class SizingPolicy : std::uint8_t {
    kGate   = 0,
    kSizing = 1,
    kHybrid = 2,
};

// -- Candidate snapshot (D2) ---------------------------------------------
// POD: SPSC-safe, trivially copyable, no heap allocations.
// Strings are represented as fixed-length char arrays.

struct CandidateSnapshot {
    std::uint32_t schema_version     = kCorrectiveSchemaVersion;
    std::uint64_t candidate_id       = 0;
    std::uint64_t as_of_timestamp_ns = 0;
    std::uint64_t knowledge_cutoff_timestamp_ns = 0;
    std::uint32_t symbol_id          = 0;
    CandidateSide side               = CandidateSide::kLong;
    double        proposed_size      = 0.0;
    double        final_size         = 0.0;
    std::array<float, kPopFeatureCountV1> feature_vector{};
    std::uint32_t feature_schema_hash = 0;
    GateVerdict   gate_verdict       = GateVerdict::kDisabled;
    float         calibrated_probability = -1.0f;
    SizingPolicy  sizing_policy      = SizingPolicy::kGate;
};

static_assert(std::is_standard_layout_v<CandidateSnapshot>);
static_assert(std::is_trivially_copyable_v<CandidateSnapshot>);

// -- Outcome record (D3) -------------------------------------------------

struct OutcomeRecord {
    std::uint32_t     schema_version       = kCorrectiveSchemaVersion;
    std::uint64_t     candidate_id         = 0;
    OutcomeType       outcome_type         = OutcomeType::kActual;
    std::uint64_t     entry_timestamp_ns   = 0;
    std::uint64_t     exit_timestamp_ns    = 0;
    std::uint32_t     holding_interval_bars = 0;
    double            gross_pnl            = 0.0;
    double            commission           = 0.0;
    double            slippage             = 0.0;
    double            net_pnl              = 0.0;
    CompletionStatus  completion_status    = CompletionStatus::kComplete;
    std::uint32_t     label_policy_version = 1;
    std::int8_t       profit_label         = -1;
};

static_assert(std::is_standard_layout_v<OutcomeRecord>);
static_assert(std::is_trivially_copyable_v<OutcomeRecord>);

// -- Golden vector -------------------------------------------------------

inline constexpr double kGoldenVectorTolerance = 1e-6;

struct GoldenVector {
    std::array<float, kPopFeatureCountV1> input_features{};
    float expected_probability = 0.0f;
    GateVerdict expected_verdict = GateVerdict::kPass;
};

static_assert(std::is_standard_layout_v<GoldenVector>);
static_assert(std::is_trivially_copyable_v<GoldenVector>);

// -- Feature schema hash (FNV-1a, same as TS computeFeatureSchemaHash) ---

constexpr std::uint32_t compute_feature_schema_hash(
    const char* const* names, std::size_t count) noexcept
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            hash ^= static_cast<std::uint32_t>('\0');
            hash *= 0x01000193u;
        }
        for (const char* p = names[i]; *p != '\0'; ++p) {
            hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(*p));
            hash *= 0x01000193u;
        }
    }
    return hash;
}

inline constexpr const char* kPopFeatureNamesV1[kPopFeatureCountV1] = {
    "proposed_size_normalized",
    "entry_price",
    "bid_ask_spread_bps",
    "atr_ratio",
    "volatility_z",
    "rsi_14",
    "bb_percent_b",
    "macd_histogram",
    "volume_ratio",
    "unrealized_pnl_normalized",
    "open_position_count",
    "bars_since_last_trade",
    "current_drawdown_pct",
    "side_encoded",
};

inline constexpr std::uint32_t kPopFeatureSchemaHashV1 =
    compute_feature_schema_hash(kPopFeatureNamesV1, kPopFeatureCountV1);

// -- Error codes (mirror constants.ts CORRECTIVE_ERROR_CODES) ---------------

enum class CorrectiveErrorCode : std::uint8_t {
    kPreflightArtifactLoadFailed   = 1,
    kPreflightFeatureSchemaMismatch = 2,
    kPreflightGoldenVectorDiverged = 3,
    kPreflightModelVersionMismatch = 4,
    kInferenceNonFiniteInput       = 5,
    kInferenceNonFiniteOutput      = 6,
    kInferenceFeatureCountMismatch = 7,
    kInferenceOnnxRuntimeError     = 8,
};

[[nodiscard]] constexpr const char* error_code_string(
    CorrectiveErrorCode code) noexcept
{
    switch (code) {
        case CorrectiveErrorCode::kPreflightArtifactLoadFailed:    return "E_PRE_001";
        case CorrectiveErrorCode::kPreflightFeatureSchemaMismatch: return "E_PRE_002";
        case CorrectiveErrorCode::kPreflightGoldenVectorDiverged:  return "E_PRE_003";
        case CorrectiveErrorCode::kPreflightModelVersionMismatch:  return "E_PRE_004";
        case CorrectiveErrorCode::kInferenceNonFiniteInput:        return "E_INF_001";
        case CorrectiveErrorCode::kInferenceNonFiniteOutput:       return "E_INF_002";
        case CorrectiveErrorCode::kInferenceFeatureCountMismatch:  return "E_INF_003";
        case CorrectiveErrorCode::kInferenceOnnxRuntimeError:      return "E_INF_004";
    }
    return "E_UNKNOWN";
}

// -- Pre-order hook result --------------------------------------------------

struct PreOrderResult {
    std::uint64_t candidate_id = 0;
    double        final_size   = 0.0;
    bool          rejected     = false;
};

// -- Runtime corrective configuration ---------------------------------------

struct CorrectiveRuntimeConfig {
    GateVerdict   state             = GateVerdict::kDisabled;
    SizingPolicy  mode              = SizingPolicy::kGate;
    float         threshold         = 0.5f;
    float         sizing_exponent   = 1.0f;
    std::uint32_t shadow_horizon_bars = 50;
};

} // namespace stratforge::corrective
