#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

// =============================================================================
// StopTrailLimit order type
// =============================================================================

TEST_CASE("StopTrailLimit sell triggers trail then fills at limit", "[orders][stoptraillimit]") {
    struct StopTrailLimitStrategy : public Strategy {
        double filled_price = 0.0;
        bool got_fill = false;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0);
            }
            if (data().index() == 1) {
                // Place trailing stop-limit sell: trail 5.0, limit at 106.0
                BackBroker::OrderParams params;
                params.type = OrderType::StopTrailLimit;
                params.trail_amount = 5.0;
                params.price = 106.0; // limit price
                (void)sell_ext(1.0, params);
            }
        }

        void notify_order(const Order& order) override {
            if (order.type == OrderType::StopTrailLimit && order.status == OrderStatus::Completed) {
                filled_price = order.executed_price;
                got_fill = true;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    // Trail order placed on bar 1. Processing starts bar 2:
    // Bar 2: trail_stop=0 -> no fill. Update: high=110, trail=105
    // Bar 3: trail_stop=105, low=108>105 -> no fill. Update: high=112, trail=107
    // Bar 4: trail_stop=107, low=106<107 -> trail triggers. Limit check: high=111>=106 -> fill at max(106, open=109)=109
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: buy at market
        {100.0, 105.0, 99.0, 104.0},  // bar 1: place stop-trail-limit
        {104.0, 110.0, 103.0, 109.0}, // bar 2: init trail, high=110, trail=105
        {109.0, 112.0, 108.0, 111.0}, // bar 3: trail=105, no fill. Update: high=112, trail=107
        {109.0, 111.0, 106.0, 107.0}, // bar 4: trail=107, low=106<107 triggers. limit=106, high=111>=106, fill at max(106,109)=109
    }));

    auto& strategy = cerebro.add_strategy<StopTrailLimitStrategy>();
    cerebro.run();

    REQUIRE(strategy.got_fill);
    REQUIRE_THAT(strategy.filled_price, WithinRel(109.0, 1e-9));
}

TEST_CASE("StopTrailLimit buy triggers trail then fills at limit", "[orders][stoptraillimit][buy]") {
    struct StopTrailLimitBuyStrategy : public Strategy {
        double filled_price = 0.0;
        bool got_fill = false;

        void next() override {
            if (data().index() == 0) {
                (void)sell(1.0);
            }
            if (data().index() == 1) {
                // Place trailing stop-limit buy: trail 5.0, limit at 94.0
                BackBroker::OrderParams params;
                params.type = OrderType::StopTrailLimit;
                params.trail_amount = 5.0;
                params.price = 94.0; // limit price
                (void)buy_ext(1.0, params);
            }
        }

        void notify_order(const Order& order) override {
            if (order.type == OrderType::StopTrailLimit && order.status == OrderStatus::Completed) {
                filled_price = order.executed_price;
                got_fill = true;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    // Buy trail: follows price down. trail_stop = low + trail_amount
    // Bar 2: trail_stop=0 -> no fill. Update: low=90, trail=95
    // Bar 3: trail_stop=95, high=92<95 -> no fill. Update: low=88, trail=93 (lower)
    // Bar 4: trail_stop=93, high=94>=93 -> triggers. Limit check: low=91<=94 -> fill at min(94, open=92)=92
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: sell short at market
        {100.0, 101.0, 95.0, 96.0},   // bar 1: place stop-trail-limit buy
        {96.0,  97.0,  90.0, 91.0},   // bar 2: init trail, low=90, trail=95
        {91.0,  92.0,  88.0, 89.0},   // bar 3: trail=95, high=92<95 no fill. Update: low=88, trail=93
        {92.0,  94.0,  91.0, 93.0},   // bar 4: trail=93, high=94>=93 triggers. limit=94, low=91<=94, fill at min(94,92)=92
    }));

    auto& strategy = cerebro.add_strategy<StopTrailLimitBuyStrategy>();
    cerebro.run();

    REQUIRE(strategy.got_fill);
    REQUIRE_THAT(strategy.filled_price, WithinRel(92.0, 1e-9));
}

// =============================================================================
// TrailPercent trailing stops
// =============================================================================

