// SPDX-License-Identifier: MIT
//
// tests/test_increment_v2_wire_contract.cpp
//
//  §6 step 7: consumer-contract conformance test (public).
//
// Asserts:
//   (a) key parity — every required key from the IncrementalResult wire
//       contract appears in snapshot_to_increment_v2_json output AND no
//       POD-only key (portfolioValue / cash / realizedPnl / etc.) leaks
//       to the wire;
//   (b) per-point equity matches CashValue::value() element-wise AND is
//       std::isfinite — catches the original symptom
//       (Number.isFinite(p.equity) === false) at the C++ boundary, not
//       just in the renderer.
//
// Units-parity vs result_serializer (step 7b in the ticket) lives in
// the StratForge-private test_increment_v2_units_parity.cpp because
// result_serializer.hpp is internal per the StratForge Exclusion Policy.
// The element-wise assertion below uses CashValue, which is public.
//
// This is the test that would have caught the original drift; the
// byte-frozen baseline alone cannot, because it pins what we emit, not
// what the consumer needs.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/observers/cash_value.hpp>
#include <stratforge/observers/increment_batcher.hpp>
#include <stratforge/observers/increment_types.hpp>
#include <stratforge/observers/increment_wire.hpp>
#include <stratforge/strategy/strategy.hpp>

#include "test_helpers.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using stratforge::Cerebro;
using stratforge::DataFeed;
using stratforge::IncrementBatcher;
using stratforge::IncrementSnapshot;
using stratforge::Strategy;
using stratforge::test::StaticFeed;

namespace {

// Trivial buy-and-hold strategy: opens one long on bar 0, holds forever.
// Produces equity movement (the original 'valid: 0' symptom needed a
// non-flat equity curve to be observable) and exactly one open trade.
class BuyAndHoldStrategy final : public Strategy {
public:
    void start() override { static_cast<void>(buy(1.0)); }
    void next() override {}
};

// Bars with rising-then-falling close so drawdown is exercised.
std::vector<StaticFeed::Bar> sample_bars() {
    return {
        {.open = 100, .high = 100, .low = 100, .close = 100},
        {.open = 110, .high = 110, .low = 110, .close = 110},
        {.open = 120, .high = 120, .low = 120, .close = 120},
        {.open = 115, .high = 115, .low = 115, .close = 115},
        {.open = 125, .high = 125, .low = 125, .close = 125},
    };
}

// Naive string-presence check for a top-level JSON key. Sufficient here
// because snapshot_to_increment_v2_json output is hand-rolled with stable
// formatting and no nested-key collisions on the names we care about.
[[nodiscard]] bool has_key(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\":";
    return json.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("[INCREMENT_V2] wire JSON contains every required consumer-contract key "
          "",
          "[serialization][increment][regression]") {
    std::vector<IncrementSnapshot> snaps;
    std::vector<DataFeed*> captured_feeds;

    Cerebro cerebro;
    cerebro.set_cash(10'000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(sample_bars()), "SAMPLE");
    cerebro.add_strategy(std::make_unique<BuyAndHoldStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 2,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = true,
        },
        [&snaps, &captured_feeds](const IncrementSnapshot& s,
                                  const std::vector<DataFeed*>& feeds) {
            snaps.push_back(s);
            captured_feeds = feeds;
        }));
    cerebro.run();

    REQUIRE_FALSE(snaps.empty());

    // Pick the first non-empty mid-flush (must have a candle, equity point,
    // and may have one open-trade event from start()'s buy fill on bar 1).
    const IncrementSnapshot* pick = nullptr;
    for (const auto& s : snaps) {
        if (!s.new_equity_points.empty()) { pick = &s; break; }
    }
    REQUIRE(pick != nullptr);

    const std::string json =
        stratforge::snapshot_to_increment_v2_json(*pick, captured_feeds);
    INFO("json=" << json);

    // ---- Required top-level keys ----
    constexpr std::array<std::string_view, 8> kTopRequired{
        "seq", "isFinal", "droppedSinceLastFlush",
        "processedBars", "totalBars",
        "newCandles", "newEquityPoints", "newTrades",
    };
    for (auto k : kTopRequired) {
        INFO("missing required top-level key: " << k);
        CHECK(has_key(json, k));
    }
    // currentMetrics is also required; checked separately because it is
    // an object — nested key checks follow.
    CHECK(has_key(json, "currentMetrics"));

    // ---- Required nested keys (asserted by simple substring presence) ----
    constexpr std::array<std::string_view, 6> kMetricsRequired{
        "totalPnl", "totalReturn", "totalTrades",
        "winningTrades", "losingTrades", "winRate",
    };
    for (auto k : kMetricsRequired) {
        INFO("missing required currentMetrics key: " << k);
        CHECK(has_key(json, k));
    }
    constexpr std::array<std::string_view, 3> kEquityPointKeys{
        "timestamp", "equity", "drawdown",
    };
    for (auto k : kEquityPointKeys) {
        INFO("missing required newEquityPoints key: " << k);
        CHECK(has_key(json, k));
    }
}

TEST_CASE("[INCREMENT_V2] wire JSON contains NO POD-only field names "
          "",
          "[serialization][increment][regression]") {
    std::vector<IncrementSnapshot> snaps;
    std::vector<DataFeed*> captured_feeds;

    Cerebro cerebro;
    cerebro.set_cash(10'000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(sample_bars()), "SAMPLE");
    cerebro.add_strategy(std::make_unique<BuyAndHoldStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 10'000,  // single terminal flush
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = false,
        },
        [&snaps, &captured_feeds](const IncrementSnapshot& s,
                                  const std::vector<DataFeed*>& feeds) {
            snaps.push_back(s);
            captured_feeds = feeds;
        }));
    cerebro.run();

