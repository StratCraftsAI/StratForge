#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Holt-Winters Channel (HWC) — triple exponential smoothing bands.
/// Outputs: mid (line_), upper, lower.
class HWC : public Indicator<HWC> {
public:
    explicit HWC(const Line<double>& source, std::size_t period = 12uz,
                 double mult = 1.0)
        : source_(source)
        , period_(period == 0 ? 1 : period)
        , mult_(mult) {
        na_ = 2.0 / (static_cast<double>(period_) + 1.0);
        nb_ = na_ / 2.0;
    }

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            upper_.data().reserve(n);
            lower_.data().reserve(n);
        }
        const auto idx = source_.index();
        const double val = source_.data()[idx];

        if (idx == 0) {
            f0_ = val;
            f1_ = 0.0;
            v0_ = val;
            v1_ = 0.0;
            line_.forward(val);
            upper_.forward(val);
            lower_.forward(val);
            return;
        }

        f0_ = (1.0 - na_) * (f0_ + f1_) + na_ * val;
        f1_ = (1.0 - nb_) * f1_ + nb_ * (f0_ - f0_prev_);
        f0_prev_ = f0_;

        const double f = f0_ + f1_;
        const double dev = std::abs(val - f);
        v0_ = (1.0 - na_) * (v0_ + v1_) + na_ * dev;
        v1_ = (1.0 - nb_) * v1_ + nb_ * (v0_ - v0_prev_);
        v0_prev_ = v0_;

        const double band = mult_ * (v0_ + v1_);
        line_.forward(f);
        upper_.forward(f + band);
        lower_.forward(f - band);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] const Line<double>& mid() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& upper() const noexcept { return upper_; }
    [[nodiscard]] const Line<double>& lower() const noexcept { return lower_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double mult_;
    double na_;
    double nb_;
    double f0_ = 0.0;
    double f1_ = 0.0;
    double f0_prev_ = 0.0;
    double v0_ = 0.0;
    double v1_ = 0.0;
    double v0_prev_ = 0.0;
    Line<double> upper_;
    Line<double> lower_;
};

using HoltWintersChannel = HWC;

} // namespace stratforge
