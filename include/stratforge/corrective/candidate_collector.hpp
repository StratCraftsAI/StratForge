// SPDX-License-Identifier: MIT
//
//  P1+P4: CandidateCollector -- central orchestrator for the
// corrective layer lifecycle data. Captures CandidateSnapshots before
// order submission, links them to OutcomeRecords, manages shadow
// evaluation, and buffers results for the IncrementBatcher pipeline.
//
// P4: When an inference context is attached (mode == kEnabled), runs
// ONNX inference -> calibration -> sizing policy on each candidate
// and may reject (gate) or resize orders. Rejected candidates are
// routed to the ShadowLedger for counterfactual outcome tracking.

#pragma once

#include <stratforge/broker/broker.hpp>
#include <stratforge/corrective/corrective_contracts.hpp>
#include <stratforge/corrective/pop_feature_builder.hpp>
#include <stratforge/corrective/shadow_ledger.hpp>
#include <stratforge/data/data_feed.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace stratforge::corrective {

// Forward-declare inference context (defined in pop_inference_engine.hpp)
class PopInferenceContext;

struct CollectorConfig {
    CorrectiveRuntimeConfig corrective;
    std::uint32_t shadow_horizon_bars = 50;
};

class CandidateCollector {
public:
    explicit CandidateCollector(CollectorConfig config) noexcept
        : config_(config) {}

    void init(const std::vector<DataFeed*>& feeds) {
        feature_builder_.init(feeds);
    }

    void set_inference_context(PopInferenceContext* ctx) noexcept {
        inference_ctx_ = ctx;
    }

    [[nodiscard]] PreOrderResult on_pre_order(
        OrderSide side,
        double proposed_size,
        std::size_t data_index,
        const std::vector<DataFeed*>& feeds,
        const BackBroker& broker)
    {
        if (config_.corrective.state == GateVerdict::kDisabled) {
            return PreOrderResult{0, proposed_size, false};
        }

        // Skip position-reducing/closing orders
        const auto& pos = broker.position(data_index);
        if ((side == OrderSide::Sell && pos.is_long()) ||
            (side == OrderSide::Buy  && pos.is_short())) {
            return PreOrderResult{0, proposed_size, false};
        }

        const std::uint64_t cand_id = next_candidate_id_++;

        CandidateSnapshot snap{};
        snap.schema_version             = kCorrectiveSchemaVersion;
        snap.candidate_id               = cand_id;
        snap.symbol_id                  = static_cast<std::uint32_t>(data_index);

        if (data_index < feeds.size() && feeds[data_index] != nullptr) {
            const auto& dt = feeds[data_index]->datetime()[0];
            snap.as_of_timestamp_ns         = to_ns_(dt);
            snap.knowledge_cutoff_timestamp_ns = to_ns_(dt);
        }

        snap.side = (side == OrderSide::Buy)
                  ? CandidateSide::kLong : CandidateSide::kShort;
        snap.proposed_size              = proposed_size;
        snap.feature_vector             = feature_builder_.build(
            side, proposed_size, data_index, feeds, broker);
        snap.feature_schema_hash        = kPopFeatureSchemaHashV1;

        // P4: apply inference + policy when enabled and context available
        if (config_.corrective.state == GateVerdict::kCollectOnly ||
            inference_ctx_ == nullptr) {
            snap.gate_verdict           = GateVerdict::kCollectOnly;
            snap.calibrated_probability = -1.0f;
            snap.final_size             = proposed_size;
            snap.sizing_policy          = config_.corrective.mode;

            pending_candidates_.push_back(snap);
            candidate_registry_[cand_id] = snap;
            return PreOrderResult{cand_id, proposed_size, false};
        }

        // kEnabled path: inference -> calibrate -> policy
        auto [probability, verdict, final_size] = run_inference_policy_(
            snap.feature_vector, proposed_size);

        snap.calibrated_probability = probability;
        snap.gate_verdict           = verdict;
        snap.final_size             = final_size;
        snap.sizing_policy          = config_.corrective.mode;

        pending_candidates_.push_back(snap);
        candidate_registry_[cand_id] = snap;

        if (verdict == GateVerdict::kReject) {
            // Route to shadow ledger for counterfactual tracking
            if (data_index < feeds.size() && feeds[data_index] != nullptr) {
                const double signed_size =
                    (side == OrderSide::Buy) ? proposed_size : -proposed_size;
                shadow_ledger_.add_shadow(ShadowEntry{
                    .candidate_id = cand_id,
                    .data_index = data_index,
                    .entry_price = feeds[data_index]->close()[0],
                    .size = signed_size,
                    .entry_timestamp_ns = snap.as_of_timestamp_ns,
                    .horizon_bars = config_.shadow_horizon_bars,
                });
            }
            return PreOrderResult{cand_id, 0.0, true};
        }

        return PreOrderResult{cand_id, final_size, false};
    }

