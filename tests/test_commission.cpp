#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/broker/commission.hpp>

using namespace stratforge;
using Catch::Matchers::WithinRel;

TEST_CASE("Commission percentage scheme", "[broker][commission]") {
    CommissionInfo info{
        .type = CommissionType::Percentage,
        .commission = 0.001, // 0.1%
    };

    SECTION("Basic percentage calculation") {
        double comm = info.calculate(100.0, 50.0); // 100 shares @ $50
        REQUIRE_THAT(comm, WithinRel(5.0, 0.001));  // 0.1% of $5000 = $5
    }

    SECTION("Zero size") {
        double comm = info.calculate(0.0, 50.0);
        REQUIRE(comm == 0.0);
    }

    SECTION("Negative size (sell)") {
        double comm = info.calculate(-100.0, 50.0);
        REQUIRE_THAT(comm, WithinRel(5.0, 0.001));  // Uses absolute size
    }
}

TEST_CASE("Commission fixed scheme", "[broker][commission]") {
    CommissionInfo info{
        .type = CommissionType::Fixed,
        .commission = 9.99,
    };

    SECTION("Fixed commission regardless of size") {
        double comm = info.calculate(100.0, 50.0);
        REQUIRE_THAT(comm, WithinRel(9.99, 0.001));

        comm = info.calculate(1.0, 50.0);
        REQUIRE_THAT(comm, WithinRel(9.99, 0.001));
    }
}

TEST_CASE("Commission per-share scheme", "[broker][commission]") {
    CommissionInfo info{
        .type = CommissionType::PerShare,
        .commission = 0.005, // $0.005 per share
    };

    SECTION("Per-share calculation") {
        double comm = info.calculate(1000.0, 50.0); // 1000 shares
        REQUIRE_THAT(comm, WithinRel(5.0, 0.001));  // 1000 * $0.005 = $5
    }

    SECTION("Small order") {
        double comm = info.calculate(1.0, 50.0);
        REQUIRE_THAT(comm, WithinRel(0.005, 0.001));
    }
}

TEST_CASE("Commission minimum", "[broker][commission]") {
    CommissionInfo info{
        .type = CommissionType::PerShare,
        .commission = 0.005,
        .min_commission = 1.0,  // Minimum $1 per trade
    };

    SECTION("Above minimum") {
        double comm = info.calculate(1000.0, 50.0); // $5 > $1 minimum
        REQUIRE_THAT(comm, WithinRel(5.0, 0.001));
    }

    SECTION("Below minimum - uses minimum") {
        double comm = info.calculate(10.0, 50.0);   // $0.05 < $1 minimum
        REQUIRE_THAT(comm, WithinRel(1.0, 0.001));
    }
}

TEST_CASE("TieredCommission per-share tiers", "[broker][commission][tiered]") {
    TieredCommission tiered;
    tiered.tier_type = CommissionType::PerShare;
    tiered.tiers = {
        {500.0, 0.01},     // First 500 shares: $0.01/share
        {1000.0, 0.008},   // Next 500 shares: $0.008/share
        {5000.0, 0.005},   // Above 1000: $0.005/share
    };

    SECTION("Small order within first tier") {
        // 100 shares * $0.01 = $1.00
        REQUIRE_THAT(tiered.calculate(100.0, 50.0), WithinRel(1.0, 0.001));
    }

    SECTION("Order spanning two tiers") {
        // 800 shares: 500 * 0.01 + 300 * 0.008 = 5.0 + 2.4 = 7.4
        REQUIRE_THAT(tiered.calculate(800.0, 50.0), WithinRel(7.4, 0.001));
    }

    SECTION("Order spanning all tiers") {
        // 2000 shares: 500*0.01 + 500*0.008 + 1000*0.005 = 5 + 4 + 5 = 14
        REQUIRE_THAT(tiered.calculate(2000.0, 50.0), WithinRel(14.0, 0.001));
    }

    SECTION("Order exceeding all tiers uses last rate") {
        // 6000 shares: 500*0.01 + 500*0.008 + 4000*0.005 + 1000*0.005
        // = 5 + 4 + 20 + 5 = 34
        REQUIRE_THAT(tiered.calculate(6000.0, 50.0), WithinRel(34.0, 0.001));
    }

    SECTION("Minimum commission applied") {
        tiered.min_commission = 5.0;
        // 10 shares * 0.01 = $0.10 < $5 minimum
        REQUIRE_THAT(tiered.calculate(10.0, 50.0), WithinRel(5.0, 0.001));
    }
}

TEST_CASE("IBCommission per-share with min/max", "[broker][commission][ib]") {
    IBCommission ib;
    ib.per_share = 0.005;
    ib.min_per_order = 1.0;
    ib.max_pct_of_value = 0.01; // 1% of trade value

    SECTION("Normal order") {
        // 500 shares * $0.005 = $2.50 (within min/max)
        REQUIRE_THAT(ib.calculate(500.0, 50.0), WithinRel(2.5, 0.001));
    }

    SECTION("Small order hits minimum") {
        // 10 shares * $0.005 = $0.05 -> capped at $1.00 minimum
        REQUIRE_THAT(ib.calculate(10.0, 50.0), WithinRel(1.0, 0.001));
    }

    SECTION("Large order hits maximum cap") {
        // 10000 shares * $0.005 = $50. Max = 10000 * 50 * 0.01 = $5000. $50 < $5000, no cap.
        REQUIRE_THAT(ib.calculate(10000.0, 50.0), WithinRel(50.0, 0.001));

        // With very low price: 10000 shares * $0.005 = $50. Max = 10000 * 0.01 * 0.01 = $1.
        // $50 > $1, capped to $1.
        REQUIRE_THAT(ib.calculate(10000.0, 0.01), WithinRel(1.0, 0.001));
    }

    SECTION("Sell order uses absolute size") {
        REQUIRE_THAT(ib.calculate(-500.0, 50.0), WithinRel(2.5, 0.001));
    }
}
