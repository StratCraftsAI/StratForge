#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/stratforge.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <limits>
#include <vector>

using stratforge::test::make_line;
using stratforge::test::make_ohlcv;
using stratforge::test::run_indicator;
using stratforge::test::run_indicator_cv;
using stratforge::test::run_indicator_hlc;
using stratforge::test::run_indicator_ohlcv;
using stratforge::test::run_indicator_full_ohlcv;
using stratforge::test::run_indicator_ohlc;
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

const std::vector<double> kOpen = {
    44.25, 44.34, 44.05, 43.70, 44.40, 44.90, 45.15, 45.50,
    45.90, 46.05, 45.95, 46.00, 45.65, 46.30, 46.25, 46.05,
    46.10, 46.40, 46.20, 45.70, 46.25, 46.20, 45.80, 46.40
};

const std::vector<double> kVolume = {
    100000, 120000, 95000, 110000, 130000, 140000, 115000, 125000,
    105000, 135000, 110000, 98000, 142000, 108000, 100000, 115000,
    128000, 112000, 135000, 118000, 102000, 138000, 125000, 105000
};

} // namespace

// --- Alpha#101: (close - open) / ((high - low) + 0.001) ---

TEST_CASE("Alpha101_101 computes bar body ratio", "[indicator][alpha101][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha101_101 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 1);

    const double expected0 = (44.34 - 44.25) / ((44.52 - 44.10) + 0.001);
    CHECK_THAT(alpha.line().data()[0], WithinRel(expected0, 1e-12));

    const double expected5 = (45.10 - 44.90) / ((45.32 - 44.95) + 0.001);
    CHECK_THAT(alpha.line().data()[5], WithinRel(expected5, 1e-12));
}

// --- Alpha#054: (-1*(low-close)*open^5) / ((low-high)*close^5) ---

TEST_CASE("Alpha101_054 computes power ratio", "[indicator][alpha101][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha101_054 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());

    const double o0 = 44.25, h0 = 44.52, l0 = 44.10, c0 = 44.34;
    const double expected0 = (-1.0 * (l0 - c0) * std::pow(o0, 5.0)) /
                             ((l0 - h0) * std::pow(c0, 5.0));
    CHECK_THAT(alpha.line().data()[0], WithinRel(expected0, 1e-10));
}

// --- Alpha#041: sqrt(high*low) - vwap ---

TEST_CASE("Alpha101_041 computes geometric mean minus vwap", "[indicator][alpha101][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha101_041 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());

    const double h0 = 44.52, l0 = 44.10, c0 = 44.34;
    const double vwap0 = (h0 + l0 + c0) / 3.0;
    const double expected0 = std::sqrt(h0 * l0) - vwap0;
    CHECK_THAT(alpha.line().data()[0], WithinRel(expected0, 1e-12));
}

// --- Alpha#012: sign(delta(volume,1)) * (-1*delta(close,1)) ---

TEST_CASE("Alpha101_012 computes sign-volume delta cross", "[indicator][alpha101][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha101_012 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    REQUIRE(std::isnan(alpha.line().data()[0]));

    const double dv1 = 120000.0 - 100000.0;
    const double dc1 = 44.09 - 44.34;
    const double expected1 = stratforge::alpha101::sign(dv1) * (-1.0 * dc1);
    CHECK_THAT(alpha.line().data()[1], WithinRel(expected1, 1e-12));
}

// --- Alpha#006: -1 * correlation(open, volume, 10) ---

TEST_CASE("Alpha101_006 computes negated open-volume correlation", "[indicator][alpha101][regression]") {
    auto o = make_line(kOpen);
    auto v = make_line(kVolume);
    stratforge::Alpha101_006 alpha(o, v);
    run_indicator_cv(o, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    for (std::size_t i = 0; i < 9; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[9]));

    SECTION("Verify against manual Pearson correlation") {
        const double* px = kOpen.data();
        const double* py = kVolume.data();
        double mx = 0, my = 0;
        for (int i = 0; i < 10; ++i) { mx += px[i]; my += py[i]; }
        mx /= 10; my /= 10;
        double cov = 0, vx = 0, vy = 0;
        for (int i = 0; i < 10; ++i) {
            double dx = px[i] - mx, dy = py[i] - my;
            cov += dx * dy; vx += dx * dx; vy += dy * dy;
        }
        const double corr = cov / std::sqrt(vx * vy);
        CHECK_THAT(alpha.line().data()[9], WithinRel(-1.0 * corr, 1e-10));
    }
}

