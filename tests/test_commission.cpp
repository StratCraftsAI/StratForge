#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/broker/commission.hpp>

using namespace stratforge;
using Catch::Matchers::WithinRel;

// ============================================================================
//  Phase E: Catch2 GENERATE parametrization for commission schemes.
// Each table-driven row (size, price, expected) replaces what was previously
// a SECTION block.  Scheme-shape variations (percentage, fixed, per-share)
// remain as separate TEST_CASEs for clarity.
// ============================================================================

namespace {

struct CommCase {
    double size;
    double price;
    double expected;
    const char* note;
};

} // namespace

TEST_CASE("Commission percentage scheme - table-driven", "[broker][commission][regression]") {
    CommissionInfo info{
        .type = CommissionType::Percentage,
        .commission = 0.001, // 0.1%
    };

    // 0.1% of (|size| * price); zero size short-circuits to zero.
    const auto c = GENERATE(values<CommCase>({
        {  100.0, 50.0, 5.0, "Basic percentage calculation"   },
        {    0.0, 50.0, 0.0, "Zero size short-circuits"       },
        { -100.0, 50.0, 5.0, "Negative size uses |size|"      },
    }));

    INFO("case: " << c.note << " size=" << c.size << " price=" << c.price);
    if (c.expected == 0.0) {
        REQUIRE(info.calculate(c.size, c.price) == 0.0);
    } else {
        REQUIRE_THAT(info.calculate(c.size, c.price), WithinRel(c.expected, 0.001));
    }
}

TEST_CASE("Commission fixed scheme - size-invariant", "[broker][commission][regression]") {
    CommissionInfo info{
        .type = CommissionType::Fixed,
        .commission = 9.99,
    };

    const auto size = GENERATE(100.0, 1.0, 1000.0, 0.5);
    INFO("size=" << size);
    REQUIRE_THAT(info.calculate(size, 50.0), WithinRel(9.99, 0.001));
}

TEST_CASE("Commission per-share scheme - table-driven", "[broker][commission][regression]") {
    CommissionInfo info{
        .type = CommissionType::PerShare,
        .commission = 0.005, // $0.005 per share
    };

    const auto c = GENERATE(values<CommCase>({
        { 1000.0, 50.0, 5.0,   "Per-share calculation" },
        {    1.0, 50.0, 0.005, "Small order"           },
    }));

    INFO("case: " << c.note);
    REQUIRE_THAT(info.calculate(c.size, c.price), WithinRel(c.expected, 0.001));
}

TEST_CASE("Commission minimum - applied when below threshold", "[broker][commission][regression]") {
    CommissionInfo info{
        .type = CommissionType::PerShare,
        .commission = 0.005,
        .min_commission = 1.0,
    };

    const auto c = GENERATE(values<CommCase>({
        { 1000.0, 50.0, 5.0, "Above minimum"             },
        {   10.0, 50.0, 1.0, "Below minimum uses minimum"},
    }));

    INFO("case: " << c.note);
    REQUIRE_THAT(info.calculate(c.size, c.price), WithinRel(c.expected, 0.001));
}

TEST_CASE("TieredCommission per-share tiers - table-driven", "[broker][commission][tiered][regression]") {
    TieredCommission tiered;
    tiered.tier_type = CommissionType::PerShare;
    tiered.tiers = {
        {500.0, 0.01},     // First 500 shares: $0.01/share
        {1000.0, 0.008},   // Next 500 shares: $0.008/share
        {5000.0, 0.005},   // Above 1000:      $0.005/share
    };

    // 100 → 100*0.01 = 1.0
    // 800 → 500*0.01 + 300*0.008          = 7.4
    // 2000 → 500*0.01 + 500*0.008 + 1000*0.005 = 14.0
    // 6000 → 500*0.01 + 500*0.008 + 4000*0.005 + 1000*0.005 = 34.0
    const auto c = GENERATE(values<CommCase>({
        {  100.0, 50.0,  1.0,  "Within first tier"           },
        {  800.0, 50.0,  7.4,  "Spans two tiers"             },
        { 2000.0, 50.0, 14.0,  "Spans all tiers"             },
        { 6000.0, 50.0, 34.0,  "Exceeds all tiers"           },
    }));

    INFO("case: " << c.note);
    REQUIRE_THAT(tiered.calculate(c.size, c.price), WithinRel(c.expected, 0.001));
}

TEST_CASE("TieredCommission honors minimum commission", "[broker][commission][tiered][regression]") {
    TieredCommission tiered;
    tiered.tier_type = CommissionType::PerShare;
    tiered.tiers = {
        {500.0,  0.01},
        {1000.0, 0.008},
        {5000.0, 0.005},
    };
    tiered.min_commission = 5.0;
    // 10 shares * 0.01 = $0.10 < $5 minimum
    REQUIRE_THAT(tiered.calculate(10.0, 50.0), WithinRel(5.0, 0.001));
}

TEST_CASE("IBCommission per-share with min/max - table-driven", "[broker][commission][ib][regression]") {
    IBCommission ib;
    ib.per_share = 0.005;
    ib.min_per_order = 1.0;
    ib.max_pct_of_value = 0.01; // 1% of trade value

    // 500   * 0.005 = 2.5  (within min/max)
    // 10    * 0.005 = 0.05 → floored at 1.0 minimum
    // 10000 * 0.005 = 50; cap = 10000*50*0.01 = 5000 → no cap
    // 10000 * 0.005 = 50; cap = 10000*0.01*0.01 = 1.0 → capped
    // -500  * 0.005 = 2.5 (uses |size|)
    const auto c = GENERATE(values<CommCase>({
        {   500.0, 50.0,  2.5, "Normal order"                  },
        {    10.0, 50.0,  1.0, "Small order hits minimum"      },
        { 10000.0, 50.0, 50.0, "Large order, max cap inactive" },
        { 10000.0,  0.01, 1.0, "Large order, max cap active"   },
        {  -500.0, 50.0,  2.5, "Sell order uses |size|"        },
    }));

    INFO("case: " << c.note);
    REQUIRE_THAT(ib.calculate(c.size, c.price), WithinRel(c.expected, 0.001));
}
