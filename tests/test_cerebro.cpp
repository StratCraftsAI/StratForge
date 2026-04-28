#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

class RecordingAnalyzer final : public Analyzer {
public:
    std::vector<double> position_sizes;
    std::vector<double> cash_values;
    std::vector<double> main_closes;

    void start() override {
        started = true;
    }

    void next(const BackBroker& broker, const std::vector<DataFeed*>& feeds) override {
        position_sizes.push_back(broker.position().size);
        cash_values.push_back(broker.cash());
        main_closes.push_back(feeds.at(0)->close()[0]);
    }

    void stop() override {
        stopped = true;
    }

    bool started = false;
    bool stopped = false;
};

class RecordingObserver final : public Observer {
public:
    std::vector<double> position_sizes;
    std::vector<double> portfolio_values;

    void next(const BackBroker& broker, const std::vector<DataFeed*>& feeds) override {
        position_sizes.push_back(broker.position().size);
        portfolio_values.push_back(broker.portfolio_value(feeds));
    }
};

class EngineOrderStrategy final : public Strategy {
public:
    std::vector<std::string> events;
    std::vector<double> closes_seen;
    std::vector<double> hedge_seen;
    std::vector<OrderStatus> order_statuses;
    std::vector<double> trade_pnlcomm;

    void start() override {
        events.push_back("start");
        static_cast<void>(buy(1.0));
    }

    void nextstart() override {
        events.push_back("nextstart@" + std::to_string(data().index()));
        closes_seen.push_back(data().close()[0]);
        hedge_seen.push_back(data(1).close()[0]);
    }

    void next() override {
        events.push_back("next@" + std::to_string(data().index()));
        closes_seen.push_back(data().close()[0]);
        hedge_seen.push_back(data(1).close()[0]);

        if (data().index() == 1 && position().is_long()) {
            static_cast<void>(close());
        }
    }

    void notify_order(const Order& order) override {
        order_statuses.push_back(order.status);
    }

    void notify_trade(const Trade& trade, double) override {
        if (trade.is_closed()) {
            trade_pnlcomm.push_back(trade.pnlcomm);
        }
    }

    void stop() override {
        events.push_back("stop");
    }
};

class FeedTrackingStrategy final : public Strategy {
public:
    std::vector<std::string> names;
    std::vector<double> primary_closes;
    std::vector<double> secondary_closes;
    std::vector<std::size_t> primary_indices;
    std::vector<std::size_t> secondary_indices;

    void start() override {
        names.push_back(data_name(0));
        names.push_back(data_name(1));
    }

    void nextstart() override {
        record();
    }

    void next() override {
        record();
    }

private:
    void record() {
        primary_indices.push_back(data(0).index());
        secondary_indices.push_back(data(1).index());
        primary_closes.push_back(data(0).close()[0]);
        secondary_closes.push_back(data(1).close()[0]);
    }
};

class EquivalenceStrategy final : public Strategy {
public:
    std::vector<std::string> lifecycle;
    std::vector<double> closes;
    std::vector<double> sizes;
    std::vector<OrderStatus> statuses;

    void start() override {
        lifecycle.push_back("start");
    }

    void nextstart() override {
        lifecycle.push_back("nextstart@" + std::to_string(data().index()));
        closes.push_back(data().close()[0]);
        static_cast<void>(buy(1.0));
        sizes.push_back(position().size);
    }

    void next() override {
        lifecycle.push_back("next@" + std::to_string(data().index()));
        closes.push_back(data().close()[0]);
        if (data().index() == 1 && position().is_long()) {
            static_cast<void>(close());
        }
        sizes.push_back(position().size);
    }

    void stop() override {
        lifecycle.push_back("stop");
    }

    void notify_order(const Order& order) override {
        statuses.push_back(order.status);
    }
};

struct EquivalenceSnapshot {
    std::vector<std::string> lifecycle;
    std::vector<double> closes;
    std::vector<double> sizes;
    std::vector<OrderStatus> statuses;
};

} // namespace