// --- Alpha#023: (SMA(high,20) < high) ? -1*delta(high,2) : 0 ---

TEST_CASE("Alpha101_023 conditional high breakout", "[indicator][alpha101][regression]") {
    auto h = make_line(kHigh);
    stratforge::Alpha101_023 alpha(h);
    run_indicator(h, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    for (std::size_t i = 0; i < 19; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    SECTION("SMA(high,20) vs high at bar 19") {
        double sum = 0;
        for (int i = 0; i < 20; ++i) sum += kHigh[i];
        const double sma20 = sum / 20.0;
        const double h19 = kHigh[19];
        if (sma20 < h19) {
            const double expected = -1.0 * (kHigh[19] - kHigh[17]);
            CHECK_THAT(alpha.line().data()[19], WithinRel(expected, 1e-10));
        } else {
            CHECK_THAT(alpha.line().data()[19], WithinAbs(0.0, 1e-12));
        }
    }
}

// --- Alpha#009: conditional on ts_min/ts_max of delta(close,1) ---

TEST_CASE("Alpha101_009 conditional momentum", "[indicator][alpha101][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha101_009 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    for (std::size_t i = 0; i < 5; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[5]));

    SECTION("Verify sign logic at bar 5") {
        std::vector<double> deltas;
        for (int i = 1; i <= 5; ++i)
            deltas.push_back(kClose[i] - kClose[i - 1]);

        const double mn = *std::min_element(deltas.begin(), deltas.end());
        const double mx = *std::max_element(deltas.begin(), deltas.end());
        const double d1_5 = kClose[5] - kClose[4];

        double expected;
        if (0.0 < mn) expected = d1_5;
        else if (mx < 0.0) expected = d1_5;
        else expected = -1.0 * d1_5;

        CHECK_THAT(alpha.line().data()[5], WithinRel(expected, 1e-10));
    }
}

// --- Alpha#053: -1 * delta(((close-low)-(high-close))/(close-low), 9) ---

TEST_CASE("Alpha101_053 inner ratio delta", "[indicator][alpha101][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha101_053 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    for (std::size_t i = 0; i < 9; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    SECTION("Verify at bar 9") {
        auto inner = [](double c, double l, double h) {
            double cl = c - l;
            return (cl == 0.0) ? 0.0 : ((cl - (h - c)) / cl);
        };
        const double v9 = inner(kClose[9], kLow[9], kHigh[9]);
        const double v0 = inner(kClose[0], kLow[0], kHigh[0]);
        CHECK_THAT(alpha.line().data()[9], WithinRel(-1.0 * (v9 - v0), 1e-10));
    }
}

// --- Alpha#046/049/051: momentum slope conditionals ---

TEST_CASE("Alpha101_046 momentum slope conditional", "[indicator][alpha101][regression]") {
    std::vector<double> data(25);
    for (int i = 0; i < 25; ++i) data[i] = 100.0 + i * 0.5;
    auto c = make_line(data);
    stratforge::Alpha101_046 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == 25);
    for (std::size_t i = 0; i < 20; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha101_049 momentum slope conditional -0.1", "[indicator][alpha101][regression]") {
    std::vector<double> data(25);
    for (int i = 0; i < 25; ++i) data[i] = 100.0 + i * 0.5;
    auto c = make_line(data);
    stratforge::Alpha101_049 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == 25);
    for (std::size_t i = 0; i < 20; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    SECTION("Linear uptrend slope > -0.1 → falls through to -1*delta(c,1)") {
        const double d20 = data[0], d10 = data[10], cv = data[20];
        const double slope = ((d20 - d10) / 10.0) - ((d10 - cv) / 10.0);
        INFO("slope = " << slope);
        if (slope < -0.1) {
            CHECK_THAT(alpha.line().data()[20], WithinAbs(1.0, 1e-12));
        } else {
            const double expected = -1.0 * (cv - data[19]);
            CHECK_THAT(alpha.line().data()[20], WithinRel(expected, 1e-10));
        }
    }
}

TEST_CASE("Alpha101_051 momentum slope conditional -0.05", "[indicator][alpha101][regression]") {
    std::vector<double> data(25);
    for (int i = 0; i < 25; ++i) data[i] = 100.0 + i * 0.5;
    auto c = make_line(data);
    stratforge::Alpha101_051 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == 25);
    CHECK_FALSE(std::isnan(alpha.line().data()[20]));
}

