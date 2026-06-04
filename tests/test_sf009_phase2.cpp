#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/indicators/idxmax.hpp>
#include <stratforge/indicators/idxmin.hpp>
#include <stratforge/indicators/imxd.hpp>
#include <stratforge/indicators/rolling_residual.hpp>
#include <stratforge/indicators/rolling_rsquared.hpp>
#include <stratforge/indicators/cntp.hpp>
#include <stratforge/indicators/cntn.hpp>
#include <stratforge/indicators/cntd.hpp>
#include <stratforge/indicators/sump.hpp>
#include <stratforge/indicators/sum_neg_return.hpp>
#include <stratforge/indicators/sumd.hpp>
#include <stratforge/indicators/vsump.hpp>
#include <stratforge/indicators/vsumn.hpp>
#include <stratforge/indicators/vsumd.hpp>
#include <stratforge/indicators/wvma.hpp>
#include <stratforge/indicators/alpha158_vwap.hpp>
#include <stratforge/indicators/quantile.hpp>
#include <stratforge/indicators/pctrank.hpp>

#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

using stratforge::test::make_line;
using stratforge::test::run_indicator;
using stratforge::test::run_indicator_hl;
using stratforge::test::run_indicator_cv;
using stratforge::test::run_indicator_hlc;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

namespace {

const std::vector<double> kClose = {
    44.34, 44.09, 43.61, 44.33, 44.83, 45.10, 45.42, 45.84,
    46.08, 45.89, 46.03, 45.61, 46.28, 46.28, 46.00, 46.03,
    46.41, 46.22, 45.64, 46.21, 46.25, 45.71, 46.45, 45.78
};

const std::vector<double> kHigh = {
    44.52, 44.34, 44.09, 44.56, 44.97, 45.32, 45.60, 46.00,
    46.20, 46.10, 46.15, 45.85, 46.50, 46.40, 46.20, 46.25,
    46.55, 46.40, 45.90, 46.35, 46.45, 46.00, 46.60, 46.10
};

const std::vector<double> kLow = {
    44.10, 43.95, 43.50, 44.10, 44.70, 44.95, 45.25, 45.60,
    45.85, 45.75, 45.90, 45.50, 46.10, 46.10, 45.85, 45.90,
    46.20, 46.00, 45.50, 46.00, 46.05, 45.55, 46.25, 45.65
};

const std::vector<double> kVolume = {
    100000, 120000, 95000, 110000, 130000, 140000, 115000, 125000,
    105000, 135000, 110000, 98000, 142000, 108000, 100000, 115000,
    128000, 112000, 135000, 118000, 102000, 138000, 125000, 105000
};

} // namespace

// =============================================================================
// IdxMax
// =============================================================================

