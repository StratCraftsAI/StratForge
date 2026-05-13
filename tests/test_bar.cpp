// SPDX-License-Identifier: MIT
//
// tests/test_bar.cpp — stratforge::Bar POD acceptance suite.
//
//  §5 acceptance tests. Tag form [bar][regression].

#include <catch2/catch_test_macros.hpp>

#include <stratforge/bar.hpp>

#include <array>
#include <chrono>
#include <span>
#include <type_traits>
#include <vector>

TEST_CASE("Bar is a standard-layout, trivially-copyable POD", "[bar][regression]") {
    STATIC_REQUIRE(std::is_standard_layout_v<stratforge::Bar>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<stratforge::Bar>);
    STATIC_REQUIRE(std::is_default_constructible_v<stratforge::Bar>);
    STATIC_REQUIRE(std::is_aggregate_v<stratforge::Bar>);
}

TEST_CASE("Bar default-constructs to zeroed fields and epoch timestamp", "[bar][regression]") {
    stratforge::Bar b{};

    REQUIRE(b.timestamp == stratforge::DateTime{});
    REQUIRE(b.open   == 0.0);
    REQUIRE(b.high   == 0.0);
    REQUIRE(b.low    == 0.0);
    REQUIRE(b.close  == 0.0);
    REQUIRE(b.volume == 0.0);
}

TEST_CASE("Bar supports defaulted three-way comparison", "[bar][regression]") {
    using namespace std::chrono_literals;
    const stratforge::DateTime t0{};
    const stratforge::DateTime t1 = t0 + 1h;

    stratforge::Bar a{t0, 1.0, 2.0, 0.5, 1.5, 100.0};
    stratforge::Bar b = a;

    SECTION("equal bars compare equal") {
        REQUIRE(a == b);
        REQUIRE_FALSE(a != b);
    }

    SECTION("differing timestamp orders by timestamp") {
        stratforge::Bar later = a;
        later.timestamp = t1;
        REQUIRE(a < later);
        REQUIRE(later > a);
    }

    SECTION("differing close on equal timestamp orders by trailing field") {
        stratforge::Bar higher_close = a;
        higher_close.close = 2.0;
        REQUIRE(a < higher_close);
    }
}

TEST_CASE("Bar interoperates with std::span", "[bar][regression]") {
    std::array<stratforge::Bar, 3> arr{};
    arr[0].close = 10.0;
    arr[1].close = 11.0;
    arr[2].close = 12.0;

    std::span<const stratforge::Bar> view{arr};

    REQUIRE(view.size() == 3);
    REQUIRE(view[0].close == 10.0);
    REQUIRE(view[2].close == 12.0);

    SECTION("span constructs from std::vector identically") {
        std::vector<stratforge::Bar> v(arr.begin(), arr.end());
        std::span<const stratforge::Bar> vview{v};
        REQUIRE(vview.size() == arr.size());
        REQUIRE(vview[1].close == arr[1].close);
    }
}