// --- Alpha#021: conditional SMA/stddev/volume ---

TEST_CASE("Alpha101_021 SMA stddev volume conditional", "[indicator][alpha101][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha101_021 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    for (std::size_t i = 0; i < 19; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[19]));

    SECTION("Output is always -1 or 1") {
        for (std::size_t i = 19; i < kClose.size(); ++i) {
            const double val = alpha.line().data()[i];
            CHECK((val == 1.0 || val == -1.0));
        }
    }
}

// --- Alpha#024: conditional on delta(SMA(close,100),100)/delay(close,100) ---

TEST_CASE("Alpha101_024 long-window momentum conditional", "[indicator][alpha101][regression]") {
    std::vector<double> data(210);
    for (int i = 0; i < 210; ++i) data[i] = 100.0 + 0.1 * i;
    auto c = make_line(data);
    stratforge::Alpha101_024 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == 210);
    for (std::size_t i = 0; i < 199; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[199]));
}

// --- Alpha#026: -1 * ts_max(correlation(ts_rank(vol,5), ts_rank(high,5), 5), 3) ---

TEST_CASE("Alpha101_026 rank correlation rolling max", "[indicator][alpha101][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha101_026 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());

    bool found_valid = false;
    for (std::size_t i = 0; i < kClose.size(); ++i) {
        if (!std::isnan(alpha.line().data()[i])) {
            found_valid = true;
            CHECK(alpha.line().data()[i] <= 0.0);
            CHECK(alpha.line().data()[i] >= -1.0);
            break;
        }
    }
    CHECK(found_valid);
}

// --- Alpha#028: scale(correlation(adv20, low, 5) + (high+low)/2 - close) ---

TEST_CASE("Alpha101_028 scaled adv correlation", "[indicator][alpha101][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha101_028 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    for (std::size_t i = 0; i < 23; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[23]));
}

// --- Alpha#035: ts_rank products ---

TEST_CASE("Alpha101_035 ts_rank volume-price cross", "[indicator][alpha101][regression]") {
    std::vector<double> c(40), h(40), l(40), v(40);
    for (int i = 0; i < 40; ++i) {
        c[i] = 100.0 + std::sin(i * 0.3) * 5.0;
        h[i] = c[i] + 1.0;
        l[i] = c[i] - 1.0;
        v[i] = 100000.0 + i * 1000.0;
    }
    auto lc = make_line(c);
    auto lh = make_line(h);
    auto ll = make_line(l);
    auto lv = make_line(v);
    stratforge::Alpha101_035 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == 40);
    for (std::size_t i = 0; i < 32; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[32]));
}

// --- Alpha#043: ts_rank(volume/adv20, 20) * ts_rank(-1*delta(close,7), 8) ---

TEST_CASE("Alpha101_043 volume ratio rank cross", "[indicator][alpha101][regression]") {
    std::vector<double> c(35), v(35);
    for (int i = 0; i < 35; ++i) {
        c[i] = 100.0 + i * 0.3;
        v[i] = 100000.0 + i * 2000.0;
    }
    auto lc = make_line(c);
    auto lv = make_line(v);
    stratforge::Alpha101_043 alpha(lc, lv);
    run_indicator_cv(lc, lv, alpha);

    REQUIRE(alpha.line().size() == 35);
    for (std::size_t i = 0; i < 26; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[26]));
}

// --- Alpha#084: SignedPower(ts_rank(vwap - ts_max(vwap,15), 20), delta(close,5)) ---

TEST_CASE("Alpha101_084 signed power rank", "[indicator][alpha101][regression]") {
    std::vector<double> c(40), h(40), l(40);
    for (int i = 0; i < 40; ++i) {
        c[i] = 100.0 + i * 0.2;
        h[i] = c[i] + 0.5;
        l[i] = c[i] - 0.5;
    }
    auto lc = make_line(c);
    auto lh = make_line(h);
    auto ll = make_line(l);
    stratforge::Alpha101_084 alpha(lh, ll, lc);
    run_indicator_hlc(lh, ll, lc, alpha);

    REQUIRE(alpha.line().size() == 40);
    for (std::size_t i = 0; i < 33; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[33]));
}

// --- Alpha#007: conditional adv20/volume with ts_rank ---