TEST_CASE("Cerebro orchestrates broker, strategy, analyzer, and observer in deterministic order", "[cerebro][integration]") {
    Cerebro cerebro;
    cerebro.set_cash(1000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {110.0, 111.0, 109.0, 110.5},
        {120.0, 121.0, 119.0, 120.5},
    }), "main");
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {200.0, 201.0, 199.0, 200.5},
        {210.0, 211.0, 209.0, 210.5},
        {220.0, 221.0, 219.0, 220.5},
    }), "hedge");

    auto& strategy = cerebro.add_strategy<EngineOrderStrategy>();
    auto& analyzer = cerebro.add_analyzer<RecordingAnalyzer>();
    auto& observer = cerebro.add_observer<RecordingObserver>();

    cerebro.run();

    REQUIRE(analyzer.started);
    REQUIRE(analyzer.stopped);
    REQUIRE(strategy.events == std::vector<std::string>{
        "start",
        "nextstart@0",
        "next@1",
        "next@2",
        "stop",
    });
    REQUIRE(strategy.order_statuses == std::vector<OrderStatus>{
        OrderStatus::Submitted,
        OrderStatus::Accepted,
        OrderStatus::Completed,
        OrderStatus::Submitted,
        OrderStatus::Accepted,
        OrderStatus::Completed,
    });
    REQUIRE(strategy.trade_pnlcomm.size() == 1);
    REQUIRE_THAT(strategy.trade_pnlcomm.front(), WithinRel(20.0, 1e-12));

    REQUIRE(strategy.closes_seen == std::vector<double>{100.5, 110.5, 120.5});
    REQUIRE(strategy.hedge_seen == std::vector<double>{200.5, 210.5, 220.5});

    REQUIRE(analyzer.position_sizes == std::vector<double>{1.0, 1.0, 0.0});
    REQUIRE(analyzer.main_closes == std::vector<double>{100.5, 110.5, 120.5});
    REQUIRE(observer.position_sizes == std::vector<double>{1.0, 1.0, 0.0});
    REQUIRE(observer.portfolio_values.size() == 3);
    REQUIRE_THAT(analyzer.cash_values.at(0), WithinRel(900.0, 1e-12));
    REQUIRE_THAT(analyzer.cash_values.at(1), WithinRel(900.0, 1e-12));
    REQUIRE_THAT(analyzer.cash_values.at(2), WithinRel(1020.0, 1e-12));
    REQUIRE_THAT(observer.portfolio_values.at(0), WithinRel(1000.5, 1e-12));
    REQUIRE_THAT(observer.portfolio_values.at(1), WithinRel(1010.5, 1e-12));
    REQUIRE_THAT(observer.portfolio_values.at(2), WithinRel(1020.0, 1e-12));
}

TEST_CASE("Cerebro preserves feed insertion order and runs to the shortest feed length", "[cerebro][multidata]") {
    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {10.0, 11.0, 9.0, 10.5},
        {11.0, 12.0, 10.0, 11.5},
        {12.0, 13.0, 11.0, 12.5},
    }));
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {20.0, 21.0, 19.0, 20.5},
        {21.0, 22.0, 20.0, 21.5},
    }));

    auto& strategy = cerebro.add_strategy<FeedTrackingStrategy>();
    cerebro.run();

    REQUIRE(strategy.names == std::vector<std::string>{"data0", "data1"});
    REQUIRE(strategy.primary_indices == std::vector<std::size_t>{0, 1});
    REQUIRE(strategy.secondary_indices == std::vector<std::size_t>{0, 1});
    REQUIRE(strategy.primary_closes == std::vector<double>{10.5, 11.5});
    REQUIRE(strategy.secondary_closes == std::vector<double>{20.5, 21.5});
}

TEST_CASE("Cerebro run options keep runonce and bar-by-bar execution equivalent for covered scenarios", "[cerebro][runonce]") {
    auto make_cerebro = []() {
        Cerebro cerebro;
        cerebro.set_cash(1000.0);
        cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
            {100.0, 101.0, 99.0, 100.5},
            {110.0, 111.0, 109.0, 110.5},
            {120.0, 121.0, 119.0, 120.5},
        }), "main");
        return cerebro;
    };

    auto run_with = [&make_cerebro](Cerebro::RunOptions options) {
        auto cerebro = make_cerebro();
        auto& strategy = cerebro.add_strategy<EquivalenceStrategy>();
        cerebro.run(options);
        return EquivalenceSnapshot{
            .lifecycle = strategy.lifecycle,
            .closes = strategy.closes,
            .sizes = strategy.sizes,
            .statuses = strategy.statuses,
        };
    };

    const auto bar_by_bar = run_with({.runonce = false, .preload = true});
    const auto runonce = run_with({.runonce = true, .preload = true});

    REQUIRE(bar_by_bar.lifecycle == runonce.lifecycle);
    REQUIRE(bar_by_bar.closes == runonce.closes);
    REQUIRE(bar_by_bar.sizes == runonce.sizes);
    REQUIRE(bar_by_bar.statuses == runonce.statuses);

    auto cerebro = make_cerebro();
    REQUIRE_THROWS_AS(cerebro.run({.runonce = false, .preload = false}), std::invalid_argument);
}
