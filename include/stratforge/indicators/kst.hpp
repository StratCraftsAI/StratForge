#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/momentum.hpp>
#include <stratforge/indicators/sma.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>

namespace stratforge {

class KnowSureThing : public Indicator<KnowSureThing> {
public:
    explicit KnowSureThing(const Line<double>& source,
                           std::size_t rp1 = 10uz,
                           std::size_t rp2 = 15uz,
                           std::size_t rp3 = 20uz,
                           std::size_t rp4 = 30uz,
                           std::size_t rma1 = 10uz,
                           std::size_t rma2 = 10uz,
                           std::size_t rma3 = 10uz,
                           std::size_t rma4 = 10uz,
                           std::size_t rsignal = 9uz)
        : source_(source)
        , roc1_(source, rp1)
        , roc2_(source, rp2)
        , roc3_(source, rp3)
        , roc4_(source, rp4)
        , rcma1_(roc1_.line(), rma1)
        , rcma2_(roc2_.line(), rma2)
        , rcma3_(roc3_.line(), rma3)
        , rcma4_(roc4_.line(), rma4)
        , signal_sma_(line_, rsignal)
        , kst_period_(std::max({rp1 + rma1, rp2 + rma2, rp3 + rma3, rp4 + rma4}) - 1)
        , rsignal_(rsignal == 0 ? 1 : rsignal) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            signal_.data().reserve(n);
        }
        roc1_.next();
        roc2_.next();
        roc3_.next();
        roc4_.next();

        rcma1_.next();
        rcma2_.next();
        rcma3_.next();
        rcma4_.next();

        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double v1 = rcma1_.line().data().back();
        const double v2 = rcma2_.line().data().back();
        const double v3 = rcma3_.line().data().back();
        const double v4 = rcma4_.line().data().back();

        if (std::isnan(v1) || std::isnan(v2) || std::isnan(v3) || std::isnan(v4)) {
            line_.forward(nan);
            signal_.forward(nan);
            return;
        }

        line_.forward(v1 + (2.0 * v2) + (3.0 * v3) + (4.0 * v4));
        signal_sma_.next();
        signal_.forward(signal_sma_.line().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return kst_period_ + rsignal_ - 1;
    }

    [[nodiscard]] std::size_t
    accessor_minimum_period_impl(std::string_view accessor) const noexcept {
        return accessor == "kst" ? kst_period_ : minimum_period_impl();
    }

    [[nodiscard]] const Line<double>& kst() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& signal() const noexcept { return signal_; }

private:
    const Line<double>& source_;
    ROC100 roc1_;
    ROC100 roc2_;
    ROC100 roc3_;
    ROC100 roc4_;
    SMA rcma1_;
    SMA rcma2_;
    SMA rcma3_;
    SMA rcma4_;
    SMA signal_sma_;
    Line<double> signal_;
    std::size_t kst_period_;
    std::size_t rsignal_;
};

using KST = KnowSureThing;

} // namespace stratforge