TEST_CASE("IdxMax returns bars since highest in window", "[indicator][idxmax][regression]") {
    auto source = make_line(kHigh);
    stratforge::IdxMax ind(source, 5);
    run_indicator(source, ind);

    REQUIRE(ind.line().size() == kHigh.size());

    SECTION("Warmup produces NaN") {
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("First valid value is correct") {
        // Window [0..4]: highs 44.52, 44.34, 44.09, 44.56, 44.97
        // Max is 44.97 at index 4 (current bar) -> offset 0
        CHECK(ind.line().data()[4] == 0.0);
    }

    SECTION("Manual check at index 8") {
        // Window [4..8]: highs 44.97, 45.32, 45.60, 46.00, 46.20
        // Max is 46.20 at index 8 (current) -> offset 0
        CHECK(ind.line().data()[8] == 0.0);
    }
}

// =============================================================================
// IdxMin
// =============================================================================

TEST_CASE("IdxMin returns bars since lowest in window", "[indicator][idxmin][regression]") {
    auto source = make_line(kLow);
    stratforge::IdxMin ind(source, 5);
    run_indicator(source, ind);

    REQUIRE(ind.line().size() == kLow.size());

    SECTION("Warmup produces NaN") {
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("First valid value") {
        // Window [0..4]: lows 44.10, 43.95, 43.50, 44.10, 44.70
        // Min is 43.50 at index 2 -> offset = 4-2 = 2
        CHECK(ind.line().data()[4] == 2.0);
    }
}

// =============================================================================
// ImxD
// =============================================================================

TEST_CASE("ImxD computes (IdxMax-IdxMin)/period", "[indicator][imxd][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::ImxD ind(h, l, 5);
    run_indicator_hl(h, l, ind);

    REQUIRE(ind.line().size() == kHigh.size());

    SECTION("Warmup produces NaN") {
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("First valid value") {
        // IdxMax for high window [0..4]: max at idx 4 -> offset 0
        // IdxMin for low window [0..4]: min at idx 2 -> offset 2
        // ImxD = (0 - 2) / 5 = -0.4
        CHECK_THAT(ind.line().data()[4], WithinAbs(-0.4, 1e-12));
    }
}

// =============================================================================
// RollingResidual
// =============================================================================

TEST_CASE("RollingResidual on linear data yields ~0 residual", "[indicator][residual][regression]") {
    auto trend = stratforge::test::generate_trend_data(30, 100.0, 2.0);
    auto source = make_line(trend);
    stratforge::RollingResidual ind(source, 10);
    run_indicator(source, ind);

    SECTION("Warmup NaN") {
        for (std::size_t i = 0; i < 9; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("Perfect linear trend has zero residual") {
        for (std::size_t i = 9; i < 30; ++i) {
            INFO("bar " << i << " residual = " << ind.line().data()[i]);
            CHECK_THAT(ind.line().data()[i], WithinAbs(0.0, 1e-10));
        }
    }
}

TEST_CASE("RollingResidual on real data produces non-trivial values", "[indicator][residual][regression]") {
    auto source = make_line(kClose);
    stratforge::RollingResidual ind(source, 5);
    run_indicator(source, ind);

    CHECK_FALSE(std::isnan(ind.line().data()[4]));
    CHECK(ind.line().data()[4] != 0.0);
}

// =============================================================================
// RollingRsquared
// =============================================================================

TEST_CASE("RollingRsquared on linear data yields R²=1", "[indicator][rsquared][regression]") {
    auto trend = stratforge::test::generate_trend_data(30, 100.0, 2.0);
    auto source = make_line(trend);
    stratforge::RollingRsquared ind(source, 10);
    run_indicator(source, ind);

    SECTION("Warmup NaN") {
        for (std::size_t i = 0; i < 9; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("Perfect linear trend has R²=1") {
        for (std::size_t i = 9; i < 30; ++i) {
            INFO("bar " << i);
            CHECK_THAT(ind.line().data()[i], WithinAbs(1.0, 1e-10));
        }
    }
}

TEST_CASE("RollingRsquared on real data is in [0,1]", "[indicator][rsquared][regression]") {
    auto source = make_line(kClose);
    stratforge::RollingRsquared ind(source, 5);
    run_indicator(source, ind);

    for (std::size_t i = 4; i < kClose.size(); ++i) {
        INFO("bar " << i);
        CHECK(ind.line().data()[i] >= -1e-12);
        CHECK(ind.line().data()[i] <= 1.0 + 1e-12);
    }
}

// =============================================================================
// CntP / CntN / CntD
// =============================================================================

TEST_CASE("CntP counts fraction of up bars", "[indicator][cntp][regression]") {
    auto source = make_line(kClose);
    stratforge::CntP ind(source, 5);
    run_indicator(source, ind);

    REQUIRE(ind.line().size() == kClose.size());

    SECTION("Warmup NaN for first period bars") {
        for (std::size_t i = 0; i < 5; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("Manual check at index 5") {
        // Bars 1..5: compare each to previous
        // 44.09<44.34(dn), 43.61<44.09(dn), 44.33>43.61(up), 44.83>44.33(up), 45.10>44.83(up)
        // up_count=3, fraction=3/5=0.6
        CHECK_THAT(ind.line().data()[5], WithinAbs(0.6, 1e-12));
    }
}

TEST_CASE("CntN counts fraction of down bars", "[indicator][cntn][regression]") {
    auto source = make_line(kClose);
    stratforge::CntN ind(source, 5);
    run_indicator(source, ind);

    SECTION("Manual check at index 5") {
        // Same window as CntP test: dn=2, fraction=2/5=0.4
        CHECK_THAT(ind.line().data()[5], WithinAbs(0.4, 1e-12));
    }
}

TEST_CASE("CntD equals CntP minus CntN", "[indicator][cntd][regression]") {
    auto source = make_line(kClose);
    stratforge::CntP cntp(source, 5);
    stratforge::CntN cntn(source, 5);
    stratforge::CntD cntd(source, 5);
    run_indicator(source, cntp);

    source.home();
    run_indicator(source, cntn);

    source.home();
    run_indicator(source, cntd);

    for (std::size_t i = 5; i < kClose.size(); ++i) {
        INFO("bar " << i);
        CHECK_THAT(cntd.line().data()[i],
                   WithinAbs(cntp.line().data()[i] - cntn.line().data()[i], 1e-12));
    }
}

// =============================================================================
// SumP / SumNegReturn / SumD
// =============================================================================

TEST_CASE("SumP sums positive returns", "[indicator][sump][regression]") {
    auto source = make_line(kClose);
    stratforge::SumP ind(source, 5);
    run_indicator(source, ind);

    SECTION("Warmup NaN") {
        for (std::size_t i = 0; i < 5; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("Manual check at index 5") {
        // Returns in window [1..5]: -0.25, -0.48, +0.72, +0.50, +0.27
        // Positive sum = 0.72 + 0.50 + 0.27 = 1.49
        double expected = (44.33 - 43.61) + (44.83 - 44.33) + (45.10 - 44.83);
        CHECK_THAT(ind.line().data()[5], WithinRel(expected, 1e-10));
    }

    SECTION("Non-negative output") {
        for (std::size_t i = 5; i < kClose.size(); ++i) {
            CHECK(ind.line().data()[i] >= -1e-12);
        }
    }
}

TEST_CASE("SumNegReturn sums negative returns", "[indicator][sum_neg_return][regression]") {
    auto source = make_line(kClose);
    stratforge::SumNegReturn ind(source, 5);
    run_indicator(source, ind);

    SECTION("Manual check at index 5") {
        // Negative returns: -0.25, -0.48
        double expected = (44.09 - 44.34) + (43.61 - 44.09);
        CHECK_THAT(ind.line().data()[5], WithinRel(expected, 1e-10));
    }

    SECTION("Non-positive output") {
        for (std::size_t i = 5; i < kClose.size(); ++i) {
            CHECK(ind.line().data()[i] <= 1e-12);
        }
    }
}

TEST_CASE("SumD equals SumP minus SumNegReturn", "[indicator][sumd][regression]") {
    auto source = make_line(kClose);
    stratforge::SumP sump(source, 5);
    stratforge::SumNegReturn sumnr(source, 5);
    stratforge::SumD sumd(source, 5);

    run_indicator(source, sump);
    source.home();
    run_indicator(source, sumnr);
    source.home();
    run_indicator(source, sumd);

    for (std::size_t i = 5; i < kClose.size(); ++i) {
        INFO("bar " << i);
        CHECK_THAT(sumd.line().data()[i],
                   WithinAbs(sump.line().data()[i] - sumnr.line().data()[i], 1e-10));
    }
}

// =============================================================================
// VSumP / VSumN / VSumD
// =============================================================================

TEST_CASE("VSumP sums volume on up bars", "[indicator][vsump][regression]") {
    auto close = make_line(kClose);
    auto volume = make_line(kVolume);
    stratforge::VSumP ind(close, volume, 5);
    run_indicator_cv(close, volume, ind);

    SECTION("Warmup NaN") {
        for (std::size_t i = 0; i < 5; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("Manual check at index 5") {
        // Bars 1..5: up at bars 3,4,5 (indices 3,4,5)
        // Volumes: 110000, 130000, 140000
        double expected = 110000.0 + 130000.0 + 140000.0;
        CHECK_THAT(ind.line().data()[5], WithinAbs(expected, 1e-6));
    }
}

TEST_CASE("VSumN sums volume on down bars", "[indicator][vsumn][regression]") {
    auto close = make_line(kClose);
    auto volume = make_line(kVolume);
    stratforge::VSumN ind(close, volume, 5);
    run_indicator_cv(close, volume, ind);

    SECTION("Manual check at index 5") {
        // Bars 1..5: down at bars 1,2 (indices 1,2)
        // Volumes: 120000, 95000
        double expected = 120000.0 + 95000.0;
        CHECK_THAT(ind.line().data()[5], WithinAbs(expected, 1e-6));
    }
}

TEST_CASE("VSumD equals VSumP minus VSumN", "[indicator][vsumd][regression]") {
    auto close = make_line(kClose);
    auto volume = make_line(kVolume);
    stratforge::VSumP vsump(close, volume, 5);
    run_indicator_cv(close, volume, vsump);

    close.home();
    volume.home();
    stratforge::VSumN vsumn(close, volume, 5);
    run_indicator_cv(close, volume, vsumn);

    close.home();
    volume.home();
    stratforge::VSumD vsumd(close, volume, 5);
    run_indicator_cv(close, volume, vsumd);

    for (std::size_t i = 5; i < kClose.size(); ++i) {
        INFO("bar " << i);
        CHECK_THAT(vsumd.line().data()[i],
                   WithinAbs(vsump.line().data()[i] - vsumn.line().data()[i], 1e-6));
    }
}

// =============================================================================
// WVMA
// =============================================================================

TEST_CASE("WVMA produces non-negative std dev values", "[indicator][wvma][regression]") {
    auto close = make_line(kClose);
    auto volume = make_line(kVolume);
    stratforge::WVMA ind(close, volume, 5);
    run_indicator_cv(close, volume, ind);

    SECTION("Warmup NaN") {
        for (std::size_t i = 0; i < 5; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("Non-negative values") {
        for (std::size_t i = 5; i < kClose.size(); ++i) {
            CHECK(ind.line().data()[i] >= 0.0);
        }
    }

    SECTION("Constant close yields zero WVMA") {
        std::vector<double> flat(20, 100.0);
        std::vector<double> vol(20, 1000.0);
        auto c = make_line(flat);
        auto v = make_line(vol);
        stratforge::WVMA wvma(c, v, 5);
        run_indicator_cv(c, v, wvma);
        for (std::size_t i = 5; i < 20; ++i) {
            CHECK_THAT(wvma.line().data()[i], WithinAbs(0.0, 1e-12));
        }
    }
}

// =============================================================================
// Alpha158VwapRatio
// =============================================================================

TEST_CASE("Alpha158VwapRatio computes TP/prev_close", "[indicator][vwap][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha158VwapRatio ind(h, l, c);
    run_indicator_hlc(h, l, c, ind);

    SECTION("First bar is NaN (no previous close)") {
        REQUIRE(std::isnan(ind.line().data()[0]));
    }

    SECTION("Manual check at index 1") {
        double tp = (kHigh[1] + kLow[1] + kClose[1]) / 3.0;
        double expected = tp / kClose[0];
        CHECK_THAT(ind.line().data()[1], WithinRel(expected, 1e-12));
    }
}

// =============================================================================
// Quantile (existing, verify Alpha158 parameterization q=0.2)
// =============================================================================

TEST_CASE("Quantile with q=0.2 matches Alpha158 qtld", "[indicator][quantile][regression]") {
    auto source = make_line(kClose);
    stratforge::Quantile ind(source, 5, 0.2);
    run_indicator(source, ind);

    SECTION("Warmup") {
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("First valid value matches manual interpolation") {
        // Window [0..4]: 44.34, 44.09, 43.61, 44.33, 44.83
        // Sorted: 43.61, 44.09, 44.33, 44.34, 44.83
        // pos = 0.2 * 4 = 0.8, lower=0, upper=1, frac=0.8
        // result = 43.61*(1-0.8) + 44.09*0.8 = 43.61*0.2 + 44.09*0.8
        double expected = 43.61 * 0.2 + 44.09 * 0.8;
        CHECK_THAT(ind.line().data()[4], WithinRel(expected, 1e-10));
    }
}

// =============================================================================
// PercentRank (existing, verify Alpha158 "Rank" semantics)
// =============================================================================

TEST_CASE("PercentRank matches Alpha158 Rank semantics", "[indicator][pctrank][regression]") {
    auto source = make_line(kClose);
    stratforge::PercentRank ind(source, 5);
    run_indicator(source, ind);

    SECTION("Warmup") {
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(std::isnan(ind.line().data()[i]));
        }
    }

    SECTION("Output in [0, 1]") {
        for (std::size_t i = 4; i < kClose.size(); ++i) {
            CHECK(ind.line().data()[i] >= 0.0);
            CHECK(ind.line().data()[i] <= 1.0);
        }
    }

    SECTION("Manual check at index 4") {
        // Window [0..4]: 44.34, 44.09, 43.61, 44.33, 44.83
        // Current = 44.83. Count below: 44.34, 44.09, 43.61, 44.33 = 4
        // Rank = 4/5 = 0.8
        CHECK_THAT(ind.line().data()[4], WithinAbs(0.8, 1e-12));
    }
}