TEST_CASE("Alpha101_007 volume spike conditional", "[indicator][alpha101][regression]") {
    std::vector<double> c(75), v(75);
    for (int i = 0; i < 75; ++i) {
        c[i] = 100.0 + std::sin(i * 0.1) * 3.0;
        v[i] = 100000.0 + i * 500.0;
    }
    v[70] = 500000.0;
    auto lc = make_line(c);
    auto lv = make_line(v);
    stratforge::Alpha101_007 alpha(lc, lv);
    run_indicator_cv(lc, lv, alpha);

    REQUIRE(alpha.line().size() == 75);
    for (std::size_t i = 0; i < 66; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));

    CHECK_FALSE(std::isnan(alpha.line().data()[66]));
}

// --- Alpha#032: scale + correlation with vwap ---

TEST_CASE("Alpha101_032 vwap correlation scale", "[indicator][alpha101][regression]") {
    std::vector<double> c(250), h(250), l(250), v(250);
    for (int i = 0; i < 250; ++i) {
        c[i] = 100.0 + i * 0.05;
        h[i] = c[i] + 1.0;
        l[i] = c[i] - 1.0;
        v[i] = 100000.0;
    }
    auto lh = make_line(h);
    auto ll = make_line(l);
    auto lc = make_line(c);
    auto lv = make_line(v);
    stratforge::Alpha101_032 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == 250);
    for (std::size_t i = 0; i < 234; ++i) {
        REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    CHECK_FALSE(std::isnan(alpha.line().data()[234]));
}

// --- Compile smoke: all 20 Batch A alphas instantiate ---

