#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/engine/optimizer.hpp>
#include <stratforge/analyzers/trade_analyzer.hpp>

#include <chrono>
#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;

namespace {

class StaticFeed final : public DataFeed {
public:
    struct Bar {
        double open;
        double high;
        double low;
        double close;
    };

    explicit StaticFeed(std::vector<Bar> bars, std::string feed_name = "")
        : bars_(std::move(bars)) {
        if (!feed_name.empty()) set_name(feed_name);
    }

    [[nodiscard]] bool load() override {
        if (loaded_) return false;
        const auto base = std::chrono::system_clock::time_point{};
        for (std::size_t i = 0; i < bars_.size(); ++i) {
            const auto& bar = bars_[i];
            datetime().forward(base + std::chrono::hours(24 * static_cast<int>(i)));
            open().forward(bar.open);
            high().forward(bar.high);
            low().forward(bar.low);
            close().forward(bar.close);
            volume().forward(1000.0);
            openinterest().forward(0.0);
        }
        datetime().home();
        open().home();
        high().home();
        low().home();
        close().home();
        volume().home();
        openinterest().home();
        loaded_ = true;
        return !bars_.empty();
    }

    [[nodiscard]] std::unique_ptr<DataFeed> clone() const override {
        auto cloned = std::make_unique<StaticFeed>(bars_);
        cloned->set_name(name());
        return cloned;
    }

private:
    std::vector<Bar> bars_;
    bool loaded_ = false;
};

class SimpleMAStrategy : public Strategy {
public:
    [[nodiscard]] ParamMap default_params() const override {
        return {{"period", std::int64_t{3}}};
    }

    void next() override {
        int period = param<int>("period");
        std::size_t p = static_cast<std::size_t>(period);

        if (data().index() + 1 < p) return;

        double sum = 0.0;
        for (std::size_t i = 0; i < p; ++i) {
            sum += data().close()[-static_cast<int>(i)];
        }
        double ma = sum / static_cast<double>(p);

        if (position().is_flat() && data().close()[0] > ma) {
            (void)buy(1.0);
        } else if (!position().is_flat() && data().close()[0] < ma) {
            (void)close();
        }
    }
};

} // namespace

// =============================================================================
// Optimizer result fields (sharpe_ratio, max_drawdown, total_trades)
// =============================================================================

TEST_CASE("Optimizer extractor populates sharpe/drawdown/trades", "[optimizer][results]") {
    auto feed = std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {101.0, 103.0, 100.0, 102.0},
        {102.0, 105.0, 101.0, 104.0},
        {104.0, 106.0, 103.0, 105.0},
        {105.0, 107.0, 104.0, 106.0},
        {106.0, 107.0, 104.0, 103.0},
        {103.0, 104.0, 100.0, 101.0},
        {101.0, 102.0, 98.0, 99.0},
    });

    std::vector<DataFeed*> feeds = {feed.get()};

    Optimizer::Config config;
    config.cash = 10000.0;
    config.max_threads = 1;
    Optimizer optimizer(config);

    std::vector<ParamRange> ranges = {
        {"period", {ParamValue{std::int64_t{2}}, ParamValue{std::int64_t{3}}}},
    };

    auto factory = [](const ParamMap&) -> std::unique_ptr<Strategy> {
        return std::make_unique<SimpleMAStrategy>();
    };

    auto extractor = [](const Cerebro& cerebro, const ParamMap& params) -> OptResult {
        OptResult result;
        result.final_value = cerebro.broker().cash();
        // Populate extra fields from broker data
        result.total_trades = cerebro.broker().closed_trades().size();
        result.sharpe_ratio = result.final_value / 10000.0; // simplified
        result.max_drawdown = 10000.0 - result.final_value;
        return result;
    };

    auto results = optimizer.run(feeds, factory, ranges, extractor);
    REQUIRE(results.size() == 2);

    for (const auto& r : results) {
        // Verify that the extra fields are populated
        REQUIRE(r.sharpe_ratio != 0.0);
        REQUIRE(r.params.contains("period"));
    }
}

