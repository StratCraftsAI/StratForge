#include <catch2/catch_test_macros.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/observer_strategy.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

/// Concrete observer: records whether close > threshold each bar.
class TestObserver final : public ObserverStrategy {
public:
    bool init_called = false;
    std::vector<bool> observations;

    void initialize_indicators() override {
        init_called = true;
    }

    [[nodiscard]] bool check_precondition() override {
        bool result = data().close()[0] > 101.0;
        observations.push_back(result);
        return result;
    }
};

} // namespace

TEST_CASE("ObserverStrategy lifecycle wiring", "[strategy][observer]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestObserver>();
    cerebro.run();

    SECTION("initialize_indicators called via init()") {
        REQUIRE(strategy.init_called);
    }

    SECTION("check_precondition called each bar") {
        REQUIRE(strategy.observations.size() == 3);
    }

    SECTION("precondition results are correct") {
        REQUIRE(strategy.observations[0] == false);  // 100.5 <= 101
        REQUIRE(strategy.observations[1] == true);    // 101.5 > 101
        REQUIRE(strategy.observations[2] == true);    // 102.5 > 101
    }
}

TEST_CASE("ObserverStrategy does not submit orders", "[strategy][observer]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestObserver>();
    cerebro.run();

    // Observer never opens positions
    REQUIRE(strategy.position().size == 0.0);
}
