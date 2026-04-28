#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>

#include "test_helpers.hpp"

#include <chrono>
#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

// =============================================================================
// 3a. Order Expiry
// =============================================================================

TEST_CASE("Order expires after valid_until_bar", "[orders][expiry]") {
    struct ExpiryStrategy : public Strategy {
        std::vector<OrderStatus> statuses;

        void next() override {
            if (data().index() == 0) {
                // Place a limit buy that won't fill, expires after bar 2
                BackBroker::OrderParams params;
                params.type = OrderType::Limit;
                params.price = 50.0; // well below market
                params.valid_until_bar = 2;
                (void)buy_ext(1.0, params);
            }
        }

        void notify_order(const Order& order) override {
            statuses.push_back(order.status);
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<ExpiryStrategy>();
    cerebro.run();

    // Should see: Submitted, Accepted, then Expired
    REQUIRE(!strategy.statuses.empty());
    REQUIRE(strategy.statuses.back() == OrderStatus::Expired);
}

// =============================================================================
// 3b. Trailing Stop
// =============================================================================

TEST_CASE("Trailing stop follows price and triggers on reversal", "[orders][trail]") {
    struct TrailStrategy : public Strategy {
        double filled_price = 0.0;
        bool got_fill = false;

        void next() override {
            if (data().index() == 0) {
                // Buy at market
                (void)buy(1.0);
            }
            if (data().index() == 1) {
                // Place trailing stop sell with fixed trail of 5.0
                BackBroker::OrderParams params;
                params.type = OrderType::StopTrail;
                params.trail_amount = 5.0;
                (void)sell_ext(1.0, params);
            }
        }

        void notify_order(const Order& order) override {
            if (order.type == OrderType::StopTrail && order.status == OrderStatus::Completed) {
                filled_price = order.executed_price;
                got_fill = true;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    // Trail order placed on bar 1. Processing starts bar 2:
    // Bar 2: trail_stop=0 -> no fill. Update: high=110 -> trail_stop=105
    // Bar 3: trail_stop=105, low=108 > 105 -> no fill. Update: high=112 -> trail_stop=107
    // Bar 4: trail_stop=107, low=106 < 107 -> fills at min(107, open=109) = 107
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: buy at market
        {100.0, 105.0, 99.0, 104.0},  // bar 1: place trail stop
        {104.0, 110.0, 103.0, 109.0}, // bar 2: init trail, high=110, trail=105 (no fill, trail_stop was 0)
        {109.0, 112.0, 108.0, 111.0}, // bar 3: trail=105, low=108>105 no fill. Update: high=112, trail=107
        {109.0, 111.0, 106.0, 107.0}, // bar 4: trail=107, low=106<107, fill at min(107, open=109)=107
    }));

    auto& strategy = cerebro.add_strategy<TrailStrategy>();
    cerebro.run();

    REQUIRE(strategy.got_fill);
    REQUIRE_THAT(strategy.filled_price, WithinRel(107.0, 1e-9));
}

// =============================================================================
// 3c. OCO (One-Cancels-Other)
// =============================================================================

TEST_CASE("OCO cancels sibling when one order fills", "[orders][oco]") {
    struct OcoStrategy : public Strategy {
        std::vector<std::pair<std::size_t, OrderStatus>> notifications;
        std::size_t oco_group = 0;

        void next() override {
            if (data().index() == 0) {
                // Use a fixed OCO group ID (non-zero)
                oco_group = 42;

                // Limit buy at 95 (will fill if price drops)
                BackBroker::OrderParams limit_params;
                limit_params.type = OrderType::Limit;
                limit_params.price = 95.0;
                limit_params.oco_group_id = oco_group;
                (void)buy_ext(1.0, limit_params);

                // Stop buy at 105 (will fill if price rises)
                BackBroker::OrderParams stop_params;
                stop_params.type = OrderType::Stop;
                stop_params.stop_price = 105.0;
                stop_params.oco_group_id = oco_group;
                (void)buy_ext(1.0, stop_params);
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed || order.status == OrderStatus::Canceled) {
                notifications.push_back({order.id, order.status});
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},   // bar 0: place orders
        {100.0, 106.0, 98.0, 103.0},   // bar 1: high hits 106, stop at 105 fills
    }));

    auto& strategy = cerebro.add_strategy<OcoStrategy>();
    cerebro.run();

    // One should be Completed, other should be Canceled
    bool has_completed = false;
    bool has_canceled = false;
    for (auto& [id, status] : strategy.notifications) {
        if (status == OrderStatus::Completed) has_completed = true;
        if (status == OrderStatus::Canceled) has_canceled = true;
    }
    REQUIRE(has_completed);
    REQUIRE(has_canceled);
}

// =============================================================================
// 3d. Bracket Orders
// =============================================================================

TEST_CASE("Bracket order take-profit fills and cancels stop-loss", "[orders][bracket]") {
    struct BracketTPStrategy : public Strategy {
        std::vector<std::pair<std::size_t, OrderStatus>> notifications;
        BackBroker::BracketResult bracket{};

        void next() override {
            if (data().index() == 0) {
                bracket = buy_bracket(1.0, 110.0, 90.0); // TP at 110, SL at 90
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed || order.status == OrderStatus::Canceled) {
                notifications.push_back({order.id, order.status});
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},   // bar 0: main buy fills at open=100
        {100.0, 111.0, 99.0, 110.0},   // bar 1: main already filled, children activate
        {110.0, 115.0, 109.0, 112.0},  // bar 2: high=115, TP limit at 110 fills
    }));

    auto& strategy = cerebro.add_strategy<BracketTPStrategy>();
    cerebro.run();

    // Main order fills, then TP fills and SL gets canceled
    int completed_count = 0;
    int canceled_count = 0;
    for (auto& [id, status] : strategy.notifications) {
        if (status == OrderStatus::Completed) completed_count++;
        if (status == OrderStatus::Canceled) canceled_count++;
    }
    // Main buy fills (1) + TP fills (1) = 2 completed, SL canceled (1)
    REQUIRE(completed_count == 2);
    REQUIRE(canceled_count == 1);
}

TEST_CASE("Bracket order stop-loss fills and cancels take-profit", "[orders][bracket]") {
    struct BracketSLStrategy : public Strategy {
        std::vector<std::pair<std::size_t, OrderStatus>> notifications;

        void next() override {
            if (data().index() == 0) {
                (void)buy_bracket(1.0, 110.0, 90.0); // TP at 110, SL at 90
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed || order.status == OrderStatus::Canceled) {
                notifications.push_back({order.id, order.status});
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},   // bar 0: main buy fills at open=100
        {100.0, 101.0, 99.0, 100.0},   // bar 1: children activate, no fill
        {95.0, 96.0, 89.0, 91.0},      // bar 2: low=89, SL at 90 fills
    }));

    auto& strategy = cerebro.add_strategy<BracketSLStrategy>();
    cerebro.run();

    int completed_count = 0;
    int canceled_count = 0;
    for (auto& [id, status] : strategy.notifications) {
        if (status == OrderStatus::Completed) completed_count++;
        if (status == OrderStatus::Canceled) canceled_count++;
    }
    // Main buy (1) + SL fills (1) = 2 completed, TP canceled (1)
    REQUIRE(completed_count == 2);
    REQUIRE(canceled_count == 1);
}
