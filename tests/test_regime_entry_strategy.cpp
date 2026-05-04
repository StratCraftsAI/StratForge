#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/regime_entry_strategy.hpp>

#include "test_helpers.hpp"

#include <cstddef>
#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

/// Concrete strategy: buys when close > 102, closes when close < 101.
class TestRegimeEntry final : public RegimeEntryStrategy {
public:
    bool init_called = false;
    int open_checks = 0;
    int close_checks = 0;

    void initialize_indicators() override {
        init_called = true;
    }

    [[nodiscard]] std::size_t get_base_warmup_period() const override {
        return 2;
    }

    [[nodiscard]] EntrySignal check_open_conditions() override {
        ++open_checks;
        return {.long_signal = data().close()[0] > 102.0};
    }

    [[nodiscard]] bool check_close_conditions() override {
        ++close_checks;
        return data().close()[0] < 101.0;
    }
};

/// Strategy that tracks update_indicators() call ordering.
class TestRegimeEntryWithIndicators final : public RegimeEntryStrategy {
public:
    int update_count = 0;
    int open_checks = 0;
    bool update_before_open = true;

    void initialize_indicators() override {}

    [[nodiscard]] std::size_t get_base_warmup_period() const override {
        return 1;
    }

    void update_indicators() override {
        ++update_count;
    }

    [[nodiscard]] EntrySignal check_open_conditions() override {
        ++open_checks;
        // Verify update_indicators was called at least as many times as open checks
        if (update_count < open_checks) {
            update_before_open = false;
        }
        return {};
    }

    [[nodiscard]] bool check_close_conditions() override {
        return false;
    }
};

} // namespace

TEST_CASE("RegimeEntryStrategy lifecycle wiring", "[strategy][regime_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
        {103.0, 104.0, 102.0, 103.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestRegimeEntry>();
    cerebro.run();

    SECTION("initialize_indicators called via init()") {
        REQUIRE(strategy.init_called);
    }

    SECTION("warmup period set from get_base_warmup_period") {
        REQUIRE(strategy.minimum_period() == 2);
    }

    SECTION("open and close conditions checked after warmup") {
        // 4 bars, warmup=2: nextstart on bar 1 + next on bars 2,3 = 3 calls for open.
        // close_checks only called when position is held (bar 3 after buy fills).
        REQUIRE(strategy.open_checks == 3);
        REQUIRE(strategy.close_checks == 1);
    }
}

TEST_CASE("RegimeEntryStrategy buy/sell lifecycle", "[strategy][regime_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},   // bar 0: warmup
        {101.0, 102.0, 100.0, 101.0},   // bar 1: nextstart, no signal
        {102.0, 104.0, 101.0, 103.0},   // bar 2: close>102 -> buy signal (fills bar 3)
        {103.0, 105.0, 102.0, 104.0},   // bar 3: buy fills, holding
        {99.0, 100.0, 98.0, 99.0},      // bar 4: close<101 -> close signal (fills bar 5)
        {98.0, 99.0, 97.0, 98.0},       // bar 5: close fills
    }), "main");

    auto& strategy = cerebro.add_strategy<TestRegimeEntry>();
    cerebro.run();

    // After run, position should be closed
    REQUIRE(strategy.position().size == 0.0);
}

TEST_CASE("RegimeEntryStrategy does not open when flat and no signal", "[strategy][regime_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {101.0, 102.0, 100.0, 101.5},  // close 101.5 not > 102
    }), "main");

    auto& strategy = cerebro.add_strategy<TestRegimeEntry>();
    cerebro.run();

    REQUIRE(strategy.position().size == 0.0);
}

TEST_CASE("RegimeEntryStrategy update_indicators called before business logic", "[strategy][regime_entry]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestRegimeEntryWithIndicators>();
    cerebro.run();

    SECTION("update_indicators called each bar after warmup") {
        // 3 bars, warmup=1: nextstart on bar 0 + next on bars 1,2 = 3 calls
        REQUIRE(strategy.update_count == 3);
    }

    SECTION("update_indicators called before check_open_conditions") {
        REQUIRE(strategy.update_before_open);
    }
}
