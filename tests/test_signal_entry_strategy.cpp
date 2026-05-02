#include <catch2/catch_test_macros.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/signal_entry_strategy.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

/// Concrete strategy: buys when close > 102, closes when close drops below 100.
class TestSignalEntry final : public SignalEntryStrategy {
public:
    bool init_called = false;
    int open_checks = 0;
    int close_checks = 0;

    void initialize_indicators() override {
        init_called = true;
    }

    [[nodiscard]] EntrySignal check_open_conditions() override {
        ++open_checks;
        return {.long_signal = data().close()[0] > 102.0};
    }

    [[nodiscard]] bool check_close_conditions() override {
        ++close_checks;
        return data().close()[0] < 100.0;
    }
};

/// Short-selling strategy: sells when close < 99, covers when close > 102.
class TestShortSignalEntry final : public SignalEntryStrategy {
public:
    void initialize_indicators() override {}

    [[nodiscard]] EntrySignal check_open_conditions() override {
        return {.short_signal = data().close()[0] < 99.0};
    }

    [[nodiscard]] bool check_close_conditions() override {
        return data().close()[0] > 102.0;
    }
};

} // namespace

TEST_CASE("SignalEntryStrategy lifecycle wiring", "[strategy][signal_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestSignalEntry>();
    cerebro.run();

    SECTION("initialize_indicators called via init()") {
        REQUIRE(strategy.init_called);
    }

    SECTION("no explicit warmup period override") {
        REQUIRE(strategy.minimum_period() == 1);
    }

    SECTION("open and close conditions checked each bar") {
        // open_checks called every bar; close_checks only when position is held.
        // No position is held in this 3-bar dataset (buy signal on bar 2 has no fill bar).
        REQUIRE(strategy.open_checks == 3);
        REQUIRE(strategy.close_checks == 0);
    }
}

TEST_CASE("SignalEntryStrategy buy then close lifecycle", "[strategy][signal_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},   // bar 0: no signal
        {102.0, 104.0, 101.0, 103.0},   // bar 1: close>102 -> buy signal (fills bar 2)
        {103.0, 105.0, 102.0, 104.0},   // bar 2: buy fills, holding
        {98.0, 99.0, 97.0, 98.0},       // bar 3: close<100 -> close signal (fills bar 4)
        {97.0, 98.0, 96.0, 97.0},       // bar 4: close fills
    }), "main");

    auto& strategy = cerebro.add_strategy<TestSignalEntry>();
    cerebro.run();

    REQUIRE(strategy.position().size == 0.0);
}

TEST_CASE("SignalEntryStrategy short sell lifecycle", "[strategy][signal_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},   // bar 0: no signal
        {98.0, 99.0, 97.0, 98.0},       // bar 1: close<99 -> short signal (fills bar 2)
        {97.0, 98.0, 96.0, 97.0},       // bar 2: short fills, holding
        {103.0, 104.0, 102.0, 103.0},   // bar 3: close>102 -> cover signal (fills bar 4)
        {104.0, 105.0, 103.0, 104.0},   // bar 4: cover fills
    }), "main");

    auto& strategy = cerebro.add_strategy<TestShortSignalEntry>();
    cerebro.run();

    REQUIRE(strategy.position().size == 0.0);
}

TEST_CASE("SignalEntryStrategy no signal means no position", "[strategy][signal_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestSignalEntry>();
    cerebro.run();

    REQUIRE(strategy.position().size == 0.0);
}