TEST_CASE("Trailing stop with trail_percent follows price proportionally", "[orders][trail][percent]") {
    struct TrailPercentStrategy : public Strategy {
        double filled_price = 0.0;
        bool got_fill = false;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0);
            }
            if (data().index() == 1) {
                BackBroker::OrderParams params;
                params.type = OrderType::StopTrail;
                params.trail_percent = 0.05; // 5% trail
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
    // Trail 5% of high price
    // Bar 2: trail_stop=0 -> no fill. high=110, trail = 110 - 110*0.05 = 104.5
    // Bar 3: trail=104.5, low=106>104.5 -> no fill. high=120, trail = 120 - 120*0.05 = 114
    // Bar 4: trail=114, low=112<114 -> fill at min(114, open=115)=114
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: buy
        {100.0, 105.0, 99.0, 104.0},  // bar 1: place trail
        {104.0, 110.0, 103.0, 109.0}, // bar 2: init trail=104.5
        {109.0, 120.0, 106.0, 115.0}, // bar 3: trail=104.5, no fill. Update: high=120, trail=114
        {115.0, 116.0, 112.0, 113.0}, // bar 4: trail=114, low=112<114, fill at min(114, 115)=114
    }));

    auto& strategy = cerebro.add_strategy<TrailPercentStrategy>();
    cerebro.run();

    REQUIRE(strategy.got_fill);
    REQUIRE_THAT(strategy.filled_price, WithinRel(114.0, 1e-9));
}

// =============================================================================
// sell_bracket
// =============================================================================

TEST_CASE("sell_bracket take-profit fills and cancels stop-loss", "[orders][bracket][sell]") {
    struct SellBracketTPStrategy : public Strategy {
        std::vector<std::pair<std::size_t, OrderStatus>> notifications;

        void next() override {
            if (data().index() == 0) {
                // Sell short, TP at 90 (buy back lower), SL at 110 (buy back higher)
                (void)sell_bracket(1.0, 90.0, 110.0);
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
        {100.0, 101.0, 99.0, 100.0},  // bar 0: sell short at 100
        {100.0, 101.0, 99.0, 100.0},  // bar 1: children activate
        {98.0,  99.0,  88.0, 89.0},   // bar 2: low=88 <= TP limit 90, TP fills
    }));

    auto& strategy = cerebro.add_strategy<SellBracketTPStrategy>();
    cerebro.run();

    int completed_count = 0;
    int canceled_count = 0;
    for (auto& [id, status] : strategy.notifications) {
        if (status == OrderStatus::Completed) completed_count++;
        if (status == OrderStatus::Canceled) canceled_count++;
    }
    // Main sell (1) + TP fills (1) = 2 completed, SL canceled (1)
    REQUIRE(completed_count == 2);
    REQUIRE(canceled_count == 1);
}

TEST_CASE("sell_bracket stop-loss fills and cancels take-profit", "[orders][bracket][sell][stoploss]") {
    struct SellBracketSLStrategy : public Strategy {
        std::vector<std::pair<std::size_t, OrderStatus>> notifications;

        void next() override {
            if (data().index() == 0) {
                (void)sell_bracket(1.0, 90.0, 110.0);
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
        {100.0, 101.0, 99.0, 100.0},  // bar 0: sell short at 100
        {100.0, 101.0, 99.0, 100.0},  // bar 1: children activate
        {105.0, 111.0, 104.0, 108.0}, // bar 2: high=111 >= SL stop 110, SL fills
    }));

    auto& strategy = cerebro.add_strategy<SellBracketSLStrategy>();
    cerebro.run();

    int completed_count = 0;
    int canceled_count = 0;
    for (auto& [id, status] : strategy.notifications) {
        if (status == OrderStatus::Completed) completed_count++;
        if (status == OrderStatus::Canceled) canceled_count++;
    }
    // Main sell (1) + SL fills (1) = 2 completed, TP canceled (1)
    REQUIRE(completed_count == 2);
    REQUIRE(canceled_count == 1);
}

// =============================================================================
// cancel_order in non-live mode
// =============================================================================

