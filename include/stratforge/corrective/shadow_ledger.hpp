// SPDX-License-Identifier: MIT
//
//  P1: Deterministic shadow outcome evaluator.
//
// Tracks rejected candidates in isolation -- no connection to real
// broker cash, positions, or risk limits (AC4). Reuses authoritative
// fill/cost semantics (D3) via a simple round-trip model.

#pragma once

#include <stratforge/corrective/corrective_contracts.hpp>
#include <stratforge/data/data_feed.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace stratforge::corrective {

struct ShadowEntry {
    std::uint64_t candidate_id       = 0;
    std::size_t   data_index         = 0;
    double        entry_price        = 0.0;
    double        size               = 0.0;  // signed: >0 long, <0 short
    std::size_t   entry_bar          = 0;
    std::uint64_t entry_timestamp_ns = 0;
    double        commission_per_side = 0.0;
    std::uint32_t horizon_bars       = 0;    // max bars to track; 0 = unlimited
    std::size_t   bars_held          = 0;
};

class ShadowLedger {
public:
    void add_shadow(ShadowEntry entry) {
        open_shadows_.push_back(entry);
    }

    [[nodiscard]] std::vector<OutcomeRecord> tick(
        std::size_t bar_index,
        const std::vector<DataFeed*>& feeds)
    {
        std::vector<OutcomeRecord> results;
        std::vector<ShadowEntry> still_open;
        still_open.reserve(open_shadows_.size());

        for (auto& s : open_shadows_) {
            ++s.bars_held;
            if (s.horizon_bars > 0 && s.bars_held >= s.horizon_bars) {
                results.push_back(close_shadow_(s, bar_index, feeds));
            } else {
                still_open.push_back(s);
            }
        }
        open_shadows_ = std::move(still_open);
        return results;
    }

    [[nodiscard]] std::vector<OutcomeRecord> flush_censored(
        std::size_t bar_index,
        const std::vector<DataFeed*>& feeds)
    {
        std::vector<OutcomeRecord> results;
        results.reserve(open_shadows_.size());
        for (auto& s : open_shadows_) {
            results.push_back(censor_shadow_(s, bar_index, feeds));
        }
        open_shadows_.clear();
        return results;
    }

    [[nodiscard]] std::size_t open_count() const noexcept {
        return open_shadows_.size();
    }

private:
    [[nodiscard]] static double exit_price_at_(
        const ShadowEntry& s,
        const std::vector<DataFeed*>& feeds)
    {
        if (s.data_index < feeds.size() && feeds[s.data_index] != nullptr) {
            return feeds[s.data_index]->close()[0];
        }
        return s.entry_price;
    }

    [[nodiscard]] static std::uint64_t bar_timestamp_ns_(
        const ShadowEntry& s,
        const std::vector<DataFeed*>& feeds)
    {
        if (s.data_index < feeds.size() && feeds[s.data_index] != nullptr) {
            const auto tp = feeds[s.data_index]->datetime()[0];
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    tp.time_since_epoch()).count());
        }
        return 0;
    }

    [[nodiscard]] OutcomeRecord close_shadow_(
        const ShadowEntry& s,
        std::size_t bar_index,
        const std::vector<DataFeed*>& feeds) const
    {
        const double exit = exit_price_at_(s, feeds);
        const double gross = (exit - s.entry_price) * s.size;
        const double comm = s.commission_per_side * 2.0;

        OutcomeRecord r{};
        r.schema_version       = kCorrectiveSchemaVersion;
        r.candidate_id         = s.candidate_id;
        r.outcome_type         = OutcomeType::kShadow;
        r.entry_timestamp_ns   = s.entry_timestamp_ns;
        r.exit_timestamp_ns    = bar_timestamp_ns_(s, feeds);
        r.holding_interval_bars = static_cast<std::uint32_t>(s.bars_held);
        r.gross_pnl            = gross;
        r.commission           = comm;
        r.slippage             = 0.0;
        r.net_pnl              = gross - comm;
        r.completion_status    = CompletionStatus::kComplete;
        r.label_policy_version = 1;
        r.profit_label         = (r.net_pnl > 0.0) ? 1 : 0;
        return r;
    }

    [[nodiscard]] OutcomeRecord censor_shadow_(
        const ShadowEntry& s,
        std::size_t bar_index,
        const std::vector<DataFeed*>& feeds) const
    {
        const double exit = exit_price_at_(s, feeds);
        const double gross = (exit - s.entry_price) * s.size;
        const double comm = s.commission_per_side * 2.0;

        OutcomeRecord r{};
        r.schema_version       = kCorrectiveSchemaVersion;
        r.candidate_id         = s.candidate_id;
        r.outcome_type         = OutcomeType::kCensored;
        r.entry_timestamp_ns   = s.entry_timestamp_ns;
        r.exit_timestamp_ns    = bar_timestamp_ns_(s, feeds);
        r.holding_interval_bars = static_cast<std::uint32_t>(s.bars_held);
        r.gross_pnl            = gross;
        r.commission           = comm;
        r.slippage             = 0.0;
        r.net_pnl              = gross - comm;
        r.completion_status    = CompletionStatus::kCensored;
        r.label_policy_version = 1;
        r.profit_label         = -1;  // unknown (censored)
        return r;
    }

    std::vector<ShadowEntry> open_shadows_;
};

} // namespace stratforge::corrective
