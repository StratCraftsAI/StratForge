#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/engine/backtest_runner.hpp>
#include <stratforge/broker/sizer.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

class SizerStrategy : public Strategy {
public:
    std::vector<double> sizes;

    void next() override {
        // buy() without size uses sizer
        auto order_id = buy();
        for (const auto& order : broker().orders()) {
            if (order.id == order_id) {
                sizes.push_back(order.size);
                break;
            }
        }
    }
};

class SellSizerStrategy : public Strategy {
public:
    std::vector<double> sizes;

    void next() override {
        if (position().is_flat()) {
            (void)buy(10.0);
        } else {
            auto order_id = sell();
            for (const auto& order : broker().orders()) {
                if (order.id == order_id) {
                    sizes.push_back(order.size);
                    break;
                }
            }
        }
    }
};

} // namespace

TEST_CASE("FixedSize sizer returns constant size", "[sizer][fixed]") {
    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<SizerStrategy>();
    strategy.setsizer(std::make_unique<FixedSize>(5.0));

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    REQUIRE_THAT(strategy.sizes[0], WithinRel(5.0, 1e-12));
}

TEST_CASE("PercentSizer returns size based on portfolio value", "[sizer][percent]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<SizerStrategy>();
    strategy.setsizer(std::make_unique<PercentSizer>(10.0)); // 10%

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    // Value = 10000. 10% = 1000. Price = 100. Size = 10.
    REQUIRE_THAT(strategy.sizes[0], WithinRel(10.0, 1e-12));
}

TEST_CASE("AllInSizer uses all available cash for buy", "[sizer][allin]") {
    Cerebro cerebro;
    cerebro.set_cash(1000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<SizerStrategy>();
    strategy.setsizer(std::make_unique<AllInSizer>());

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    REQUIRE_THAT(strategy.sizes[0], WithinRel(10.0, 1e-12)); // 1000 / 100 = 10
}

TEST_CASE("AllInSizer uses position size for sell", "[sizer][allin]") {
    Cerebro cerebro;
    cerebro.set_cash(2000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<SellSizerStrategy>();
    strategy.setsizer(std::make_unique<AllInSizer>());

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    REQUIRE_THAT(strategy.sizes[0], WithinRel(10.0, 1e-12));
}

namespace {

class ReverserStrategy : public Strategy {
public:
    std::vector<double> sizes;

    void next() override {
        // First bar: buy (flat -> base_size)
        // Second bar: sell to reverse (position + base_size)
        if (position().is_flat()) {
            auto order_id = buy();
            for (const auto& order : broker().orders()) {
                if (order.id == order_id) {
                    sizes.push_back(order.size);
                    break;
                }
            }
        } else {
            auto order_id = sell();
            for (const auto& order : broker().orders()) {
                if (order.id == order_id) {
                    sizes.push_back(order.size);
                    break;
                }
            }
        }
    }
};

} // namespace

TEST_CASE("FixedReverser returns base_size when flat", "[sizer][reverser]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<ReverserStrategy>();
    strategy.setsizer(std::make_unique<FixedReverser>(5.0));

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    REQUIRE_THAT(strategy.sizes[0], WithinRel(5.0, 1e-12));
}

TEST_CASE("FixedReverser returns position + base_size when reversing", "[sizer][reverser]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<ReverserStrategy>();
    strategy.setsizer(std::make_unique<FixedReverser>(5.0));

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 2);
    // First bar: flat, returns base_size = 5
    REQUIRE_THAT(strategy.sizes[0], WithinRel(5.0, 1e-12));
    // Second bar: position is 5, returns 5 + 5 = 10
    REQUIRE_THAT(strategy.sizes[1], WithinRel(10.0, 1e-12));
}

// =========================================================================
// : BacktestConfig sizer parsing + run_backtest sizer wiring
// =========================================================================

TEST_CASE("parse_backtest_config defaults to PercentSizer", "[sizer][config]") {
    const auto cfg = stratforge::parse_backtest_config(
        R"({"data_file":"test.csv","initial_cash":50000})");
    REQUIRE(cfg.sizer_type == stratforge::SizerType::Percent);
    REQUIRE_THAT(cfg.sizer_param, WithinRel(100.0, 1e-12));
    REQUIRE(cfg.symbol_count == 1);
}

TEST_CASE("parse_backtest_config parses sizer_type fixed", "[sizer][config]") {
    const auto cfg = stratforge::parse_backtest_config(
        R"({"data_file":"t.csv","sizer_type":"fixed","sizer_param":2.5,"symbol_count":10})");
    REQUIRE(cfg.sizer_type == stratforge::SizerType::Fixed);
    REQUIRE_THAT(cfg.sizer_param, WithinRel(2.5, 1e-12));
    REQUIRE(cfg.symbol_count == 10);
}

TEST_CASE("parse_backtest_config parses sizer_type allin", "[sizer][config]") {
    const auto cfg = stratforge::parse_backtest_config(
        R"({"data_file":"t.csv","sizer_type":"allin"})");
    REQUIRE(cfg.sizer_type == stratforge::SizerType::AllIn);
}

TEST_CASE("parse_backtest_config parses sizer_type percent with symbol_count", "[sizer][config]") {
    const auto cfg = stratforge::parse_backtest_config(
        R"({"data_file":"t.csv","sizer_type":"percent","sizer_param":100,"symbol_count":66})");
    REQUIRE(cfg.sizer_type == stratforge::SizerType::Percent);
    REQUIRE_THAT(cfg.sizer_param, WithinRel(100.0, 1e-12));
    REQUIRE(cfg.symbol_count == 66);
}

TEST_CASE("parse_backtest_config clamps symbol_count < 1 to 1", "[sizer][config]") {
    const auto cfg = stratforge::parse_backtest_config(
        R"({"data_file":"t.csv","symbol_count":0})");
    REQUIRE(cfg.symbol_count == 1);
}

TEST_CASE("PercentSizer equal-weight 100/N via BacktestConfig", "[sizer][config][integration]") {
    Cerebro cerebro;
    cerebro.set_cash(100000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {50.0, 51.0, 49.0, 50.0},
    }));

    auto strategy = std::make_unique<SizerStrategy>();
    stratforge::BacktestConfig cfg;
    cfg.sizer_type = stratforge::SizerType::Percent;
    cfg.sizer_param = 100.0;
    cfg.symbol_count = 10;

    const double per_symbol_pct = cfg.sizer_param /
                                  static_cast<double>(cfg.symbol_count);
    strategy->setsizer(std::make_unique<PercentSizer>(per_symbol_pct));

    auto* ptr = strategy.get();
    cerebro.add_strategy(std::move(strategy));
    cerebro.run();

    REQUIRE(ptr->sizes.size() == 1);
    // 100000 * (100/10 = 10%) / 50 = 200
    REQUIRE_THAT(ptr->sizes[0], WithinRel(200.0, 1e-6));
}