TEST_CASE("Alpha101 Batch A compile smoke", "[indicator][alpha101][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);

    stratforge::Alpha101_006 a006(o, v);
    stratforge::Alpha101_007 a007(c, v);
    stratforge::Alpha101_009 a009(c);
    stratforge::Alpha101_012 a012(c, v);
    stratforge::Alpha101_021 a021(c, v);
    stratforge::Alpha101_023 a023(h);
    stratforge::Alpha101_024 a024(c);
    stratforge::Alpha101_026 a026(h, v);
    stratforge::Alpha101_028 a028(h, l, c, v);
    stratforge::Alpha101_032 a032(h, l, c, v);
    stratforge::Alpha101_035 a035(h, l, c, v);
    stratforge::Alpha101_041 a041(h, l, c);
    stratforge::Alpha101_043 a043(c, v);
    stratforge::Alpha101_046 a046(c);
    stratforge::Alpha101_049 a049(c);
    stratforge::Alpha101_051 a051(c);
    stratforge::Alpha101_053 a053(h, l, c);
    stratforge::Alpha101_054 a054(o, h, l, c);
    stratforge::Alpha101_084 a084(h, l, c);
    stratforge::Alpha101_101 a101(o, h, l, c);

    CHECK(a006.minimum_period() == 10);
    CHECK(a007.minimum_period() == 67);
    CHECK(a009.minimum_period() == 6);
    CHECK(a012.minimum_period() == 2);
    CHECK(a021.minimum_period() == 20);
    CHECK(a023.minimum_period() == 20);
    CHECK(a024.minimum_period() == 200);
    CHECK(a026.minimum_period() == 12);
    CHECK(a028.minimum_period() == 24);
    CHECK(a032.minimum_period() == 235);
    CHECK(a035.minimum_period() == 33);
    CHECK(a041.minimum_period() == 1);
    CHECK(a043.minimum_period() == 27);
    CHECK(a046.minimum_period() == 21);
    CHECK(a049.minimum_period() == 21);
    CHECK(a051.minimum_period() == 21);
    CHECK(a053.minimum_period() == 10);
    CHECK(a054.minimum_period() == 1);
    CHECK(a084.minimum_period() == 34);
    CHECK(a101.minimum_period() == 1);
}
TEST_CASE("Alpha101_001 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 45;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_001 alpha(lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 25);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 24uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 24; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_002 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 29;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_002 alpha(lo, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 9);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 8uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 8; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_003 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 30;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_003 alpha(lo, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 10);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 9uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 9; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_004 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 29;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_004 alpha(ll);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 9);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 8uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 8; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_005 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 30;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_005 alpha(lo, lh, ll, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 10);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 9uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 9; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_008 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 36;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_008 alpha(lo, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 16);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 15uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 15; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_010 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 26;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_010 alpha(lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 6);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 5uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 5; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_011 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 24;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_011 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 4);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 3uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 3; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_022 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 40;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_022 alpha(lh, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 20);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 19uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 19; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_025 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 40;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_025 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 20);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 19uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 19; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_027 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 28;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_027 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 8);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 7uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 7; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_029 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 300;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_029 alpha(lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 12);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 11uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 11; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_030 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 40;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_030 alpha(lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 20);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 19uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 19; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_031 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 51;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_031 alpha(ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 31);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 30uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 30; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_033 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 21;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_033 alpha(lo, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 1);

    SECTION("no warmup NaN (minimum_period == 1)") {
        CHECK_FALSE(std::isnan(alpha.line().data()[0]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 0; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_034 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 26;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_034 alpha(lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 6);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 5uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 5; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_038 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 30;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_038 alpha(lo, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 10);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 9uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 9; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_040 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 30;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_040 alpha(lh, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 10);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 9uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 9; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_042 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 21;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_042 alpha(lh, ll, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 1);

    SECTION("no warmup NaN (minimum_period == 1)") {
        CHECK_FALSE(std::isnan(alpha.line().data()[0]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 0; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_044 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 25;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_044 alpha(lh, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 5);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 4uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 4; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_045 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 45;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_045 alpha(lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 25);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 24uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 24; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_062 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 72;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_062 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 52);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 51uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 51; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_065 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 93;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_065 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 73);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 72uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 72; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_066 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 37;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_066 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 17);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 16uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 16; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_068 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 58;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_068 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 38);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 37uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 37; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_072 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 200;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_072 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 58);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 57uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 57; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_073 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 42;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_073 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 22);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 21uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 21; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_074 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 100;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_074 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 80);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 79uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 79; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_075 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 82;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_075 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 62);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 61uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 61; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_077 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 65;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_077 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 45);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 44uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 44; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_078 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 85;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_078 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 65);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 64uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 64; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_081 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 100;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_081 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 80);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 79uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 79; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_083 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 27;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_083 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 7);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 6uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 6; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_085 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 59;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_085 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 39);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 38uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 38; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_086 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 78;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_086 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 58);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 57uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 57; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_088 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 115;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_088 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 95);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 94uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 94; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_092 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 70;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_092 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 50);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 49uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 49; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_094 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 102;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_094 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 82);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 81uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 81; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_095 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 101;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_095 alpha(lo, lh, ll, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 81);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 80uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 80; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_098 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 76;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_098 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 56);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 55uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 55; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_099 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 107;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_099 alpha(lh, ll, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 87);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 86uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 86; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_013 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 277;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_013 alpha(lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 257);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 256uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 256; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_014 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 276;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_014 alpha(lo, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 256);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 255uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 255; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_015 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 279;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_015 alpha(lh, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 259);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 258uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 258; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_016 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 277;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_016 alpha(lh, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 257);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 256uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 256; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_017 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 295;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_017 alpha(lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 275);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 274uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 274; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_018 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 282;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_018 alpha(lo, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 262);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 261uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 261; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_019 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 523;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_019 alpha(lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 503);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 502uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 502; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_020 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 273;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_020 alpha(lo, lh, ll, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 253);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 252uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 252; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_036 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 220;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_036 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 200);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 199uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 199; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_037 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 222;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_037 alpha(lo, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 202);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 201uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 201; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_039 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 271;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_039 alpha(lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 251);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 250uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 250; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_047 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 273;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_047 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 253);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 252uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 252; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_050 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 281;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_050 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 261);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 260uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 260; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_052 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 265;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_052 alpha(ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 245);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 244uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 244; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_055 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 284;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_055 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 264);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 263uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 263; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_056 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 276;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_056 alpha(lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 256);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 255uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 255; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_057 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 304;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_057 alpha(lh, ll, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 284);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 283uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 283; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_060 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 282;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_060 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 262);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 261uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 261; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_061 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 290;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_061 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 270);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 269uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 269; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_064 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 168;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_064 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 148);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 147uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 147; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_071 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 233;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_071 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 213);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 212uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 212; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101_096 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 123;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_096 alpha(lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    CHECK(alpha.minimum_period() == 103);

    SECTION("warmup produces NaN") {
        for (std::size_t i = 0; i < 102uz; ++i)
            REQUIRE(std::isnan(alpha.line().data()[i]));
    }

    SECTION("post-warmup produces finite values") {
        std::size_t valid_count = 0;
        for (std::size_t i = 102; i < N; ++i) {
            if (!std::isnan(alpha.line().data()[i])) ++valid_count;
        }
        CHECK(valid_count > 0);
    }
}

