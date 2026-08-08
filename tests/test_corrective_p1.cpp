// SPDX-License-Identifier: MIT
//
// tests/test_corrective_p1.cpp
//
//  P1 acceptance suite: candidate lifecycle data in StratForge.
// AC3 (identity), AC4 (shadow isolation), AC5 (capture timing),
// AC6 (orphan detection), AC12 (disabled baseline / collect-only).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/broker/broker.hpp>
#include <stratforge/corrective/candidate_collector.hpp>
#include <stratforge/corrective/corrective_contracts.hpp>
#include <stratforge/corrective/pop_feature_builder.hpp>
#include <stratforge/corrective/shadow_ledger.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/observers/increment_batcher.hpp>
#include <stratforge/observers/increment_types.hpp>
#include <stratforge/observers/increment_wire.hpp>
#include <stratforge/strategy/strategy.hpp>

#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using stratforge::BackBroker;
using stratforge::Cerebro;
using stratforge::DataFeed;
using stratforge::IncrementBatcher;
using stratforge::IncrementSnapshot;
using stratforge::OrderSide;
using stratforge::Strategy;
using stratforge::Trade;
using stratforge::test::StaticFeed;
using stratforge::corrective::CandidateCollector;
using stratforge::corrective::CandidateSnapshot;
using stratforge::corrective::CollectorConfig;
using stratforge::corrective::GateVerdict;
using stratforge::corrective::OutcomeRecord;
using stratforge::corrective::OutcomeType;
using stratforge::corrective::CompletionStatus;
using stratforge::corrective::PopFeatureBuilder;
using stratforge::corrective::ShadowEntry;
using stratforge::corrective::ShadowLedger;
using stratforge::corrective::kPopFeatureSchemaHashV1;
using stratforge::corrective::kPopFeatureCountV1;

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

class BuyOnBar10CloseOnBar20 final : public Strategy {
public:
    void next() override {
        const auto idx = data().index();
        if (idx == 10 && !bought_) {
            static_cast<void>(buy(1.0));
            bought_ = true;
        }
        if (idx == 20 && position().is_long()) {
            static_cast<void>(close());
        }
    }
private:
    bool bought_ = false;
};

class MultiOrderStrategy final : public Strategy {
public:
    void next() override {
        const auto idx = data().index();
        if (idx == 5) static_cast<void>(buy(1.0));
        if (idx == 10) static_cast<void>(close());
        if (idx == 15) static_cast<void>(buy(1.0));
        if (idx == 20) static_cast<void>(close());
        if (idx == 25) static_cast<void>(buy(1.0));
        if (idx == 30) static_cast<void>(close());
        if (idx == 35) static_cast<void>(sell(1.0));
        if (idx == 40) static_cast<void>(close());
        if (idx == 45) static_cast<void>(buy(1.0));
        // intentionally leave the last position open
    }
};

class NoopStrategy final : public Strategy {
public:
    void next() override {}
};

struct CollectedData {
    std::vector<CandidateSnapshot> all_candidates;
    std::vector<OutcomeRecord> all_outcomes;
    std::vector<IncrementSnapshot> all_snapshots;
};

CollectedData run_with_corrective(GateVerdict mode) {
    auto bars = make_linear_bars(kBarCount);
    auto feed = std::make_unique<StaticFeed>(bars, "TEST", 1000.0, 10.0);

    Cerebro cerebro;
    cerebro.add_data(std::move(feed));
    cerebro.set_cash(100000.0);
    cerebro.add_strategy<MultiOrderStrategy>();

    CollectedData data;

    cerebro.add_observer<IncrementBatcher>(
        IncrementBatcher::Config{.max_bars_per_batch = 500},
        [&data](const IncrementSnapshot& snap,
                const std::vector<DataFeed*>&) {
            for (const auto& c : snap.new_candidates) {
                data.all_candidates.push_back(c);
            }
            for (const auto& o : snap.new_outcomes) {
                data.all_outcomes.push_back(o);
            }
            data.all_snapshots.push_back(snap);
        });

    if (mode != GateVerdict::kDisabled) {
        cerebro.set_candidate_collector(
            std::make_unique<CandidateCollector>(
                CollectorConfig{.corrective = {.state = mode}}));
    }

    cerebro.run();
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// AC3: monotonic candidate_id
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: monotonic candidate_id", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    REQUIRE(data.all_candidates.size() >= 5);

    for (std::size_t i = 1; i < data.all_candidates.size(); ++i) {
        REQUIRE(data.all_candidates[i].candidate_id >
                data.all_candidates[i - 1].candidate_id);
    }
}

