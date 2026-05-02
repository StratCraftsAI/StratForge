#include <catch2/catch_test_macros.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/exit_strategy.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

/// Concrete exit strategy: signals exit when close drops below a threshold.
class TestExitStrategy final : public ExitStrategy {
public:
    bool init_called = false;
    int exit_checks = 0;

    void initialize_indicators() override {
        init_called = true;
    }

    [[nodiscard]] bool check_exit_signal() override {
        ++exit_checks;
        return data().close()[0] < 100.0;
    }
};

/// Helper: opens a long position on bar 0, then delegates to ExitStrategy.
/// We use two strategies on the same Cerebro: one to open, one to exit.
/// Instead, we use a single strategy that opens in start() and checks exit in next().
class TestExitWithPosition final : public ExitStrategy {
public:
    bool position_was_closed = false;

    void initialize_indicators() override {}

    void start() override {
        // Open a position immediately
        (void)buy(1.0);
    }

    [[nodiscard]] bool check_exit_signal() override {
        return data().close()[0] < 100.0;
    }

    void notify_trade(const Trade& trade, [[maybe_unused]] double original_size) override {
        if (trade.is_closed()) {
            position_was_closed = true;
        }
    }
};

} // namespace

TEST_CASE("ExitStrategy lifecycle wiring", "[strategy][exit_strategy]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
    }), "main");

    auto& strategy = cerebro.add_strategy<TestExitStrategy>();
    cerebro.run();

    SECTION("initialize_indicators called via init()") {
        REQUIRE(strategy.init_called);
    }

    SECTION("check_exit_signal called each bar (but no close since flat)") {
        // No position -> check_exit_signal not called (position().size == 0)
        REQUIRE(strategy.exit_checks == 0);
    }
}

TEST_CASE("ExitStrategy triggers close when in position", "[strategy][exit_strategy]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {102.0, 103.0, 101.0, 102.0},   // bar 0: buy submitted in start()
        {101.0, 102.0, 100.0, 101.0},   // bar 1: buy fills, close>100 -> no exit
        {98.0, 99.0, 97.0, 98.0},       // bar 2: close<100 -> exit signal
        {97.0, 98.0, 96.0, 97.0},       // bar 3: close order fills
    }), "main");

    auto& strategy = cerebro.add_strategy<TestExitWithPosition>();
    cerebro.run();

    REQUIRE(strategy.position_was_closed);
    REQUIRE(strategy.position().size == 0.0);
}

TEST_CASE("ExitStrategy does not close when exit signal is false", "[strategy][exit_strategy]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {102.0, 103.0, 101.0, 102.0},   // bar 0: buy submitted in start()
        {103.0, 104.0, 102.0, 103.0},   // bar 1: buy fills, close>100 -> no exit
        {104.0, 105.0, 103.0, 104.0},   // bar 2: close>100 -> no exit
    }), "main");

    auto& strategy = cerebro.add_strategy<TestExitWithPosition>();
    cerebro.run();

    // Position still open
    REQUIRE(strategy.position().size != 0.0);
}