TEST_CASE("Alpha101 Batch B compile smoke", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 30;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);

    stratforge::Alpha101_001 a001(lc);
    stratforge::Alpha101_002 a002(lo, lc, lv);
    stratforge::Alpha101_003 a003(lo, lv);
    stratforge::Alpha101_004 a004(ll);
    stratforge::Alpha101_005 a005(lo, lh, ll, lc);
    stratforge::Alpha101_008 a008(lo, lc);
    stratforge::Alpha101_010 a010(lc);
    stratforge::Alpha101_011 a011(lh, ll, lc, lv);
    stratforge::Alpha101_013 a013(lc, lv);
    stratforge::Alpha101_014 a014(lo, lc, lv);
    stratforge::Alpha101_015 a015(lh, lv);
    stratforge::Alpha101_016 a016(lh, lv);
    stratforge::Alpha101_017 a017(lc, lv);
    stratforge::Alpha101_018 a018(lo, lc);
    stratforge::Alpha101_019 a019(lc);
    stratforge::Alpha101_020 a020(lo, lh, ll, lc);
    stratforge::Alpha101_022 a022(lh, lc, lv);
    stratforge::Alpha101_025 a025(lh, ll, lc, lv);
    stratforge::Alpha101_027 a027(lh, ll, lc, lv);
    stratforge::Alpha101_029 a029(lc);
    stratforge::Alpha101_030 a030(lc, lv);
    stratforge::Alpha101_031 a031(ll, lc, lv);
    stratforge::Alpha101_033 a033(lo, lc);
    stratforge::Alpha101_034 a034(lc);
    stratforge::Alpha101_036 a036(lo, lh, ll, lc, lv);
    stratforge::Alpha101_037 a037(lo, lc);
    stratforge::Alpha101_038 a038(lo, lc);
    stratforge::Alpha101_039 a039(lc, lv);
    stratforge::Alpha101_040 a040(lh, lv);
    stratforge::Alpha101_042 a042(lh, ll, lc);
    stratforge::Alpha101_044 a044(lh, lv);
    stratforge::Alpha101_045 a045(lc, lv);
    stratforge::Alpha101_047 a047(lh, ll, lc, lv);
    stratforge::Alpha101_050 a050(lh, ll, lc, lv);
    stratforge::Alpha101_052 a052(ll, lc, lv);
    stratforge::Alpha101_055 a055(lh, ll, lc, lv);
    stratforge::Alpha101_056 a056(lc, lv);
    stratforge::Alpha101_057 a057(lh, ll, lc);
    stratforge::Alpha101_060 a060(lh, ll, lc, lv);
    stratforge::Alpha101_061 a061(lh, ll, lc, lv);
    stratforge::Alpha101_062 a062(lo, lh, ll, lc, lv);
    stratforge::Alpha101_064 a064(lo, lh, ll, lc, lv);
    stratforge::Alpha101_065 a065(lo, lh, ll, lc, lv);
    stratforge::Alpha101_066 a066(lo, lh, ll, lc, lv);
    stratforge::Alpha101_068 a068(lh, ll, lc, lv);
    stratforge::Alpha101_071 a071(lo, lh, ll, lc, lv);
    stratforge::Alpha101_072 a072(lh, ll, lc, lv);
    stratforge::Alpha101_073 a073(lo, lh, ll, lc, lv);
    stratforge::Alpha101_074 a074(lh, ll, lc, lv);
    stratforge::Alpha101_075 a075(lh, ll, lc, lv);
    stratforge::Alpha101_077 a077(lh, ll, lc, lv);
    stratforge::Alpha101_078 a078(lh, ll, lc, lv);
    stratforge::Alpha101_081 a081(lh, ll, lc, lv);
    stratforge::Alpha101_083 a083(lh, ll, lc, lv);
    stratforge::Alpha101_085 a085(lh, ll, lc, lv);
    stratforge::Alpha101_086 a086(lo, lh, ll, lc, lv);
    stratforge::Alpha101_088 a088(lo, lh, ll, lc, lv);
    stratforge::Alpha101_092 a092(lo, lh, ll, lc, lv);
    stratforge::Alpha101_094 a094(lh, ll, lc, lv);
    stratforge::Alpha101_095 a095(lo, lh, ll, lv);
    stratforge::Alpha101_096 a096(lh, ll, lc, lv);
    stratforge::Alpha101_098 a098(lo, lh, ll, lc, lv);
    stratforge::Alpha101_099 a099(lh, ll, lv);

    CHECK(a001.minimum_period() == 25);
    CHECK(a002.minimum_period() == 9);
    CHECK(a003.minimum_period() == 10);
    CHECK(a004.minimum_period() == 9);
    CHECK(a005.minimum_period() == 10);
    CHECK(a008.minimum_period() == 16);
    CHECK(a010.minimum_period() == 6);
    CHECK(a011.minimum_period() == 4);
    CHECK(a013.minimum_period() == 257);
    CHECK(a014.minimum_period() == 256);
    CHECK(a015.minimum_period() == 259);
    CHECK(a016.minimum_period() == 257);
    CHECK(a017.minimum_period() == 275);
    CHECK(a018.minimum_period() == 262);
    CHECK(a019.minimum_period() == 503);
    CHECK(a020.minimum_period() == 253);
    CHECK(a022.minimum_period() == 20);
    CHECK(a025.minimum_period() == 20);
    CHECK(a027.minimum_period() == 8);
    CHECK(a029.minimum_period() == 12);
    CHECK(a030.minimum_period() == 20);
    CHECK(a031.minimum_period() == 31);
    CHECK(a033.minimum_period() == 1);
    CHECK(a034.minimum_period() == 6);
    CHECK(a036.minimum_period() == 200);
    CHECK(a037.minimum_period() == 202);
    CHECK(a038.minimum_period() == 10);
    CHECK(a039.minimum_period() == 251);
    CHECK(a040.minimum_period() == 10);
    CHECK(a042.minimum_period() == 1);
    CHECK(a044.minimum_period() == 5);
    CHECK(a045.minimum_period() == 25);
    CHECK(a047.minimum_period() == 253);
    CHECK(a050.minimum_period() == 261);
    CHECK(a052.minimum_period() == 245);
    CHECK(a055.minimum_period() == 264);
    CHECK(a056.minimum_period() == 256);
    CHECK(a057.minimum_period() == 284);
    CHECK(a060.minimum_period() == 262);
    CHECK(a061.minimum_period() == 270);
    CHECK(a062.minimum_period() == 52);
    CHECK(a064.minimum_period() == 148);
    CHECK(a065.minimum_period() == 73);
    CHECK(a066.minimum_period() == 17);
    CHECK(a068.minimum_period() == 38);
    CHECK(a071.minimum_period() == 213);
    CHECK(a072.minimum_period() == 58);
    CHECK(a073.minimum_period() == 22);
    CHECK(a074.minimum_period() == 80);
    CHECK(a075.minimum_period() == 62);
    CHECK(a077.minimum_period() == 45);
    CHECK(a078.minimum_period() == 65);
    CHECK(a081.minimum_period() == 80);
    CHECK(a083.minimum_period() == 7);
    CHECK(a085.minimum_period() == 39);
    CHECK(a086.minimum_period() == 58);
    CHECK(a088.minimum_period() == 95);
    CHECK(a092.minimum_period() == 50);
    CHECK(a094.minimum_period() == 82);
    CHECK(a095.minimum_period() == 81);
    CHECK(a096.minimum_period() == 103);
    CHECK(a098.minimum_period() == 56);
    CHECK(a099.minimum_period() == 87);
}

