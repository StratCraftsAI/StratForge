// : StratForge generation conformance for the reviewed Larry Williams
// (Williams %R) rules shape.
//
// Scope (per ticket §4.1 / §6.2): this repository owns integration verification
// only. The upstream generation-service security-sentinel separation is a
// backend correction and is NOT implemented here. This suite proves that the
// sanitized reviewed rules shape:
//   - compiles against the current StratForge public API,
//   - satisfies the ABI v2 factory contract (QNX_STRATEGY_FACTORY_EXPORT),
//   - and produces an admissible artifact that runs through the supported
//     runner path (Cerebro) without weakening any existing gate.
//
// This file adds NO C++ parser for backend security sentinels — the incident
// occurs before any C++ artifact exists (ticket §4.1).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>

// The ABI v2 factory contract lives in the runner integration SDK
// (qnx_strategy_sdk), not the public StratForge SDK surface (ticket §4.3). It is
// present in the upstream tree but intentionally absent from the public
// StratForge sync. Guard the ABI-conformance portion on header availability so
// the public build compiles only the Cerebro runner path — same rationale that
// excludes test_strategy_runtime.cpp from the public sync.
#if __has_include(<qnx_strategy_sdk/qnx_strategy_sdk.hpp>)
#  include <qnx_strategy_sdk/qnx_strategy_sdk.hpp>
#  include <qnx_strategy_sdk/version.hpp>
#  define SF_TICKET791_HAS_QNX_ABI 1
#else
#  define SF_TICKET791_HAS_QNX_ABI 0
#endif

#include "fixtures/williams_r_strategy.hpp"
#include "test_helpers.hpp"

#include <cmath>
#include <vector>

using namespace stratforge;
using StaticFeed = stratforge::test::StaticFeed;
using stratforge::fixtures::WilliamsRStrategy;

namespace {

// Deterministic oscillating OHLC data that drives Williams %R through both the
// oversold and overbought zones so entry and exit rules are exercised.
std::vector<StaticFeed::Bar> oscillating_bars(std::size_t count) {
    std::vector<StaticFeed::Bar> bars;
    bars.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double base = 100.0 + 20.0 * std::sin(static_cast<double>(i) * 0.35);
        bars.push_back(StaticFeed::Bar{
            .open = base,
            .high = base + 1.5,
            .low = base - 1.5,
            .close = base + 0.5,
        });
    }
    return bars;
}

} // namespace

TEST_CASE(": reviewed Williams %R rules shape runs through the runner path",
          "[strategy][integration][regression][ticket791]") {
    Cerebro cerebro;
    cerebro.set_cash(100000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(oscillating_bars(120)), "WPR");

    auto& strategy = cerebro.add_strategy<WilliamsRStrategy>();

    SECTION("AC1: admitted rules shape produces a running backtest") {
        REQUIRE_NOTHROW(cerebro.run());
    }

    SECTION("AC7: artifact executes and exercises entry/exit logic") {
        cerebro.run();
        // The oscillating feed crosses the oversold/overbought bands, so the
        // reviewed rules must produce at least one round trip.
        INFO("entries=" << strategy.entries() << " exits=" << strategy.exits());
        REQUIRE(strategy.entries() >= 1);
        REQUIRE(strategy.exits() >= 1);

        const double final_value =
            cerebro.broker().portfolio_value(strategy.data_feeds());
        REQUIRE(std::isfinite(final_value));
        REQUIRE(final_value > 0.0);
    }
}

#if SF_TICKET791_HAS_QNX_ABI
// The generated artifact must satisfy the ABI v2 factory contract. Emitting the
// exports here is a compile-smoke proof that the fixture is a valid ABI v2
// strategy type; the runtime symbols themselves are covered by the [live]
// dlopen path (test_strategy_runtime.cpp). This block compiles only where the
// runner integration SDK is available (upstream tree only).
QNX_STRATEGY_FACTORY_EXPORT(stratforge::fixtures::WilliamsRStrategy)

TEST_CASE(": fixture satisfies the ABI v2 factory contract",
          "[strategy][integration][ticket791]") {
    SECTION("AC7: exported ABI version matches the SDK contract") {
        REQUIRE(qnx_strategy_abi_version() == QNX_STRATEGY_ABI_VERSION);
        REQUIRE(QNX_STRATEGY_ABI_VERSION == 2);
    }

    SECTION("AC7: factory create/destroy round-trips a live Strategy*") {
        stratforge::Strategy* raw = stratforge_create_strategy();
        REQUIRE(raw != nullptr);
        REQUIRE(dynamic_cast<WilliamsRStrategy*>(raw) != nullptr);
        stratforge_destroy_strategy(raw);
    }
}
#endif // SF_TICKET791_HAS_QNX_ABI