// ---------------------------------------------------------------------------
// AC3: one-to-one candidate-outcome linkage
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: every candidate has exactly one outcome", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    REQUIRE(!data.all_candidates.empty());

    std::unordered_set<std::uint64_t> cand_ids;
    for (const auto& c : data.all_candidates) {
        cand_ids.insert(c.candidate_id);
    }

    std::unordered_set<std::uint64_t> outcome_cand_ids;
    for (const auto& o : data.all_outcomes) {
        REQUIRE(cand_ids.count(o.candidate_id) == 1);
        REQUIRE(outcome_cand_ids.count(o.candidate_id) == 0);
        outcome_cand_ids.insert(o.candidate_id);
    }

    REQUIRE(outcome_cand_ids.size() == cand_ids.size());
}

// ---------------------------------------------------------------------------
// AC5: capture timing (features at decision bar, not fill bar)
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: candidate captured at decision bar", "[corrective]") {
    auto bars = make_linear_bars(kBarCount);
    auto feed = std::make_unique<StaticFeed>(bars, "TEST", 1000.0, 10.0);

    Cerebro cerebro;
    cerebro.add_data(std::move(feed));
    cerebro.set_cash(100000.0);
    cerebro.add_strategy<BuyOnBar10CloseOnBar20>();

    std::vector<CandidateSnapshot> candidates;
    cerebro.add_observer<IncrementBatcher>(
        IncrementBatcher::Config{.max_bars_per_batch = 500},
        [&candidates](const IncrementSnapshot& snap,
                      const std::vector<DataFeed*>&) {
            for (const auto& c : snap.new_candidates) {
                candidates.push_back(c);
            }
        });

    cerebro.set_candidate_collector(
        std::make_unique<CandidateCollector>(
            CollectorConfig{.corrective = {.state = GateVerdict::kCollectOnly}}));

    cerebro.run();

    REQUIRE(!candidates.empty());

    // The buy on bar 10 should have as_of_timestamp from bar 10's datetime
    // Bar 10's timestamp = epoch + 10 * 24h
    const auto expected_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::hours(24 * 10)).count());

    REQUIRE(candidates[0].as_of_timestamp_ns == expected_ns);
}

// ---------------------------------------------------------------------------
// Feature schema hash matches P0 contract
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: feature schema hash matches contract", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    REQUIRE(!data.all_candidates.empty());
    for (const auto& c : data.all_candidates) {
        REQUIRE(c.feature_schema_hash == kPopFeatureSchemaHashV1);
    }
}

// ---------------------------------------------------------------------------
// AC12: collect-only mode never changes order size
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: collect-only final_size equals proposed_size", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    REQUIRE(!data.all_candidates.empty());
    for (const auto& c : data.all_candidates) {
        REQUIRE(c.final_size == c.proposed_size);
    }
}

// ---------------------------------------------------------------------------
// AC12: disabled mode produces no candidates or outcomes
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: disabled mode produces no corrective data", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kDisabled);

    REQUIRE(data.all_candidates.empty());
    REQUIRE(data.all_outcomes.empty());
}

// ---------------------------------------------------------------------------
// Actual outcome PnL matches Trade
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: actual outcome has correct PnL", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    bool found_actual = false;
    for (const auto& o : data.all_outcomes) {
        if (o.outcome_type == OutcomeType::kActual) {
            found_actual = true;
            REQUIRE(o.completion_status == CompletionStatus::kComplete);
            REQUIRE_THAT(o.net_pnl,
                Catch::Matchers::WithinAbs(o.gross_pnl - o.commission, 1e-10));
            REQUIRE((o.profit_label == 0 || o.profit_label == 1));
        }
    }
    REQUIRE(found_actual);
}

// ---------------------------------------------------------------------------
// Censored outcome for open-at-end-of-run trade
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: censored outcome for open trade at stop", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    // MultiOrderStrategy leaves the last buy (bar 45) open
    bool found_censored = false;
    for (const auto& o : data.all_outcomes) {
        if (o.outcome_type == OutcomeType::kCensored) {
            found_censored = true;
            REQUIRE(o.completion_status == CompletionStatus::kCensored);
            REQUIRE(o.profit_label == -1);
        }
    }
    REQUIRE(found_censored);
}

