#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/statistics.hpp>
#include <stratforge/indicators/volume.hpp>
#include <stratforge/indicators/volatility.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace {

stratforge::Line<double> make_line(const std::vector<double>& values) {
    stratforge::Line<double> line;
    for (double value : values) {
        line.forward(value);
    }
    line.home();
    return line;
}

template <typename IndicatorType>
void run_indicator(stratforge::Line<double>& source, IndicatorType& indicator) {
    for (std::size_t i = 0; i < source.size(); ++i) {
        indicator.next();
        if (i + 1 < source.size()) {
            source.advance();
        }
    }
}

} // namespace

TEST_CASE("AD accumulates money flow volume", "[indicator][phase8][ad]") {
    auto high = make_line({10.0, 11.0, 12.0});
    auto low = make_line({8.0, 9.0, 10.0});
    auto close = make_line({9.0, 10.5, 10.5});
    auto volume = make_line({100.0, 200.0, 150.0});

    stratforge::AD ad(high, low, close, volume);
    for (std::size_t i = 0; i < close.size(); ++i) {
        ad.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
            volume.advance();
        }
    }

    REQUIRE(ad.line().data()[0] == Approx(0.0));
    REQUIRE(ad.line().data()[1] == Approx(100.0));
    REQUIRE(ad.line().data()[2] == Approx(25.0));
}

TEST_CASE("PVT compounds volume by percentage close change", "[indicator][phase8][pvt]") {
    auto close = make_line({10.0, 11.0, 10.0, 12.0});
    auto volume = make_line({100.0, 200.0, 150.0, 120.0});

    stratforge::PVT pvt(close, volume);
    for (std::size_t i = 0; i < close.size(); ++i) {
        pvt.next();
        if (i + 1 < close.size()) {
            close.advance();
            volume.advance();
        }
    }

    REQUIRE(pvt.line().data()[0] == Approx(0.0));
    REQUIRE(pvt.line().data()[1] == Approx(20.0));
    REQUIRE(pvt.line().data()[2] == Approx(6.3636363636));
    REQUIRE(pvt.line().data()[3] == Approx(30.3636363636));
}

TEST_CASE("VWAP tracks cumulative typical-price weighting", "[indicator][phase8][vwap]") {
    auto high = make_line({10.0, 12.0, 14.0});
    auto low = make_line({8.0, 10.0, 12.0});
    auto close = make_line({9.0, 11.0, 13.0});
    auto volume = make_line({100.0, 200.0, 300.0});

    stratforge::VWAP vwap(high, low, close, volume);
    for (std::size_t i = 0; i < close.size(); ++i) {
        vwap.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
            volume.advance();
        }
    }

    REQUIRE(vwap.line().data()[0] == Approx(9.0));
    REQUIRE(vwap.line().data()[1] == Approx(10.3333333333));
    REQUIRE(vwap.line().data()[2] == Approx(11.6666666667));
}

TEST_CASE("Donchian channels emit rolling high low and midpoint", "[indicator][phase8][donchian]") {
    auto high = make_line({10.0, 12.0, 11.0, 15.0});
    auto low = make_line({8.0, 9.0, 7.0, 10.0});

    stratforge::Donchian donchian(high, low, 3);
    for (std::size_t i = 0; i < high.size(); ++i) {
        donchian.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
        }
    }

    REQUIRE(std::isnan(donchian.mid().data()[0]));
    REQUIRE(std::isnan(donchian.top().data()[1]));
    REQUIRE(donchian.top().data()[2] == Approx(12.0));
    REQUIRE(donchian.bottom().data()[2] == Approx(7.0));
    REQUIRE(donchian.mid().data()[2] == Approx(9.5));
    REQUIRE(donchian.top().data()[3] == Approx(15.0));
    REQUIRE(donchian.bottom().data()[3] == Approx(7.0));
    REQUIRE(donchian.mid().data()[3] == Approx(11.0));
}

TEST_CASE("UlcerIndex measures trailing squared drawdowns", "[indicator][phase8][ulcer]") {
    auto close = make_line({100.0, 90.0, 80.0, 120.0});

    stratforge::UlcerIndex ulcer(close, 3);
    run_indicator(close, ulcer);

    REQUIRE(std::isnan(ulcer.line().data()[0]));
    REQUIRE(std::isnan(ulcer.line().data()[1]));
    REQUIRE(ulcer.line().data()[2] == Approx(12.9099444874));
    REQUIRE(ulcer.line().data()[3] == Approx(6.4150029914));
}

TEST_CASE("Correlation and RSquared match trailing Pearson statistics", "[indicator][phase8][statistics]") {
    auto x_corr = make_line({1.0, 2.0, 3.0, 4.0});
    auto y_corr = make_line({2.0, 4.0, 6.0, 9.0});
    auto x_r2 = make_line({1.0, 2.0, 3.0, 4.0});
    auto y_r2 = make_line({2.0, 4.0, 6.0, 9.0});

    stratforge::Correlation corr(x_corr, y_corr, 3);
    stratforge::RSquared r2(x_r2, y_r2, 3);

    for (std::size_t i = 0; i < x_corr.size(); ++i) {
        corr.next();
        r2.next();
        if (i + 1 < x_corr.size()) {
            x_corr.advance();
            y_corr.advance();
            x_r2.advance();
            y_r2.advance();
        }
    }

    REQUIRE(std::isnan(corr.line().data()[0]));
    REQUIRE(std::isnan(corr.line().data()[1]));
    REQUIRE(corr.line().data()[2] == Approx(1.0));
    REQUIRE(corr.line().data()[3] == Approx(0.9933992678));

    REQUIRE(std::isnan(r2.line().data()[0]));
    REQUIRE(std::isnan(r2.line().data()[1]));
    REQUIRE(r2.line().data()[2] == Approx(1.0));
    REQUIRE(r2.line().data()[3] == Approx(0.9868421053));
}
