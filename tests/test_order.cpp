#include <catch2/catch_test_macros.hpp>

#include <stratforge/broker/order.hpp>

using namespace stratforge;

TEST_CASE("Order default state", "[broker][order]") {
    Order order;
    REQUIRE(order.status == OrderStatus::Created);
    REQUIRE(order.type == OrderType::Market);
    REQUIRE(order.side == OrderSide::Buy);
    REQUIRE(order.is_alive());
    REQUIRE_FALSE(order.is_complete());
}

TEST_CASE("Order state machine transitions", "[broker][order]") {
    SECTION("Created -> Submitted -> Accepted -> Completed") {
        Order order;
        REQUIRE(order.status == OrderStatus::Created);

        order.submit();
        REQUIRE(order.status == OrderStatus::Submitted);
        REQUIRE(order.is_alive());

        order.accept();
        REQUIRE(order.status == OrderStatus::Accepted);
        REQUIRE(order.is_alive());

        order.execute(100.0, 10.0, 0.5);
        REQUIRE(order.status == OrderStatus::Completed);
        REQUIRE(order.is_complete());
        REQUIRE(order.executed_price == 100.0);
        REQUIRE(order.executed_size == 10.0);
        REQUIRE(order.commission == 0.5);
    }

    SECTION("Created -> Submitted -> Accepted -> Canceled") {
        Order order;
        order.submit();
        order.accept();
        order.cancel();
        REQUIRE(order.status == OrderStatus::Canceled);
        REQUIRE(order.is_complete());
    }

    SECTION("Created -> Rejected") {
        Order order;
        order.reject();
        REQUIRE(order.status == OrderStatus::Rejected);
        REQUIRE(order.is_complete());
    }

    SECTION("Created -> Submitted -> Accepted -> Expired") {
        Order order;
        order.submit();
        order.accept();
        order.expire();
        REQUIRE(order.status == OrderStatus::Expired);
        REQUIRE(order.is_complete());
    }

    SECTION("Margin status") {
        Order order;
        order.submit();
        order.accept();
        order.margin();
        REQUIRE(order.status == OrderStatus::Margin);
        REQUIRE(order.is_complete());
    }

    SECTION("Partial fill") {
        Order order;
        order.submit();
        order.accept();
        order.partial_fill(100.0, 5.0, 0.25);
        REQUIRE(order.status == OrderStatus::Partial);
        REQUIRE(order.is_alive());
        REQUIRE(order.executed_size == 5.0);
        REQUIRE(order.commission == 0.25);
    }
}

TEST_CASE("Order types", "[broker][order]") {
    SECTION("Market order") {
        Order order;
        order.type = OrderType::Market;
        REQUIRE(order.type == OrderType::Market);
    }

    SECTION("Limit order") {
        Order order;
        order.type = OrderType::Limit;
        order.price = 100.0;
        REQUIRE(order.type == OrderType::Limit);
        REQUIRE(order.price == 100.0);
    }

    SECTION("Stop order") {
        Order order;
        order.type = OrderType::Stop;
        order.stop_price = 95.0;
        REQUIRE(order.type == OrderType::Stop);
        REQUIRE(order.stop_price.has_value());
        REQUIRE(*order.stop_price == 95.0);
    }

    SECTION("StopLimit order") {
        Order order;
        order.type = OrderType::StopLimit;
        order.price = 100.0;
        order.stop_price = 95.0;
        REQUIRE(order.type == OrderType::StopLimit);
    }
}
