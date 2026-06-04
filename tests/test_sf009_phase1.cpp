#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/stratforge.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <limits>
#include <vector>

using stratforge::test::make_line;
using stratforge::test::run_indicator;
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

// --- Trivial formula indicators ---

TEST_CASE("Bias produces correct values", "[indicator][bias][regression]") {
    auto source = make_line(kClose);
    stratforge::Bias bias(source, 5);
    run_indicator(source, bias);

    REQUIRE(bias.line().size() == kClose.size());
    REQUIRE(std::isnan(bias.line().data()[0]));
    REQUIRE(std::isnan(bias.line().data()[3]));
    CHECK_FALSE(std::isnan(bias.line().data()[4]));
}

TEST_CASE("LogReturn produces correct values", "[indicator][log_return][regression]") {
    auto source = make_line(kClose);
    stratforge::LogReturn lr(source, 1);
    run_indicator(source, lr);

    REQUIRE(lr.line().size() == kClose.size());
    REQUIRE(std::isnan(lr.line().data()[0]));
    CHECK_THAT(lr.line().data()[1], WithinRel(std::log(44.09 / 44.34), 1e-12));
}

TEST_CASE("PriceDistance outputs (H-L)/C", "[indicator][pdist][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::PriceDistance pd(h, l, c);
    stratforge::test::run_indicator_hlc(h, l, c, pd);

    REQUIRE(pd.line().size() == kClose.size());
    CHECK_THAT(pd.line().data()[0], WithinRel((44.52 - 44.10) / 44.34, 1e-12));
}

