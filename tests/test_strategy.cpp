#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>

#include "test_helpers.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

class LifecycleStrategy final : public Strategy {
public:
    std::vector<std::string> events;
    std::vector<double> main_closes;
    std::vector<double> hedge_closes;
    std::vector<std::string> visible_data_names;
    std::string label;
    bool enabled = false;
    int period = 0;
    double threshold = 0.0;

    [[nodiscard]] ParamMap default_params() const override {
        return {
            {"period", std::int64_t{2}},
            {"label", std::string{"default-label"}},
            {"enabled", true},
            {"threshold", 1.25},
        };
    }

    void init() override {
        period = param<int>("period");
        label = param<std::string>("label");
        enabled = param<bool>("enabled");
        threshold = param<double>("threshold");
        set_minimum_period(static_cast<std::size_t>(period));
        events.push_back("init");
    }

    void start() override {
        events.push_back("start");
        visible_data_names.push_back(data_name(0));
        visible_data_names.push_back(data_name(1));
        main_closes.push_back(data("main").close()[0]);
        hedge_closes.push_back(data("hedge").close()[0]);
    }

    void prenext() override {
        events.push_back("prenext@" + std::to_string(data().index()));
        main_closes.push_back(data(0).close()[0]);
        hedge_closes.push_back(data("hedge").close()[0]);
    }

    void nextstart() override {
        events.push_back("nextstart@" + std::to_string(data().index()));
        main_closes.push_back(data("main").close()[0]);
        hedge_closes.push_back(data(1).close()[0]);
    }

    void next() override {
        events.push_back("next@" + std::to_string(data().index()));
        main_closes.push_back(data().close()[0]);
        hedge_closes.push_back(data("hedge").close()[0]);
    }

    void stop() override {
        events.push_back("stop");
    }
};

class OrderStrategy final : public Strategy {
public:
    std::vector<OrderStatus> order_statuses;
    std::vector<std::size_t> order_ids;
    std::vector<double> trade_pnl;
    std::vector<double> trade_original_sizes;
    bool close_sent = false;

    void start() override {
        order_ids.push_back(buy(2.0));
    }

    void next() override {
        if (!close_sent && position().is_long()) {
            order_ids.push_back(close());
            close_sent = true;
        }
    }

    void notify_order(const Order& order) override {
        order_statuses.push_back(order.status);
    }

    void notify_trade(const Trade& trade, double original_size) override {
        if (trade.is_closed()) {
            trade_pnl.push_back(trade.pnlcomm);
            trade_original_sizes.push_back(original_size);
        }
    }
};

class CancelStrategy final : public Strategy {
public:
    std::size_t order_id = 0;
    std::vector<OrderStatus> statuses;

    void start() override {
        order_id = buy(1.0, 50.0, OrderType::Limit);
    }

    void next() override {
        if (order_id != 0) {
            cancel(order_id);
            order_id = 0;
        }
    }

    void notify_order(const Order& order) override {
        statuses.push_back(order.status);
    }
};

} // namespace

TEST_CASE("Strategy lifecycle, params, and named multi-data access are wired through Cerebro", "[strategy][lifecycle]") {
    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {101.0, 102.0, 100.0, 101.5},
        {102.0, 103.0, 101.0, 102.5},
        {103.0, 104.0, 102.0, 103.5},
    }), "main");
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {200.0, 201.0, 199.0, 200.5},
        {201.0, 202.0, 200.0, 201.5},
        {202.0, 203.0, 201.0, 202.5},
        {203.0, 204.0, 202.0, 203.5},
    }), "hedge");

    auto& strategy = cerebro.add_strategy_with_params<LifecycleStrategy>({
        {"period", std::int64_t{3}},
        {"label", std::string{"phase4"}},
    });

    cerebro.run();

    REQUIRE(strategy.events == std::vector<std::string>{
        "init",
        "start",
        "prenext@0",
        "prenext@1",
        "nextstart@2",
        "next@3",
        "stop",
    });
    REQUIRE(strategy.visible_data_names == std::vector<std::string>{"main", "hedge"});
    REQUIRE(strategy.label == "phase4");
    REQUIRE(strategy.enabled);
    REQUIRE(strategy.period == 3);
    REQUIRE_THAT(strategy.threshold, WithinRel(1.25, 1e-12));

    REQUIRE(strategy.main_closes.size() == 5);
    REQUIRE(strategy.hedge_closes.size() == 5);
    REQUIRE_THAT(strategy.main_closes[0], WithinRel(100.5, 1e-12));
    REQUIRE_THAT(strategy.main_closes[4], WithinRel(103.5, 1e-12));
    REQUIRE_THAT(strategy.hedge_closes[0], WithinRel(200.5, 1e-12));
    REQUIRE_THAT(strategy.hedge_closes[4], WithinRel(203.5, 1e-12));
}

TEST_CASE("Strategy order helpers delegate to broker and receive order and trade notifications", "[strategy][orders]") {
    Cerebro cerebro;
    cerebro.set_cash(1000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 102.0, 99.0, 101.0},
        {110.0, 112.0, 109.0, 111.0},
    }), "main");

    auto& strategy = cerebro.add_strategy<OrderStrategy>();
    cerebro.run();

    REQUIRE(strategy.order_ids.size() == 2);
    REQUIRE(strategy.order_ids[0] != 0);
    REQUIRE(strategy.order_ids[1] != 0);
    REQUIRE(strategy.order_statuses == std::vector<OrderStatus>{
        OrderStatus::Submitted,
        OrderStatus::Accepted,
        OrderStatus::Completed,
        OrderStatus::Submitted,
        OrderStatus::Accepted,
        OrderStatus::Completed,
    });
    REQUIRE(strategy.trade_pnl.size() == 1);
    REQUIRE(strategy.trade_original_sizes.size() == 1);
    REQUIRE_THAT(strategy.trade_original_sizes[0], WithinRel(2.0, 1e-12));
    REQUIRE_THAT(strategy.trade_pnl[0], WithinRel(20.0, 1e-12));
}

TEST_CASE("Strategy cancel helper cancels a live order before execution", "[strategy][orders]") {
    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {102.0, 103.0, 101.0, 102.0},
    }), "main");

    auto& strategy = cerebro.add_strategy<CancelStrategy>();
    cerebro.run();

    REQUIRE(strategy.statuses == std::vector<OrderStatus>{
        OrderStatus::Submitted,
        OrderStatus::Accepted,
        OrderStatus::Canceled,
    });
}