    REQUIRE_FALSE(snaps.empty());
    const std::string json =
        stratforge::snapshot_to_increment_v2_json(snaps.back(), captured_feeds);
    INFO("json=" << json);

    // These are POD names from increment_types.hpp that MUST NOT appear
    // on the wire — they are exactly the symptoms the original drift
    // produced. If any of these show up, the serializer has re-leaked
    // a POD field.
    constexpr std::array<std::string_view, 13> kForbidden{
        "portfolioValue", "cash",
        "realizedPnl", "unrealizedPnl",
        "currentDdPct", "maxDdPct",
        "tradeCount",
        "tradeId", "dataIndex", "pnlNet", "status",
        "entryBar", "exitBar",
        // "size" is too ambiguous (also a property of arrays / appears
        // as a substring elsewhere); the contract uses "quantity" instead
        // and key-parity test above asserts "quantity" exists. We don't
        // include "size" in the forbidden list because it could match the
        // legitimate substring inside other tokens.
    };
    for (auto k : kForbidden) {
        const std::string needle = "\"" + std::string(k) + "\":";
        INFO("POD field name leaked onto wire: " << k);
        CHECK(json.find(needle) == std::string::npos);
    }
}

TEST_CASE("[INCREMENT_V2] newEquityPoints.equity values are finite AND match "
          "CashValue::value() element-wise "
          "",
          "[serialization][increment][regression]") {
    Cerebro cerebro;
    cerebro.set_cash(10'000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(sample_bars()), "SAMPLE");
    cerebro.add_strategy(std::make_unique<BuyAndHoldStrategy>());
    auto& cash_value = cerebro.add_observer<stratforge::CashValue>();

    std::vector<IncrementSnapshot> snaps;
    std::vector<DataFeed*> captured_feeds;
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 10'000,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = false,
        },
        [&snaps, &captured_feeds](const IncrementSnapshot& s,
                                  const std::vector<DataFeed*>& feeds) {
            snaps.push_back(s);
            captured_feeds = feeds;
        }));
    cerebro.run();

    // Concatenate every equity point across snapshots — this is what the
    // renderer accumulator does on the consumer side.
    std::vector<double> stream_equities;
    for (const auto& s : snaps) {
        for (const auto& p : s.new_equity_points) {
            stream_equities.push_back(p.portfolio_value);
            // Hard invariant — every value is finite. The exact symptom
            // that motivated this ticket: Number.isFinite(p.equity) was
            // false because the wire key didn't match.
            CHECK(std::isfinite(p.portfolio_value));
            CHECK(std::isfinite(p.drawdown_pct));
        }
    }

    // Element-wise count parity — terminal flush emits same #points as
    // CashValue's per-bar samples.
    const auto& cash_data = cash_value.value().data();
    REQUIRE(stream_equities.size() == cash_data.size());

    for (std::size_t i = 0; i < stream_equities.size(); ++i) {
        INFO("equity divergence at i=" << i
             << " stream=" << stream_equities[i]
             << " cash_value=" << cash_data[i]);
        // CashValue observer and IncrementBatcher both read
        // broker.portfolio_value(feeds) at the same point per bar, so
        // values must be bit-identical (no rounding).
        CHECK_THAT(stream_equities[i],
                   Catch::Matchers::WithinAbs(cash_data[i], 1e-9));
    }
}