TEST_CASE("Decay holds value with linear decline", "[indicator][decay][regression]") {
    auto source = make_line({1.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    stratforge::Decay decay(source, 5);
    run_indicator(source, decay);

    REQUIRE(decay.line().size() == 6);
    CHECK_THAT(decay.line().data()[0], WithinAbs(1.0, 1e-12));
    CHECK_THAT(decay.line().data()[1], WithinAbs(0.8, 1e-12));
    CHECK_THAT(decay.line().data()[5], WithinAbs(0.0, 1e-12));
}

TEST_CASE("Increasing detects consecutive up-bars", "[indicator][increasing][regression]") {
    auto source = make_line({1.0, 2.0, 3.0, 4.0, 3.5, 4.5});
    stratforge::Increasing inc(source, 2);
    run_indicator(source, inc);

    REQUIRE(inc.line().size() == 6);
    REQUIRE(std::isnan(inc.line().data()[0]));
    REQUIRE(std::isnan(inc.line().data()[1]));
    CHECK(inc.line().data()[2] == 1.0);
    CHECK(inc.line().data()[3] == 1.0);
    CHECK(inc.line().data()[4] == 0.0);
}

TEST_CASE("Decreasing detects consecutive down-bars", "[indicator][decreasing][regression]") {
    auto source = make_line({4.0, 3.0, 2.0, 1.0, 1.5, 0.5});
    stratforge::Decreasing dec(source, 2);
    run_indicator(source, dec);

    REQUIRE(dec.line().size() == 6);
    CHECK(dec.line().data()[2] == 1.0);
    CHECK(dec.line().data()[4] == 0.0);
}

TEST_CASE("ZScore normalizes values", "[indicator][zscore][regression]") {
    auto source = make_line(kClose);
    stratforge::ZScore zs(source, 5);
    run_indicator(source, zs);

    REQUIRE(zs.line().size() == kClose.size());
    REQUIRE(std::isnan(zs.line().data()[3]));
    CHECK_FALSE(std::isnan(zs.line().data()[4]));
}

TEST_CASE("PPO computes percentage oscillator", "[indicator][ppo][regression]") {
    auto source = make_line(kClose);
    stratforge::PercentagePriceOscillator ppo(source, 3, 5);
    run_indicator(source, ppo);

    REQUIRE(ppo.line().size() == kClose.size());
    REQUIRE(std::isnan(ppo.line().data()[3]));
    CHECK_FALSE(std::isnan(ppo.line().data()[4]));
}

TEST_CASE("EfficiencyRatio is in [0,1]", "[indicator][er][regression]") {
    auto source = make_line(kClose);
    stratforge::EfficiencyRatio er(source, 5);
    run_indicator(source, er);

    for (std::size_t i = 5; i < er.line().size(); ++i) {
        INFO("bar=" << i);
        CHECK(er.line().data()[i] >= 0.0);
        CHECK(er.line().data()[i] <= 1.0 + 1e-12);
    }
}

TEST_CASE("CFO measures deviation from linreg", "[indicator][cfo][regression]") {
    auto source = make_line(kClose);
    stratforge::CFO cfo(source, 5);
    run_indicator(source, cfo);

    REQUIRE(cfo.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(cfo.line().data()[4]));
}

// --- Rolling statistics ---

TEST_CASE("Median computes rolling median", "[indicator][median][regression]") {
    auto source = make_line({5.0, 3.0, 1.0, 4.0, 2.0});
    stratforge::Median med(source, 3);
    run_indicator(source, med);

    REQUIRE(med.line().size() == 5);
    REQUIRE(std::isnan(med.line().data()[0]));
    CHECK_THAT(med.line().data()[2], WithinAbs(3.0, 1e-12));
    CHECK_THAT(med.line().data()[3], WithinAbs(3.0, 1e-12));
    CHECK_THAT(med.line().data()[4], WithinAbs(2.0, 1e-12));
}

TEST_CASE("Quantile at 0.5 matches median", "[indicator][quantile][regression]") {
    auto source = make_line({5.0, 3.0, 1.0, 4.0, 2.0});
    stratforge::Quantile q(source, 3, 0.5);
    stratforge::Median med(source, 3);
    source.home();
    run_indicator(source, q);
    source.home();
    run_indicator(source, med);

    for (std::size_t i = 2; i < 5; ++i) {
        INFO("bar=" << i);
        CHECK_THAT(q.line().data()[i], WithinAbs(med.line().data()[i], 1e-12));
    }
}

TEST_CASE("Kurtosis handles uniform data", "[indicator][kurtosis][regression]") {
    auto source = make_line(kClose);
    stratforge::Kurtosis kurt(source, 10);
    run_indicator(source, kurt);

    REQUIRE(kurt.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(kurt.line().data()[9]));
}

TEST_CASE("Skew handles normal-ish data", "[indicator][skew][regression]") {
    auto source = make_line(kClose);
    stratforge::Skew skew(source, 10);
    run_indicator(source, skew);

    REQUIRE(skew.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(skew.line().data()[9]));
}

TEST_CASE("Entropy is non-negative", "[indicator][entropy][regression]") {
    auto source = make_line(kClose);
    stratforge::Entropy ent(source, 5);
    run_indicator(source, ent);

    for (std::size_t i = 4; i < ent.line().size(); ++i) {
        INFO("bar=" << i);
        CHECK(ent.line().data()[i] >= 0.0);
    }
}

TEST_CASE("TosStdevAll produces linreg and bands", "[indicator][tos_stdevall][regression]") {
    auto source = make_line(kClose);
    stratforge::TosStdevAll tos(source, 10);
    run_indicator(source, tos);

    REQUIRE(tos.linreg().size() == kClose.size());
    for (std::size_t i = 9; i < kClose.size(); ++i) {
        INFO("bar=" << i);
        CHECK(tos.upper2().data()[i] > tos.upper1().data()[i]);
        CHECK(tos.lower2().data()[i] < tos.lower1().data()[i]);
    }
}

TEST_CASE("EBSW oscillates in [-1, 1]", "[indicator][ebsw][regression]") {
    auto source = make_line(kClose);
    stratforge::EBSW ebsw(source, 10);
    run_indicator(source, ebsw);

    for (std::size_t i = 9; i < ebsw.line().size(); ++i) {
        INFO("bar=" << i);
        CHECK(ebsw.line().data()[i] >= -1.0 - 1e-12);
        CHECK(ebsw.line().data()[i] <= 1.0 + 1e-12);
    }
}

// --- Weighted MAs ---

TEST_CASE("FWMA uses Fibonacci weights", "[indicator][fwma][regression]") {
    auto source = make_line(kClose);
    stratforge::FWMA fwma(source, 5);
    run_indicator(source, fwma);

    REQUIRE(fwma.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(fwma.line().data()[4]));
}

TEST_CASE("SWMA uses symmetric weights", "[indicator][swma][regression]") {
    auto source = make_line(kClose);
    stratforge::SWMA swma(source, 5);
    run_indicator(source, swma);

    REQUIRE(swma.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(swma.line().data()[4]));
}

TEST_CASE("PWMA uses Pascal weights", "[indicator][pwma][regression]") {
    auto source = make_line(kClose);
    stratforge::PWMA pwma(source, 5);
    run_indicator(source, pwma);

    REQUIRE(pwma.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(pwma.line().data()[4]));
}

TEST_CASE("SINWMA uses sine weights", "[indicator][sinwma][regression]") {
    auto source = make_line(kClose);
    stratforge::SINWMA sinwma(source, 5);
    run_indicator(source, sinwma);

    REQUIRE(sinwma.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(sinwma.line().data()[4]));
}

TEST_CASE("ALMA uses Gaussian weights", "[indicator][alma][regression]") {
    auto source = make_line(kClose);
    stratforge::ALMA alma(source, 5);
    run_indicator(source, alma);

    REQUIRE(alma.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(alma.line().data()[4]));
}

// --- Smoothing / Adaptive MAs ---

TEST_CASE("SSF smooths without lag", "[indicator][ssf][regression]") {
    auto source = make_line(kClose);
    stratforge::SSF ssf(source, 10);
    run_indicator(source, ssf);

    REQUIRE(ssf.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(ssf.line().data()[0]));
}

TEST_CASE("JMA adapts to price", "[indicator][jma][regression]") {
    auto source = make_line(kClose);
    stratforge::JMA jma(source, 7);
    run_indicator(source, jma);

    REQUIRE(jma.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(jma.line().data()[0]));
}

TEST_CASE("McGinleyDynamic adapts speed", "[indicator][mcgd][regression]") {
    auto source = make_line(kClose);
    stratforge::McGinleyDynamic mcgd(source, 10);
    run_indicator(source, mcgd);

    REQUIRE(mcgd.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(mcgd.line().data()[0]));
}

TEST_CASE("HWC produces mid/upper/lower bands", "[indicator][hwc][regression]") {
    auto source = make_line(kClose);
    stratforge::HWC hwc(source, 12);
    run_indicator(source, hwc);

    REQUIRE(hwc.mid().size() == kClose.size());
    for (std::size_t i = 5; i < kClose.size(); ++i) {
        INFO("bar=" << i);
        CHECK(hwc.upper().data()[i] >= hwc.mid().data()[i]);
        CHECK(hwc.lower().data()[i] <= hwc.mid().data()[i]);
    }
}

// --- Volume indicators ---

TEST_CASE("VWMA weights by volume", "[indicator][vwma][regression]") {
    auto source = make_line(kClose);
    auto vol = make_line(kVolume);
    stratforge::VWMA vwma(source, vol, 5);
    stratforge::test::run_indicator_cv(source, vol, vwma);

    REQUIRE(vwma.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(vwma.line().data()[4]));
}

TEST_CASE("ADOSC computes A/D oscillator", "[indicator][adosc][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::ADOSC adosc(h, l, c, v, 3, 10);
    stratforge::test::run_indicator_ohlcv(h, l, c, v, adosc);

    REQUIRE(adosc.line().size() == kClose.size());
}

TEST_CASE("AOBV produces OBV with EMA signals", "[indicator][aobv][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::AOBV aobv(c, v, 4, 12);
    stratforge::test::run_indicator_cv(c, v, aobv);

    REQUIRE(aobv.line().size() == kClose.size());
}

TEST_CASE("EFI computes elder force index", "[indicator][efi][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::EFI efi(c, v, 5);
    stratforge::test::run_indicator_cv(c, v, efi);

    REQUIRE(efi.line().size() == kClose.size());
}

TEST_CASE("NVI starts at 1000 and updates on down-volume", "[indicator][nvi][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::NVI nvi(c, v);
    stratforge::test::run_indicator_cv(c, v, nvi);

    REQUIRE(nvi.line().size() == kClose.size());
    CHECK_THAT(nvi.line().data()[0], WithinAbs(1000.0, 1e-12));
}

TEST_CASE("PVI starts at 1000 and updates on up-volume", "[indicator][pvi][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::PVI pvi(c, v);
    stratforge::test::run_indicator_cv(c, v, pvi);

    REQUIRE(pvi.line().size() == kClose.size());
    CHECK_THAT(pvi.line().data()[0], WithinAbs(1000.0, 1e-12));
}

TEST_CASE("PriceVolume is close * volume", "[indicator][pvol][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::PriceVolume pvol(c, v);
    stratforge::test::run_indicator_cv(c, v, pvol);

    CHECK_THAT(pvol.line().data()[0], WithinRel(44.34 * 100000.0, 1e-12));
}

TEST_CASE("PVR classifies into 4 categories", "[indicator][pvr][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::PVR pvr(c, v);
    stratforge::test::run_indicator_cv(c, v, pvr);

    for (std::size_t i = 1; i < pvr.line().size(); ++i) {
        const double val = pvr.line().data()[i];
        CHECK((val == 1.0 || val == -1.0 || val == 2.0 || val == -2.0));
    }
}

TEST_CASE("VolumeProfile outputs in [0,1]", "[indicator][vp][regression]") {
    auto c = make_line(kClose);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto v = make_line(kVolume);
    stratforge::VolumeProfile vp(c, h, l, v, 5);
    stratforge::test::run_indicator_ohlcv(h, l, c, v, vp);

    for (std::size_t i = 4; i < vp.line().size(); ++i) {
        INFO("bar=" << i);
        CHECK(vp.line().data()[i] >= 0.0);
        CHECK(vp.line().data()[i] <= 1.0 + 1e-12);
    }
}

// --- Momentum / Trend ---

TEST_CASE("PSL outputs percentage in [0,100]", "[indicator][psl][regression]") {
    auto source = make_line(kClose);
    stratforge::PSL psl(source, 5);
    run_indicator(source, psl);

    for (std::size_t i = 5; i < psl.line().size(); ++i) {
        INFO("bar=" << i);
        CHECK(psl.line().data()[i] >= 0.0);
        CHECK(psl.line().data()[i] <= 100.0 + 1e-12);
    }
}

TEST_CASE("QQE produces values", "[indicator][qqe][regression]") {
    auto source = make_line(kClose);
    stratforge::QQE qqe(source, 5);
    run_indicator(source, qqe);

    REQUIRE(qqe.line().size() == kClose.size());
}

TEST_CASE("Squeeze detects BB inside KC", "[indicator][squeeze][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Squeeze sq(h, l, c, 10);
    stratforge::test::run_indicator_hlc(h, l, c, sq);

    REQUIRE(sq.squeeze_on().size() == kClose.size());
    for (std::size_t i = 9; i < kClose.size(); ++i) {
        const double v = sq.squeeze_on().data()[i];
        CHECK((v == 0.0 || v == 1.0));
    }
}

TEST_CASE("Choppiness is in [0, 100]", "[indicator][chop][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Choppiness chop(h, l, c, 10);
    stratforge::test::run_indicator_hlc(h, l, c, chop);

    for (std::size_t i = 10; i < chop.line().size(); ++i) {
        INFO("bar=" << i);
        CHECK(chop.line().data()[i] >= 0.0);
        CHECK(chop.line().data()[i] <= 100.0 + 1e-6);
    }
}

TEST_CASE("ChandeKrollStop produces stop levels", "[indicator][cksp][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::ChandeKrollStop cksp(h, l, c, 5, 3);
    stratforge::test::run_indicator_hlc(h, l, c, cksp);

    REQUIRE(cksp.stop_long().size() == kClose.size());
    REQUIRE(cksp.stop_short().size() == kClose.size());
}

TEST_CASE("QStick measures body momentum", "[indicator][qstick][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::QStick qs(o, c, 5);
    stratforge::test::run_indicator_oc(o, c, qs);

    REQUIRE(qs.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(qs.line().data()[4]));
}

TEST_CASE("RVI is RSI-like in [0,100]", "[indicator][rvi][regression]") {
    auto source = make_line(kClose);
    stratforge::RVI rvi(source, 5, 5);
    run_indicator(source, rvi);

    for (std::size_t i = 6; i < rvi.line().size(); ++i) {
        INFO("bar=" << i);
        CHECK(rvi.line().data()[i] >= 0.0);
        CHECK(rvi.line().data()[i] <= 100.0 + 1e-12);
    }
}

TEST_CASE("Thermometer outputs EMA of bar volatility", "[indicator][thermo][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Thermometer thermo(h, l, 5);
    stratforge::test::run_indicator_hl(h, l, thermo);

    REQUIRE(thermo.line().size() == kHigh.size());
    for (std::size_t i = 1; i < kHigh.size(); ++i) {
        CHECK(thermo.line().data()[i] >= 0.0);
    }
}

TEST_CASE("STC is in [0, 100]", "[indicator][stc][regression]") {
    auto source = make_line(kClose);
    stratforge::STC stc(source, 5, 5, 10);
    run_indicator(source, stc);

    for (std::size_t i = 0; i < stc.line().size(); ++i) {
        if (std::isnan(stc.line().data()[i])) continue;
        INFO("bar=" << i);
        CHECK(stc.line().data()[i] >= 0.0 - 1e-12);
        CHECK(stc.line().data()[i] <= 100.0 + 1e-12);
    }
}

TEST_CASE("TDSequential counts buy/sell setups", "[indicator][td_seq][regression]") {
    auto source = make_line(kClose);
    stratforge::TDSequential td(source, 4);
    run_indicator(source, td);

    REQUIRE(td.line().size() == kClose.size());
}

TEST_CASE("Inertia measures trend inertia", "[indicator][inertia][regression]") {
    auto source = make_line(kClose);
    stratforge::Inertia inertia(source, 10, 5);
    run_indicator(source, inertia);

    REQUIRE(inertia.line().size() == kClose.size());
}

TEST_CASE("CTI measures correlation trend in [-1,1]", "[indicator][cti][regression]") {
    auto source = make_line(kClose);
    stratforge::CTI cti(source, 5);
    run_indicator(source, cti);

    for (std::size_t i = 4; i < cti.line().size(); ++i) {
        INFO("bar=" << i);
        CHECK(cti.line().data()[i] >= -1.0 - 1e-12);
        CHECK(cti.line().data()[i] <= 1.0 + 1e-12);
    }
}

TEST_CASE("MassIndex detects reversal bulge", "[indicator][massi][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::MassIndex massi(h, l, 3, 5);
    stratforge::test::run_indicator_hl(h, l, massi);

    REQUIRE(massi.line().size() == kHigh.size());
}

TEST_CASE("Aberration produces mid/upper/lower", "[indicator][aberration][regression]") {
    auto source = make_line(kClose);
    stratforge::Aberration ab(source, 5);
    run_indicator(source, ab);

    for (std::size_t i = 4; i < kClose.size(); ++i) {
        INFO("bar=" << i);
        CHECK(ab.upper().data()[i] >= ab.mid().data()[i]);
        CHECK(ab.lower().data()[i] <= ab.mid().data()[i]);
    }
}

TEST_CASE("AMAT outputs +1/-1/0", "[indicator][amat][regression]") {
    auto source = make_line(kClose);
    stratforge::AMAT amat(source, 3, 8);
    run_indicator(source, amat);

    for (std::size_t i = 0; i < amat.line().size(); ++i) {
        const double v = amat.line().data()[i];
        CHECK((v == 1.0 || v == -1.0 || v == 0.0));
    }
}

// --- Signal helpers ---

TEST_CASE("LongRun counts consecutive fast > slow", "[indicator][long_run][regression]") {
    auto fast = make_line({1.0, 3.0, 4.0, 5.0, 2.0, 3.0});
    auto slow = make_line({2.0, 2.0, 2.0, 2.0, 2.0, 2.0});
    stratforge::LongRun lr(fast, slow, 2);
    stratforge::test::run_indicator_cv(fast, slow, lr);

    CHECK(lr.line().data()[0] == 0.0);
    CHECK(lr.line().data()[1] == 0.0);
    CHECK(lr.line().data()[2] == 1.0);
    CHECK(lr.line().data()[3] == 1.0);
    CHECK(lr.line().data()[4] == 0.0);
}

TEST_CASE("ShortRun counts consecutive fast < slow", "[indicator][short_run][regression]") {
    auto fast = make_line({3.0, 1.0, 0.5, 0.3, 4.0, 1.0});
    auto slow = make_line({2.0, 2.0, 2.0, 2.0, 2.0, 2.0});
    stratforge::ShortRun sr(fast, slow, 2);
    stratforge::test::run_indicator_cv(fast, slow, sr);

    CHECK(sr.line().data()[0] == 0.0);
    CHECK(sr.line().data()[1] == 0.0);
    CHECK(sr.line().data()[2] == 1.0);
    CHECK(sr.line().data()[3] == 1.0);
    CHECK(sr.line().data()[4] == 0.0);
}

TEST_CASE("TSignals generates trend and signal", "[indicator][tsignals][regression]") {
    auto source = make_line(kClose);
    stratforge::TSignals ts(source, 5);
    run_indicator(source, ts);

    REQUIRE(ts.trend().size() == kClose.size());
    REQUIRE(ts.signal().size() == kClose.size());
}

TEST_CASE("XSignals detects crossovers", "[indicator][xsignals][regression]") {
    auto fast = make_line({1.0, 3.0, 5.0, 3.0, 1.0, 3.0});
    auto slow = make_line({2.0, 2.0, 2.0, 4.0, 4.0, 2.0});
    stratforge::XSignals xs(fast, slow);
    stratforge::test::run_indicator_cv(fast, slow, xs);

    CHECK(xs.line().data()[1] == 1.0);
    CHECK(xs.line().data()[3] == -1.0);
}

TEST_CASE("AccelerationBands produce upper/lower", "[indicator][accbands][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::AccelerationBands ab(h, l, c, 5);
    stratforge::test::run_indicator_hlc(h, l, c, ab);

    for (std::size_t i = 4; i < kClose.size(); ++i) {
        INFO("bar=" << i);
        CHECK(ab.upper().data()[i] >= ab.mid().data()[i]);
        CHECK(ab.lower().data()[i] <= ab.mid().data()[i]);
    }
}

// --- Candlestick ---

TEST_CASE("CdlInside detects inside bars", "[indicator][cdl_inside][regression]") {
    auto h = make_line({10.0, 8.0, 9.0, 7.0, 11.0});
    auto l = make_line({5.0, 6.0, 4.0, 6.5, 4.0});
    stratforge::CdlInside cdl(h, l);
    stratforge::test::run_indicator_hl(h, l, cdl);

    CHECK(cdl.line().data()[0] == 0.0);
    CHECK(cdl.line().data()[1] == 1.0);
    CHECK(cdl.line().data()[2] == 0.0);
    CHECK(cdl.line().data()[3] == 1.0);
    CHECK(cdl.line().data()[4] == 0.0);
}

TEST_CASE("CdlZ scores candle bodies", "[indicator][cdl_z][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::CdlZ cz(o, c, 5);
    stratforge::test::run_indicator_oc(o, c, cz);

    REQUIRE(cz.line().size() == kClose.size());
    CHECK_FALSE(std::isnan(cz.line().data()[4]));
}

// --- DX standalone ---

TEST_CASE("DX extracts directional index", "[indicator][dx][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::DX dx(h, l, c, 5);
    stratforge::test::run_indicator_hlc(h, l, c, dx);

    REQUIRE(dx.line().size() == kClose.size());
}
