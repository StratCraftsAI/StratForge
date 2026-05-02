#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/ai_libero_strategy.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

/// Concrete strategy: collects indicator values, no trading logic.
class TestAILibero final : public AILiberoStrategy {
public:
    bool init_called = false;
    std::vector<IndicatorValues> collected_values;

    void initialize_indicators() override {
        init_called = true;
    }

    [[nodiscard]] IndicatorValues get_indicator_values() override {
        IndicatorValues values;
        values["close"] = data().close()[0];
        values["sma_proxy"] = (data().close()[0] + data().open()[0]) / 2.0;
        collected_values.push_back(values);
        return values;
    }
};

} // namespace

TEST_CASE("AILiberoStrategy lifecycle wiring", "[strategy][ai_libero]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestAILibero>();
    cerebro.run();

    SECTION("initialize_indicators called via init()") {
        REQUIRE(strategy.init_called);
    }

    SECTION("get_indicator_values called each bar") {
        REQUIRE(strategy.collected_values.size() == 3);
    }

    SECTION("indicator values are accurate") {
        REQUIRE_THAT(strategy.collected_values[0].at("close"), WithinRel(100.5, 1e-12));
        REQUIRE_THAT(strategy.collected_values[0].at("sma_proxy"), WithinRel(100.25, 1e-12));
    }
}

TEST_CASE("AILiberoStrategy does not submit orders", "[strategy][ai_libero]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestAILibero>();
    cerebro.run();

    // No position opened — AILibero only collects indicator values
    REQUIRE(strategy.position().size == 0.0);
}
