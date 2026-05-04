#include <catch2/catch_test_macros.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/regime_detector_strategy.hpp>

#include "test_helpers.hpp"

#include <cstddef>
#include <memory>
#include <vector>

using namespace stratforge;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

/// Concrete test strategy: detects trend when close > 102, range when close < 101,
/// volatility when high - low > 3.
class TestRegimeDetector final : public RegimeDetectorStrategy {
public:
    bool init_called = false;
    int next_count = 0;

    void initialize_indicators() override {
        init_called = true;
    }

    [[nodiscard]] std::size_t get_base_warmup_period() const override {
        return 2;
    }

    [[nodiscard]] double calculate_trend_strength() override {
        ++next_count;
        return data().close()[0] > 102.0 ? 0.8 : 0.0;
    }

    [[nodiscard]] double calculate_range_strength() override {
        return data().close()[0] < 101.0 ? 0.7 : 0.0;
    }

    [[nodiscard]] bool get_volatility_state() override {
        return (data().high()[0] - data().low()[0]) > 3.0;
    }
};

/// Strategy that tracks update_indicators() call ordering.
class TestRegimeDetectorWithIndicators final : public RegimeDetectorStrategy {
public:
    int update_count = 0;
    int trend_checks = 0;
    bool update_before_trend = true;

    void initialize_indicators() override {}

    [[nodiscard]] std::size_t get_base_warmup_period() const override {
        return 1;
    }

    void update_indicators() override {
        ++update_count;
    }

    [[nodiscard]] double calculate_trend_strength() override {
        ++trend_checks;
        if (update_count < trend_checks) {
            update_before_trend = false;
        }
        return 0.0;
    }
};

/// Minimal strategy that only implements required pure virtuals.
class MinimalRegimeDetector final : public RegimeDetectorStrategy {
public:
    void initialize_indicators() override {}

    [[nodiscard]] std::size_t get_base_warmup_period() const override {
        return 1;
    }
};

} // namespace

TEST_CASE("RegimeDetectorStrategy lifecycle wiring", "[strategy][regime_detector]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
        {103.0, 104.0, 102.0, 103.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestRegimeDetector>();
    cerebro.run();

    SECTION("initialize_indicators called via init()") {
        REQUIRE(strategy.init_called);
    }

    SECTION("warmup period respected") {
        // 4 bars, warmup=2: prenext on bar 0, nextstart on bar 1 (calls next()),
        // next on bars 2 and 3. detect_trend called 3 times total.
        REQUIRE(strategy.next_count == 3);
    }

    SECTION("minimum period set from get_base_warmup_period") {
        REQUIRE(strategy.minimum_period() == 2);
    }
}

TEST_CASE("RegimeDetectorStrategy regime state transitions", "[strategy][regime_detector]") {
    SECTION("trending state when close > 102") {
        Cerebro cerebro;
        cerebro.set_cash(10000.0);
        cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
            {100.0, 101.0, 99.0, 100.0},
            {103.0, 104.0, 102.0, 103.0},
        }), "main");

        auto& strategy = cerebro.add_strategy<TestRegimeDetector>();
        cerebro.run();

        REQUIRE(strategy.current_state() == RegimeState::Trending);
    }

    SECTION("range-bound state when close < 101") {
        Cerebro cerebro;
        cerebro.set_cash(10000.0);
        cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
            {100.0, 101.0, 99.0, 100.0},
            {100.0, 101.0, 99.0, 100.5},
        }), "main");

        auto& strategy = cerebro.add_strategy<TestRegimeDetector>();
        cerebro.run();

        REQUIRE(strategy.current_state() == RegimeState::RangeBound);
    }

    SECTION("high volatility takes priority over trend") {
        Cerebro cerebro;
        cerebro.set_cash(10000.0);
        cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
            {100.0, 101.0, 99.0, 100.0},
            {103.0, 107.0, 102.0, 103.0},  // high-low=5 > 3, also close>102
        }), "main");

        auto& strategy = cerebro.add_strategy<TestRegimeDetector>();
        cerebro.run();

        REQUIRE(strategy.current_state() == RegimeState::HighVolatility);
    }

    SECTION("low volatility fallback when nothing matches") {
        Cerebro cerebro;
        cerebro.set_cash(10000.0);
        cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
            {100.0, 101.0, 99.0, 100.0},
            {101.0, 102.0, 100.0, 101.5},  // close 101.5 not >102, not <101, spread=2 <3
        }), "main");

        auto& strategy = cerebro.add_strategy<TestRegimeDetector>();
        cerebro.run();

        REQUIRE(strategy.current_state() == RegimeState::LowVolatility);
    }
}

TEST_CASE("RegimeDetectorStrategy optional defaults return false", "[strategy][regime_detector]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<MinimalRegimeDetector>();
    cerebro.run();

    // All optional detectors return false -> LowVolatility
    REQUIRE(strategy.current_state() == RegimeState::LowVolatility);
}

TEST_CASE("RegimeDetectorStrategy update_indicators called before business logic", "[strategy][regime_detector]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestRegimeDetectorWithIndicators>();
    cerebro.run();

    SECTION("update_indicators called each bar after warmup") {
        // 3 bars, warmup=1: nextstart on bar 0 + next on bars 1,2 = 3 calls
        REQUIRE(strategy.update_count == 3);
    }

    SECTION("update_indicators called before calculate_trend_strength") {
        REQUIRE(strategy.update_before_trend);
    }
}