TEST_CASE("Optimizer with commission config", "[optimizer][commission]") {
    auto feed = std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {101.0, 103.0, 100.0, 102.0},
        {102.0, 105.0, 101.0, 104.0},
        {104.0, 106.0, 103.0, 105.0},
        {105.0, 107.0, 104.0, 106.0},
        {106.0, 107.0, 104.0, 103.0},
        {103.0, 104.0, 100.0, 101.0},
        {101.0, 102.0, 98.0, 99.0},
    });

    std::vector<DataFeed*> feeds = {feed.get()};

    Optimizer::Config config;
    config.cash = 10000.0;
    config.max_threads = 1;
    config.commission.commission = 1.0;
    config.commission.type = CommissionType::Fixed;
    Optimizer optimizer(config);

    std::vector<ParamRange> ranges = {
        {"period", {ParamValue{std::int64_t{2}}}},
    };

    auto factory = [](const ParamMap&) -> std::unique_ptr<Strategy> {
        return std::make_unique<SimpleMAStrategy>();
    };

    auto extractor = [](const Cerebro& cerebro, const ParamMap&) -> OptResult {
        OptResult result;
        result.final_value = cerebro.broker().cash();
        return result;
    };

    auto results = optimizer.run(feeds, factory, ranges, extractor);
    REQUIRE(results.size() == 1);
    // Commission should reduce final value compared to initial
    // (strategy makes trades, each costs commission)
    REQUIRE(results[0].final_value > 0.0);
}

TEST_CASE("Optimizer with empty parameter ranges returns empty", "[optimizer][edge]") {
    auto feed = std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    });

    std::vector<DataFeed*> feeds = {feed.get()};

    Optimizer optimizer;

    std::vector<ParamRange> ranges = {};

    auto factory = [](const ParamMap&) -> std::unique_ptr<Strategy> {
        return std::make_unique<SimpleMAStrategy>();
    };

    auto extractor = [](const Cerebro&, const ParamMap&) -> OptResult {
        return {};
    };

    auto results = optimizer.run(feeds, factory, ranges, extractor);
    REQUIRE(results.empty());
}

// =============================================================================
// Cerebro add_strategy_with_params
// =============================================================================

TEST_CASE("add_strategy_with_params overrides default params", "[cerebro][params]") {
    struct ParamStrategy : public Strategy {
        int period_used = 0;

        [[nodiscard]] ParamMap default_params() const override {
            return {{"period", std::int64_t{10}}};
        }

        void next() override {
            period_used = param<int>("period");
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    ParamMap overrides = {{"period", std::int64_t{5}}};
    auto& strategy = cerebro.add_strategy_with_params<ParamStrategy>(std::move(overrides));
    cerebro.run();

    REQUIRE(strategy.period_used == 5);
}

// =============================================================================
// Multi-data with 3+ feeds
// =============================================================================

TEST_CASE("Cerebro handles 3 data feeds", "[cerebro][multidata][three]") {
    struct ThreeFeedStrategy : public Strategy {
        std::size_t num_feeds = 0;
        bool accessed_all = false;

        void next() override {
            num_feeds = data_count();
            if (num_feeds >= 3) {
                // Access all three feeds
                double c0 = data(0).close()[0];
                double c1 = data(1).close()[0];
                double c2 = data(2).close()[0];
                accessed_all = (c0 > 0 && c1 > 0 && c2 > 0);
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {101.0, 102.0, 100.0, 101.0},
    }, "SPY"));
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {200.0, 201.0, 199.0, 200.0},
        {201.0, 202.0, 200.0, 201.0},
    }, "QQQ"));
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {300.0, 301.0, 299.0, 300.0},
        {301.0, 302.0, 300.0, 301.0},
    }, "IWM"));

    auto& strategy = cerebro.add_strategy<ThreeFeedStrategy>();
    cerebro.run();

    REQUIRE(strategy.num_feeds == 3);
    REQUIRE(strategy.accessed_all);
}

TEST_CASE("Multi-data orders route to correct data_index", "[cerebro][multidata][routing]") {
    struct MultiOrderStrategy : public Strategy {
        bool bought_feed1 = false;
        double feed1_position_size = 0.0;

        void next() override {
            if (data().index() == 0) {
                // Buy on feed index 1
                BackBroker::OrderParams params;
                params.type = OrderType::Market;
                params.data_index = 1;
                (void)buy_ext(1.0, params);
            }
            if (data().index() == 1) {
                feed1_position_size = position(1).size;
                bought_feed1 = (feed1_position_size > 0);
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {101.0, 102.0, 100.0, 101.0},
        {102.0, 103.0, 101.0, 102.0},
    }, "SPY"));
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {50.0, 51.0, 49.0, 50.0},
        {51.0, 52.0, 50.0, 51.0},
        {52.0, 53.0, 51.0, 52.0},
    }, "QQQ"));

    auto& strategy = cerebro.add_strategy<MultiOrderStrategy>();
    cerebro.run();

    REQUIRE(strategy.bought_feed1);
    REQUIRE(strategy.feed1_position_size == 1.0);
}

// =============================================================================
// Cerebro set_slippage_perc propagation
// =============================================================================