TEST_CASE("Alpha101_033 value check: rank(-(1 - open/close))", "[indicator][alpha101][regression]") {
    auto lo = make_line({100.0, 101.0, 99.0, 102.0, 100.5});
    auto lc = make_line({101.0, 100.0, 100.0, 103.0, 101.0});
    auto lh = make_line({102.0, 102.0, 101.0, 104.0, 102.0});
    auto ll = make_line({99.0, 99.0, 98.0, 101.0, 99.5});
    auto lv = make_line({100000.0, 110000.0, 95000.0, 120000.0, 105000.0});
    stratforge::Alpha101_033 alpha(lo, lc);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == 5);
    CHECK_FALSE(std::isnan(alpha.line().data()[0]));
}

TEST_CASE("Alpha101_042 vwap rank ratio", "[indicator][alpha101][regression]") {
    auto lh = make_line({102.0, 103.0, 101.0, 104.0});
    auto ll = make_line({98.0, 99.0, 97.0, 100.0});
    auto lc = make_line({100.0, 101.0, 99.0, 102.0});
    stratforge::Alpha101_042 alpha(lh, ll, lc);
    run_indicator_hlc(lh, ll, lc, alpha);

    REQUIRE(alpha.line().size() == 4);
    CHECK_FALSE(std::isnan(alpha.line().data()[0]));
}

