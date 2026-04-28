#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/broker/broker.hpp>

#include "test_helpers.hpp"

#include <chrono>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

TEST_CASE("Position update matches long-short lifecycle semantics", "[broker][position]") {
    Position position;

    position.update(10.0, 100.0);
    REQUIRE(position.is_long());
    REQUIRE(position.upopened == 10.0);
    REQUIRE(position.upclosed == 0.0);
    REQUIRE_THAT(position.avg_price, WithinRel(100.0, 1e-12));

    position.update(5.0, 110.0);
    REQUIRE(position.size == 15.0);
    REQUIRE(position.upopened == 5.0);
    REQUIRE(position.upclosed == 0.0);
    REQUIRE_THAT(position.avg_price, WithinRel(103.33333333333333, 1e-12));

    position.update(-8.0, 120.0);
    REQUIRE(position.size == 7.0);
    REQUIRE(position.upopened == 0.0);
    REQUIRE(position.upclosed == -8.0);
    REQUIRE_THAT(position.avg_price, WithinRel(103.33333333333333, 1e-12));

    position.update(-10.0, 90.0);
    REQUIRE(position.is_short());
    REQUIRE(position.upopened == -3.0);
    REQUIRE(position.upclosed == -7.0);
    REQUIRE_THAT(position.avg_price, WithinRel(90.0, 1e-12));

    position.update(3.0, 80.0);
    REQUIRE(position.is_flat());
    REQUIRE(position.upopened == 0.0);
    REQUIRE(position.upclosed == 3.0);
    REQUIRE_THAT(position.avg_price, WithinRel(0.0, 1e-12));
}

TEST_CASE("Trade update accumulates pnl and commission across the lifecycle", "[broker][trade]") {
    Trade trade;

    trade.update(10.0, 100.0, 0.1, 1);
    REQUIRE(trade.justopened);
    REQUIRE(trade.is_open());
    REQUIRE_FALSE(trade.is_closed());
    REQUIRE(trade.entry_bar == 1);
    REQUIRE_THAT(trade.entry_price, WithinRel(100.0, 1e-12));
    REQUIRE_THAT(trade.pnlcomm, WithinRel(-0.1, 1e-12));

    trade.update(5.0, 110.0, 0.2, 2);
    REQUIRE(trade.size == 15.0);
    REQUIRE_THAT(trade.entry_price, WithinRel(103.33333333333333, 1e-12));
    REQUIRE_THAT(trade.commission, WithinRel(0.3, 1e-12));
    REQUIRE_THAT(trade.pnl_gross(), WithinRel(0.0, 1e-12));

    trade.update(-8.0, 120.0, 0.3, 3);
    REQUIRE(trade.is_open());
    REQUIRE(trade.size == 7.0);
    REQUIRE_THAT(trade.pnl_gross(), WithinRel(133.33333333333337, 1e-12));
    REQUIRE_THAT(trade.pnlcomm, WithinRel(132.73333333333338, 1e-12));

    trade.update(-7.0, 90.0, 0.4, 4);
    REQUIRE(trade.is_closed());
    REQUIRE(trade.size == 0.0);
    REQUIRE(trade.exit_bar == 4);
    REQUIRE_THAT(trade.exit_price, WithinRel(90.0, 1e-12));
    REQUIRE_THAT(trade.pnl_gross(), WithinRel(40.00000000000007, 1e-12));
    REQUIRE_THAT(trade.closed_pnl(15.0), WithinRel(39.00000000000007, 1e-12));
}