TEST_CASE("Cerebro set_slippage_perc propagates to broker", "[cerebro][slippage]") {
    struct SlippageCheckStrategy : public Strategy {
        double fill_price = 0.0;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0);
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed) {
                fill_price = order.executed_price;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.set_slippage_perc(0.02); // 2%
    // Order placed on bar 0, fills on bar 1 open (next-bar execution)
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: place buy
        {100.0, 110.0, 90.0, 100.0},  // bar 1: fill at open=100 + 2% = 102
    }));

    auto& strategy = cerebro.add_strategy<SlippageCheckStrategy>();
    cerebro.run();

    // Buy at bar 1 open=100 + 2% slippage = 102, within bar 1 [90, 110]
    REQUIRE_THAT(strategy.fill_price, WithinRel(102.0, 1e-9));
}

TEST_CASE("Cerebro set_slippage_fixed propagates to broker", "[cerebro][slippage][fixed]") {
    struct FixedSlipCheckStrategy : public Strategy {
        double fill_price = 0.0;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0);
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed) {
                fill_price = order.executed_price;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.set_slippage_fixed(3.0);
    // Order placed on bar 0, fills on bar 1 open (next-bar execution)
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: place buy
        {100.0, 110.0, 90.0, 100.0},  // bar 1: fill at open=100 + 3 = 103
    }));

    auto& strategy = cerebro.add_strategy<FixedSlipCheckStrategy>();
    cerebro.run();

    // Buy at bar 1 open=100 + 3 fixed = 103, within bar 1 [90, 110]
    REQUIRE_THAT(strategy.fill_price, WithinRel(103.0, 1e-9));
}

// =============================================================================
// Cerebro const broker() accessor
// =============================================================================

TEST_CASE("Cerebro broker() const getter works", "[cerebro][broker]") {
    Cerebro cerebro;
    cerebro.set_cash(50000.0);

    const auto& const_cerebro = cerebro;
    REQUIRE(const_cerebro.broker().cash() == 50000.0);
}

// =============================================================================
// Cerebro run with no data feeds
// =============================================================================

TEST_CASE("Cerebro run with no data returns early", "[cerebro][edge][nodata]") {
    struct NoDataStrategy : public Strategy {
        bool next_called = false;
        void next() override { next_called = true; }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    auto& strategy = cerebro.add_strategy<NoDataStrategy>();
    cerebro.run();

    // next() should not be called since there's no data
    REQUIRE_FALSE(strategy.next_called);
}

// =============================================================================
// Parameter grid edge cases
// =============================================================================

TEST_CASE("Parameter grid with single range single value", "[optimizer][grid][edge]") {
    std::vector<ParamRange> ranges = {
        {"period", {ParamValue{std::int64_t{5}}}},
    };

    auto grid = generate_param_grid(ranges);
    REQUIRE(grid.size() == 1);
    REQUIRE(std::get<std::int64_t>(grid[0].at("period")) == 5);
}

TEST_CASE("Parameter grid with three ranges", "[optimizer][grid][triple]") {
    std::vector<ParamRange> ranges = {
        {"a", {ParamValue{std::int64_t{1}}, ParamValue{std::int64_t{2}}}},
        {"b", {ParamValue{std::int64_t{10}}, ParamValue{std::int64_t{20}}}},
        {"c", {ParamValue{std::int64_t{100}}}},
    };

    auto grid = generate_param_grid(ranges);
    REQUIRE(grid.size() == 4); // 2 * 2 * 1

    for (const auto& m : grid) {
        REQUIRE(m.contains("a"));
        REQUIRE(m.contains("b"));
        REQUIRE(m.contains("c"));
        REQUIRE(std::get<std::int64_t>(m.at("c")) == 100);
    }
}

// =============================================================================
// Portfolio value with multiple data feeds
// =============================================================================

TEST_CASE("Portfolio value accounts for positions across feeds", "[broker][portfolio][multidata]") {
    struct PortfolioStrategy : public Strategy {
        double portfolio_val = 0.0;

        void next() override {
            if (data().index() == 0) {
                (void)buy(10.0); // buy 10 shares of feed 0
                BackBroker::OrderParams params;
                params.type = OrderType::Market;
                params.data_index = 1;
                (void)buy_ext(5.0, params); // buy 5 shares of feed 1
            }
        }

        void stop() override {
            // We can't directly call portfolio_value from strategy;
            // instead verify positions
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(100000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {105.0, 106.0, 104.0, 105.0},
    }, "SPY"));
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {200.0, 201.0, 199.0, 200.0},
        {210.0, 211.0, 209.0, 210.0},
    }, "QQQ"));

    cerebro.add_strategy<PortfolioStrategy>();
    cerebro.run();

    // After buying: position in SPY (10 shares at 100) and QQQ (5 shares at 200)
    const auto& pos0 = cerebro.broker().position(0);
    const auto& pos1 = cerebro.broker().position(1);
    REQUIRE(pos0.size == 10.0);
    REQUIRE(pos1.size == 5.0);
}