TEST_CASE("cancel_order cancels a pending limit order", "[orders][cancel]") {
    struct CancelStrategy : public Strategy {
        std::size_t order_id = 0;
        bool got_canceled = false;

        void next() override {
            if (data().index() == 0) {
                BackBroker::OrderParams params;
                params.type = OrderType::Limit;
                params.price = 50.0; // well below market, won't fill
                order_id = buy_ext(1.0, params);
            }
            if (data().index() == 1) {
                cancel(order_id);
            }
        }

        void notify_order(const Order& order) override {
            if (order.id == order_id && order.status == OrderStatus::Canceled) {
                got_canceled = true;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<CancelStrategy>();
    cerebro.run();

    REQUIRE(strategy.got_canceled);
}

// =============================================================================
// Order partial_fill method
// =============================================================================

TEST_CASE("Order partial_fill accumulates size and commission", "[order][partial]") {
    Order order;
    order.id = 1;
    order.type = OrderType::Market;
    order.side = OrderSide::Buy;
    order.size = 10.0;

    order.submit();
    REQUIRE(order.status == OrderStatus::Submitted);

    order.accept();
    REQUIRE(order.status == OrderStatus::Accepted);

    order.partial_fill(100.0, 3.0, 1.5);
    REQUIRE(order.status == OrderStatus::Partial);
    REQUIRE(order.executed_size == 3.0);
    REQUIRE(order.executed_price == 100.0);
    REQUIRE(order.commission == 1.5);
    REQUIRE(order.is_alive()); // Partial is alive

    order.partial_fill(101.0, 4.0, 2.0);
    REQUIRE(order.status == OrderStatus::Partial);
    REQUIRE(order.executed_size == 7.0);
    REQUIRE(order.executed_price == 101.0); // Latest fill price
    REQUIRE(order.commission == 3.5);       // Accumulated

    order.execute(102.0, 10.0, 5.0);
    REQUIRE(order.status == OrderStatus::Completed);
    REQUIRE(order.is_complete());
    REQUIRE_FALSE(order.is_alive());
}

// =============================================================================
// Multiple OCO groups simultaneously
// =============================================================================

TEST_CASE("Multiple OCO groups work independently", "[orders][oco][multi]") {
    struct MultiOcoStrategy : public Strategy {
        std::vector<std::pair<std::size_t, OrderStatus>> notifications;

        void next() override {
            if (data().index() == 0) {
                // OCO Group 1: limit buy at 95 vs stop buy at 105
                BackBroker::OrderParams lp1;
                lp1.type = OrderType::Limit;
                lp1.price = 95.0;
                lp1.oco_group_id = 42;
                (void)buy_ext(1.0, lp1);

                BackBroker::OrderParams sp1;
                sp1.type = OrderType::Stop;
                sp1.stop_price = 105.0;
                sp1.oco_group_id = 42;
                (void)buy_ext(1.0, sp1);

                // OCO Group 2: limit sell at 110 vs stop sell at 90
                BackBroker::OrderParams lp2;
                lp2.type = OrderType::Limit;
                lp2.price = 110.0;
                lp2.oco_group_id = 43;
                (void)sell_ext(1.0, lp2);

                BackBroker::OrderParams sp2;
                sp2.type = OrderType::Stop;
                sp2.stop_price = 90.0;
                sp2.oco_group_id = 43;
                (void)sell_ext(1.0, sp2);
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
        {100.0, 101.0, 99.0, 100.0},   // bar 0: place all orders
        {100.0, 106.0, 96.0, 103.0},   // bar 1: high=106>=105 (stop buy fills, group 1), low=96>90 (stop sell no fill, group 2)
        {103.0, 104.0, 88.0, 91.0},    // bar 2: low=88<=90 (stop sell fills, group 2)
    }));

    auto& strategy = cerebro.add_strategy<MultiOcoStrategy>();
    cerebro.run();

    // Each group: 1 completed + 1 canceled = 2 completed, 2 canceled total
    int completed_count = 0;
    int canceled_count = 0;
    for (auto& [id, status] : strategy.notifications) {
        if (status == OrderStatus::Completed) completed_count++;
        if (status == OrderStatus::Canceled) canceled_count++;
    }
    REQUIRE(completed_count == 2);
    REQUIRE(canceled_count == 2);
}

// =============================================================================
// Slippage flags: slip_limit, slip_open, slip_match, slip_out
// =============================================================================

TEST_CASE("Percentage slippage adjusts fill price", "[orders][slippage][perc]") {
    struct SlipStrategy : public Strategy {
        double filled_price = 0.0;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0); // market buy fills at next bar open
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed) {
                filled_price = order.executed_price;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.set_slippage_perc(0.01); // 1% slippage
    // Order placed bar 0, fills bar 1 at open
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: place order
        {100.0, 110.0, 90.0, 100.0},  // bar 1: open=100 + 1% = 101, in [90,110]
    }));

    auto& strategy = cerebro.add_strategy<SlipStrategy>();
    cerebro.run();

    REQUIRE_THAT(strategy.filled_price, WithinRel(101.0, 1e-9));
}

TEST_CASE("Fixed slippage adjusts fill price", "[orders][slippage][fixed]") {
    struct FixedSlipStrategy : public Strategy {
        double filled_price = 0.0;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0);
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed) {
                filled_price = order.executed_price;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.set_slippage_fixed(2.0); // 2 points slippage
    // Order placed bar 0, fills bar 1 at open
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: place order
        {100.0, 110.0, 90.0, 100.0},  // bar 1: open=100 + 2 = 102, in [90,110]
    }));

    auto& strategy = cerebro.add_strategy<FixedSlipStrategy>();
    cerebro.run();

    REQUIRE_THAT(strategy.filled_price, WithinRel(102.0, 1e-9));
}