TEST_CASE("BackBroker executes orders and updates positions and trades", "[broker][execution]") {
    StaticFeed feed({
        {100.0, 112.0, 95.0, 107.0},
        {108.0, 120.0, 100.0, 118.0},
    });
    REQUIRE(feed.load());
    std::vector<DataFeed*> feeds{&feed};

    BackBroker broker(5000.0);
    broker.set_commission({.type = CommissionType::Percentage, .commission = 0.001});

    std::vector<OrderStatus> order_events;
    broker.set_order_notify([&order_events](const Order& order) {
        order_events.push_back(order.status);
    });

    const auto order_id = broker.buy(10.0);
    broker.set_bar_index(0);
    broker.process_orders(feeds);

    REQUIRE(order_id != 0);
    REQUIRE(order_events.size() == 3);
    REQUIRE(order_events[0] == OrderStatus::Submitted);
    REQUIRE(order_events[1] == OrderStatus::Accepted);
    REQUIRE(order_events[2] == OrderStatus::Completed);

    const auto& order = broker.orders().front();
    REQUIRE(order.id == order_id);
    REQUIRE(order.status == OrderStatus::Completed);
    REQUIRE_THAT(order.executed_price, WithinRel(100.0, 1e-12));
    REQUIRE_THAT(order.commission, WithinRel(1.0, 1e-12));
    REQUIRE_THAT(broker.cash(), WithinRel(3999.0, 1e-12));

    const auto& position = broker.position();
    REQUIRE(position.is_long());
    REQUIRE(position.size == 10.0);
    REQUIRE_THAT(position.avg_price, WithinRel(100.0, 1e-12));

    const auto close_id = broker.close();
    REQUIRE(close_id != 0);
    feed.advance();
    broker.set_bar_index(1);
    broker.process_orders(feeds);

    REQUIRE(broker.position().is_flat());
    REQUIRE_THAT(broker.cash(), WithinRel(5077.92, 1e-12));
    REQUIRE(broker.closed_trades().size() == 1);
    REQUIRE_THAT(broker.closed_trades().front().first.pnl_gross(), WithinRel(80.0, 1e-12));
    REQUIRE_THAT(broker.closed_trades().front().first.closed_pnl(10.0), WithinRel(77.92, 1e-12));
}

TEST_CASE("BackBroker handles rejection, margin, reversal, and slippage", "[broker][execution]") {
    SECTION("Invalid data index is rejected") {
        StaticFeed feed({{100.0, 105.0, 95.0, 102.0}});
        REQUIRE(feed.load());
        std::vector<DataFeed*> feeds{&feed};

        BackBroker broker(1000.0);
        static_cast<void>(broker.buy(1.0, 0.0, std::nullopt, OrderType::Market, 3));
        broker.process_orders(feeds);

        REQUIRE(broker.orders().front().status == OrderStatus::Rejected);
    }

    SECTION("Insufficient cash produces margin status and leaves position unchanged") {
        StaticFeed feed({{100.0, 105.0, 95.0, 102.0}});
        REQUIRE(feed.load());
        std::vector<DataFeed*> feeds{&feed};

        BackBroker broker(50.0);
        static_cast<void>(broker.buy(1.0));
        broker.process_orders(feeds);

        REQUIRE(broker.orders().front().status == OrderStatus::Margin);
        REQUIRE(broker.position().is_flat());
        REQUIRE_THAT(broker.cash(), WithinRel(50.0, 1e-12));
    }

    SECTION("A reversal closes the current trade and opens a new one") {
        StaticFeed feed({
            {100.0, 105.0, 95.0, 102.0},
            {101.0, 104.0, 99.0, 100.0},
        });
        REQUIRE(feed.load());
        std::vector<DataFeed*> feeds{&feed};

        BackBroker broker(5000.0);
        static_cast<void>(broker.buy(5.0));
        broker.process_orders(feeds);

        static_cast<void>(broker.sell(8.0));
        feed.advance();
        broker.set_bar_index(1);
        broker.process_orders(feeds);

        REQUIRE(broker.closed_trades().size() == 1);
        REQUIRE(broker.open_trades().size() == 1);
        REQUIRE(broker.position().is_short());
        REQUIRE(broker.position().size == -3.0);
        REQUIRE_THAT(broker.closed_trades().front().first.exit_price, WithinRel(101.0, 1e-12));
        REQUIRE_THAT(broker.open_trades().front().first.entry_price, WithinRel(101.0, 1e-12));
    }

    SECTION("Percentage and fixed slippage worsen execution price") {
        StaticFeed feed({
            {100.0, 112.0, 95.0, 107.0},
            {108.0, 120.0, 100.0, 118.0},
        });
        REQUIRE(feed.load());
        std::vector<DataFeed*> feeds{&feed};

        BackBroker broker(5000.0);
        broker.set_slippage_perc(0.05);
        static_cast<void>(broker.buy(1.0));
        broker.process_orders(feeds);
        REQUIRE_THAT(broker.orders().front().executed_price, WithinRel(105.0, 1e-12));

        feed.advance();
        broker.set_bar_index(1);
        broker.set_slippage_fixed(2.0);
        static_cast<void>(broker.sell(1.0));
        broker.process_orders(feeds);
        REQUIRE_THAT(broker.orders().back().executed_price, WithinRel(106.0, 1e-12));
    }
}