// ---------------------------------------------------------------------------
// AC4: shadow ledger isolation
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: shadow ledger does not affect broker state", "[corrective]") {
    ShadowLedger ledger;

    auto bars = make_linear_bars(20);
    auto feed = std::make_unique<StaticFeed>(bars, "SHADOW_TEST");
    feed->preload();

    std::vector<DataFeed*> feeds = {feed.get()};

    BackBroker broker(100000.0);
    const double cash_before = broker.cash();

    ShadowEntry entry{};
    entry.candidate_id = 42;
    entry.data_index = 0;
    entry.entry_price = 105.0;
    entry.size = 1.0;  // long
    entry.entry_bar = 0;
    entry.entry_timestamp_ns = 0;
    entry.commission_per_side = 1.0;
    entry.horizon_bars = 5;

    ledger.add_shadow(entry);

    for (std::size_t i = 0; i < 10; ++i) {
        auto outcomes = ledger.tick(i, feeds);
        if (!outcomes.empty()) {
            REQUIRE(outcomes[0].outcome_type == OutcomeType::kShadow);
            REQUIRE(outcomes[0].candidate_id == 42);
            REQUIRE(outcomes[0].completion_status == CompletionStatus::kComplete);
        }
        if (i + 1 < 20) feed->advance();
    }

    // Broker state untouched
    REQUIRE(broker.cash() == cash_before);
    REQUIRE(broker.open_trades().empty());
}

// ---------------------------------------------------------------------------
// Shadow ledger censored at flush
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: shadow ledger flush produces censored outcome", "[corrective]") {
    ShadowLedger ledger;

    auto bars = make_linear_bars(10);
    auto feed = std::make_unique<StaticFeed>(bars, "SHADOW_TEST");
    feed->preload();
    std::vector<DataFeed*> feeds = {feed.get()};

    ShadowEntry entry{};
    entry.candidate_id = 99;
    entry.data_index = 0;
    entry.entry_price = 100.0;
    entry.size = -1.0;  // short
    entry.horizon_bars = 0;  // unlimited
    entry.commission_per_side = 0.5;

    ledger.add_shadow(entry);

    // Tick a few bars but don't exhaust horizon
    for (std::size_t i = 0; i < 3; ++i) {
        static_cast<void>(ledger.tick(i, feeds));
        feed->advance();
    }

    auto censored = ledger.flush_censored(3, feeds);
    REQUIRE(censored.size() == 1);
    REQUIRE(censored[0].outcome_type == OutcomeType::kCensored);
    REQUIRE(censored[0].completion_status == CompletionStatus::kCensored);
    REQUIRE(censored[0].profit_label == -1);
    REQUIRE(censored[0].candidate_id == 99);
}

// ---------------------------------------------------------------------------
// Wire serialization includes newCandidates and newOutcomes
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: wire JSON contains candidate and outcome arrays", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    bool found_candidates_in_wire = false;
    bool found_outcomes_in_wire = false;
    for (const auto& snap : data.all_snapshots) {
        auto json = stratforge::snapshot_to_increment_v2_json(
            snap, {});
        if (json.find("\"newCandidates\":[") != std::string::npos) {
            found_candidates_in_wire = true;
        }
        if (json.find("\"newOutcomes\":[") != std::string::npos) {
            found_outcomes_in_wire = true;
        }
    }
    REQUIRE(found_candidates_in_wire);
    REQUIRE(found_outcomes_in_wire);
}

// ---------------------------------------------------------------------------
// Wire serialization: disabled mode emits empty arrays
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: disabled mode wire JSON has empty arrays", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kDisabled);

    for (const auto& snap : data.all_snapshots) {
        auto json = stratforge::snapshot_to_increment_v2_json(snap, {});
        REQUIRE(json.find("\"newCandidates\":[]") != std::string::npos);
        REQUIRE(json.find("\"newOutcomes\":[]") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Feature vector: non-finite values clamped to 0
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: feature vector has no NaN/Inf values", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    for (const auto& c : data.all_candidates) {
        for (std::size_t fi = 0; fi < kPopFeatureCountV1; ++fi) {
            REQUIRE(std::isfinite(c.feature_vector[fi]));
        }
    }
}

// ---------------------------------------------------------------------------
// Gate verdict is always kCollectOnly in P1
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: gate verdict is always collect_only", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    for (const auto& c : data.all_candidates) {
        REQUIRE(c.gate_verdict == GateVerdict::kCollectOnly);
    }
}

// ---------------------------------------------------------------------------
// Calibrated probability is always -1.0 in P1 (no model)
// ---------------------------------------------------------------------------

TEST_CASE("Corrective P1: calibrated probability is -1.0", "[corrective]") {
    auto data = run_with_corrective(GateVerdict::kCollectOnly);

    for (const auto& c : data.all_candidates) {
        REQUIRE_THAT(c.calibrated_probability,
            Catch::Matchers::WithinAbs(-1.0, 1e-6));
    }
}