TEST_CASE("Alpha101_083 ratio delay volume", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 15;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_083 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 6; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[6]));
}

// ============================================================================
// Batch C: Cross-sectional with IndNeutralize (18 factors)
// ============================================================================

TEST_CASE("Alpha101_048 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 260;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_048 alpha(lc);
    run_indicator(lc, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 251; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[251]));
    CHECK(std::isfinite(alpha.line().data()[N - 1]));
}

TEST_CASE("Alpha101_058 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 30;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_058 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 14; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[14]));
}

TEST_CASE("Alpha101_059 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 40;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_059 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 27; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[27]));
}

TEST_CASE("Alpha101_063 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 290;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_063 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 282; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[282]));
}

TEST_CASE("Alpha101_067 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 265;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_067 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 259; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[259]));
}

TEST_CASE("Alpha101_069 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 265;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_069 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 259; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[259]));
}

TEST_CASE("Alpha101_070 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 295;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_070 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 285; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[285]));
}

TEST_CASE("Alpha101_076 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 310;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_076 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 302; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[302]));
}

TEST_CASE("Alpha101_079 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 285;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_079 alpha(lo, lh, ll, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 274; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[274]));
    const double v = alpha.line().data()[274];
    CHECK((v == -1.0 || v == 0.0));
}

TEST_CASE("Alpha101_080 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 270;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_080 alpha(lo, lh, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 261; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[261]));
}

TEST_CASE("Alpha101_082 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 300;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_082 alpha(lo, lc, lv);
    run_indicator_full_ohlcv(lo, lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 288; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[288]));
}

TEST_CASE("Alpha101_087 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 295;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_087 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 284; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[284]));
}

TEST_CASE("Alpha101_089 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 35;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_089 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 27; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[27]));
}

TEST_CASE("Alpha101_090 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 265;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_090 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 259; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[259]));
}

TEST_CASE("Alpha101_091 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 295;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_091 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 283; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[283]));
}

TEST_CASE("Alpha101_093 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 310;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_093 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 299; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[299]));
}

TEST_CASE("Alpha101_097 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 320;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_097 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 309; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[309]));
}

TEST_CASE("Alpha101_100 warmup and output", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 290;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);
    stratforge::Alpha101_100 alpha(lh, ll, lc, lv);
    run_indicator_ohlcv(lh, ll, lc, lv, alpha);

    REQUIRE(alpha.line().size() == N);
    for (std::size_t i = 0; i < 281; ++i)
        REQUIRE(std::isnan(alpha.line().data()[i]));
    CHECK_FALSE(std::isnan(alpha.line().data()[281]));
    CHECK(std::isfinite(alpha.line().data()[N - 1]));
}

TEST_CASE("Alpha101 Batch C compile smoke", "[indicator][alpha101][regression]") {
    constexpr std::size_t N = 320;
    auto [lo, lh, ll, lc, lv] = make_ohlcv(N);

    SECTION("048") {
        stratforge::Alpha101_048 a(lc);
        run_indicator(lc, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("058") {
        stratforge::Alpha101_058 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("059") {
        stratforge::Alpha101_059 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("063") {
        stratforge::Alpha101_063 a(lo, lh, ll, lc, lv);
        run_indicator_full_ohlcv(lo, lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("067") {
        stratforge::Alpha101_067 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("069") {
        stratforge::Alpha101_069 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("070") {
        stratforge::Alpha101_070 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("076") {
        stratforge::Alpha101_076 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("079") {
        stratforge::Alpha101_079 a(lo, lh, ll, lc, lv);
        run_indicator_full_ohlcv(lo, lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("080") {
        stratforge::Alpha101_080 a(lo, lh, lc, lv);
        run_indicator_full_ohlcv(lo, lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("082") {
        stratforge::Alpha101_082 a(lo, lc, lv);
        run_indicator_full_ohlcv(lo, lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("087") {
        stratforge::Alpha101_087 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("089") {
        stratforge::Alpha101_089 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("090") {
        stratforge::Alpha101_090 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("091") {
        stratforge::Alpha101_091 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("093") {
        stratforge::Alpha101_093 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("097") {
        stratforge::Alpha101_097 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
    SECTION("100") {
        stratforge::Alpha101_100 a(lh, ll, lc, lv);
        run_indicator_ohlcv(lh, ll, lc, lv, a);
        REQUIRE(a.line().size() == N);
    }
}