    void on_trade_closed(const Trade& trade, double /*orig_size*/) {
        if (config_.corrective.state == GateVerdict::kDisabled) return;
        if (trade.candidate_id == 0) return;

        OutcomeRecord r{};
        r.schema_version       = kCorrectiveSchemaVersion;
        r.candidate_id         = trade.candidate_id;
        r.outcome_type         = OutcomeType::kActual;
        r.entry_timestamp_ns   = to_ns_(trade.entry_time);
        r.exit_timestamp_ns    = to_ns_(trade.exit_time);
        r.holding_interval_bars = static_cast<std::uint32_t>(
            trade.exit_bar >= trade.entry_bar
                ? trade.exit_bar - trade.entry_bar : 0);
        r.gross_pnl            = trade.pnl;
        r.commission           = trade.commission;
        r.slippage             = 0.0;
        r.net_pnl              = trade.pnlcomm;
        r.completion_status    = CompletionStatus::kComplete;
        r.label_policy_version = 1;
        r.profit_label         = (trade.pnlcomm > 0.0) ? 1 : 0;

        pending_outcomes_.push_back(r);
        feature_builder_.record_trade();
    }

    void on_bar(
        std::size_t bar_index,
        const std::vector<DataFeed*>& feeds,
        const BackBroker& broker)
    {
        if (config_.corrective.state == GateVerdict::kDisabled) return;

        feature_builder_.advance();
        feature_builder_.update_equity_peak(broker.portfolio_value(feeds));

        auto shadow_outcomes = shadow_ledger_.tick(bar_index, feeds);
        for (auto& o : shadow_outcomes) {
            pending_outcomes_.push_back(std::move(o));
        }
    }

    void on_stop(
        const std::vector<DataFeed*>& feeds,
        const BackBroker& broker)
    {
        if (config_.corrective.state == GateVerdict::kDisabled) return;

        // Censored outcomes for open actual trades
        for (const auto& [trade, orig_size] : broker.open_trades()) {
            if (!trade.is_open()) continue;
            if (trade.candidate_id == 0) continue;

            const double close_price =
                (trade.data_index < feeds.size() && feeds[trade.data_index])
                    ? feeds[trade.data_index]->close()[0] : 0.0;
            const double gross = (close_price - trade.entry_price) * trade.size;

            OutcomeRecord r{};
            r.schema_version       = kCorrectiveSchemaVersion;
            r.candidate_id         = trade.candidate_id;
            r.outcome_type         = OutcomeType::kCensored;
            r.entry_timestamp_ns   = to_ns_(trade.entry_time);
            if (trade.data_index < feeds.size() && feeds[trade.data_index]) {
                r.exit_timestamp_ns = to_ns_(feeds[trade.data_index]->datetime()[0]);
            }
            r.holding_interval_bars = static_cast<std::uint32_t>(
                trade.barlen);
            r.gross_pnl            = gross;
            r.commission           = trade.commission;
            r.slippage             = 0.0;
            r.net_pnl              = gross - trade.commission;
            r.completion_status    = CompletionStatus::kCensored;
            r.label_policy_version = 1;
            r.profit_label         = -1;

            pending_outcomes_.push_back(r);
        }

        // Censored outcomes for open shadow entries
        const std::size_t last_bar = 0;
        auto shadow_censored = shadow_ledger_.flush_censored(last_bar, feeds);
        for (auto& o : shadow_censored) {
            pending_outcomes_.push_back(std::move(o));
        }
    }

    [[nodiscard]] std::vector<CandidateSnapshot> drain_pending_candidates() {
        return std::exchange(pending_candidates_, {});
    }

    [[nodiscard]] std::vector<OutcomeRecord> drain_pending_outcomes() {
        return std::exchange(pending_outcomes_, {});
    }

    [[nodiscard]] bool is_enabled() const noexcept {
        return config_.corrective.state != GateVerdict::kDisabled;
    }

    [[nodiscard]] const CollectorConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] std::uint64_t candidate_count() const noexcept {
        return next_candidate_id_ - 1;
    }

    [[nodiscard]] PopFeatureBuilder& feature_builder() noexcept {
        return feature_builder_;
    }

    [[nodiscard]] ShadowLedger& shadow_ledger() noexcept {
        return shadow_ledger_;
    }

private:
    struct InferencePolicyResult {
        float       probability;
        GateVerdict verdict;
        double      final_size;
    };

    // Defined after PopInferenceContext is complete (below)
    inline InferencePolicyResult run_inference_policy_(
        const std::array<float, kPopFeatureCountV1>& features,
        double proposed_size) const;

    [[nodiscard]] static std::uint64_t to_ns_(DateTime dt) noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                dt.time_since_epoch()).count());
    }

    CollectorConfig  config_;
    std::uint64_t    next_candidate_id_ = 1;
    PopFeatureBuilder feature_builder_;
    ShadowLedger     shadow_ledger_;
    PopInferenceContext* inference_ctx_ = nullptr;

    std::vector<CandidateSnapshot> pending_candidates_;
    std::vector<OutcomeRecord>     pending_outcomes_;
    std::unordered_map<std::uint64_t, CandidateSnapshot> candidate_registry_;
};

// -----------------------------------------------------------------------
// Deferred implementation -- requires PopInferenceContext to be complete
// -----------------------------------------------------------------------

} // namespace stratforge::corrective

#include <stratforge/corrective/pop_inference_engine.hpp>

namespace stratforge::corrective {

inline CandidateCollector::InferencePolicyResult
CandidateCollector::run_inference_policy_(
    const std::array<float, kPopFeatureCountV1>& features,
    double proposed_size) const
{
    if (inference_ctx_ == nullptr) {
        return {-1.0f, GateVerdict::kCollectOnly, proposed_size};
    }
    const auto result = inference_ctx_->infer(features, proposed_size);
    return {result.calibrated_probability, result.verdict, result.final_size};
}

} // namespace stratforge::corrective
