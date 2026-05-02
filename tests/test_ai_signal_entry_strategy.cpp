#include <catch2/catch_test_macros.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/ai_signal_entry_strategy.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

/// Concrete strategy: collects close price as indicator value,
/// buys when close > 102, closes when close < 100.
class TestAISignalEntry final : public AISignalEntryStrategy {
public:
    bool init_called = false;
    std::vector<IndicatorValues> collected_values;

    void initialize_indicators() override {
        init_called = true;
    }

    [[nodiscard]] IndicatorValues get_indicator_values() override {
        IndicatorValues values;
        values["close"] = data().close()[0];
        values["volume"] = data().volume()[0];
        collected_values.push_back(values);
        return values;
    }

    [[nodiscard]] EntrySignal check_open_conditions() override {
        return {.long_signal = data().close()[0] > 102.0};
    }

    [[nodiscard]] bool check_close_conditions() override {
        return data().close()[0] < 100.0;
    }
};

} // namespace

TEST_CASE("AISignalEntryStrategy lifecycle wiring", "[strategy][ai_signal_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestAISignalEntry>();
    cerebro.run();

    SECTION("initialize_indicators called via init()") {
        REQUIRE(strategy.init_called);
    }

    SECTION("get_indicator_values called each bar") {
        REQUIRE(strategy.collected_values.size() == 3);
    }

    SECTION("indicator values contain expected keys") {
        REQUIRE(strategy.collected_values[0].count("close") == 1);
        REQUIRE(strategy.collected_values[0].count("volume") == 1);
    }
}

TEST_CASE("AISignalEntryStrategy indicator values plus open/close signals", "[strategy][ai_signal_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},   // bar 0: no signal
        {102.0, 104.0, 101.0, 103.0},   // bar 1: close>102 -> buy (fills bar 2)
        {103.0, 105.0, 102.0, 104.0},   // bar 2: buy fills, holding
        {98.0, 99.0, 97.0, 98.0},       // bar 3: close<100 -> close (fills bar 4)
        {97.0, 98.0, 96.0, 97.0},       // bar 4: close fills
    }), "main");

    auto& strategy = cerebro.add_strategy<TestAISignalEntry>();
    cerebro.run();

    // Indicator values collected for every bar
    REQUIRE(strategy.collected_values.size() == 5);
    // Position closed at end
    REQUIRE(strategy.position().size == 0.0);
}