TEST_CASE("Slippage with slip_out allows fill outside bar range", "[orders][slippage][slipout]") {
    struct SlipOutStrategy : public Strategy {
        double filled_price = 0.0;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0);
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed) {
                filled_price = order.executed_price;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(100000.0);
    // slip_out=true allows fill outside high/low range
    cerebro.set_slippage_fixed(20.0, true, true, true, true);
    // Order placed bar 0, fills bar 1 at open
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: place order
        {100.0, 105.0, 95.0, 100.0},  // bar 1: open=100 + 20 = 120, outside [95,105]
    }));

    auto& strategy = cerebro.add_strategy<SlipOutStrategy>();
    cerebro.run();

    // slip_out=true: fill at 120 even though it's outside bar range
    REQUIRE_THAT(strategy.filled_price, WithinRel(120.0, 1e-9));
}

TEST_CASE("Slippage with slip_match clamps to bar range", "[orders][slippage][slipmatch]") {
    struct SlipMatchStrategy : public Strategy {
        double filled_price = 0.0;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0);
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed) {
                filled_price = order.executed_price;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    // slip_match=true (default): clamp to bar range
    cerebro.set_slippage_fixed(20.0, true, true, true, false);
    // Order placed bar 0, fills bar 1 at open
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},  // bar 0: place order
        {100.0, 105.0, 95.0, 100.0},  // bar 1: open=100 + 20 = 120, clamped to high=105
    }));

    auto& strategy = cerebro.add_strategy<SlipMatchStrategy>();
    cerebro.run();

    REQUIRE_THAT(strategy.filled_price, WithinRel(105.0, 1e-9));
}

// =============================================================================
// Order status transitions
// =============================================================================

TEST_CASE("Order status transitions follow state machine", "[order][status]") {
    Order order;
    order.id = 1;
    order.type = OrderType::Market;
    order.side = OrderSide::Buy;
    order.size = 1.0;

    REQUIRE(order.status == OrderStatus::Created);
    REQUIRE(order.is_alive());
    REQUIRE_FALSE(order.is_complete());

    order.submit();
    REQUIRE(order.status == OrderStatus::Submitted);
    REQUIRE(order.is_alive());

    order.accept();
    REQUIRE(order.status == OrderStatus::Accepted);
    REQUIRE(order.is_alive());

    // Test each terminal state
    Order cancel_order = order;
    cancel_order.cancel();
    REQUIRE(cancel_order.status == OrderStatus::Canceled);
    REQUIRE(cancel_order.is_complete());

    Order reject_order = order;
    reject_order.reject();
    REQUIRE(reject_order.status == OrderStatus::Rejected);
    REQUIRE(reject_order.is_complete());

    Order expire_order = order;
    expire_order.expire();
    REQUIRE(expire_order.status == OrderStatus::Expired);
    REQUIRE(expire_order.is_complete());

    Order margin_order = order;
    margin_order.margin();
    REQUIRE(margin_order.status == OrderStatus::Margin);
    REQUIRE(margin_order.is_complete());

    Order exec_order = order;
    exec_order.execute(100.0, 1.0, 0.5);
    REQUIRE(exec_order.status == OrderStatus::Completed);
    REQUIRE(exec_order.is_complete());
    REQUIRE(exec_order.executed_price == 100.0);
    REQUIRE(exec_order.executed_size == 1.0);
    REQUIRE(exec_order.commission == 0.5);
}

// =============================================================================
// Cerebro commission propagation
// =============================================================================

TEST_CASE("Cerebro set_commission propagates to broker", "[cerebro][commission]") {
    struct CommStrategy : public Strategy {
        double commission_paid = 0.0;

        void next() override {
            if (data().index() == 0) {
                (void)buy(1.0);
            }
        }

        void notify_order(const Order& order) override {
            if (order.status == OrderStatus::Completed) {
                commission_paid = order.commission;
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(100000.0);
    CommissionInfo info;
    info.commission = 0.001; // 0.1%
    info.type = CommissionType::Percentage;
    cerebro.set_commission(info);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<CommStrategy>();
    cerebro.run();

    // 0.1% of 100 * 1 share = 0.1
    REQUIRE(strategy.commission_paid > 0.0);
}

// =============================================================================
// Close flat position returns 0
// =============================================================================

TEST_CASE("Closing flat position returns 0", "[broker][close][flat]") {
    struct CloseStrategy : public Strategy {
        std::size_t close_result = 999;

        void next() override {
            if (data().index() == 0) {
                // No position yet, close should return 0
                close_result = close();
            }
        }
    };

    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<CloseStrategy>();
    cerebro.run();

    REQUIRE(strategy.close_result == 0);
}
