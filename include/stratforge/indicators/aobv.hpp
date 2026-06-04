#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Archer On Balance Volume: OBV with fast/slow EMA signal lines.
class AOBV : public Indicator<AOBV> {
public:
    AOBV(const Line<double>& close, const Line<double>& volume,
         std::size_t fast_period = 4uz, std::size_t slow_period = 12uz)
        : close_(close), volume_(volume)
        , fast_period_(fast_period == 0 ? 1 : fast_period)
        , slow_period_(slow_period == 0 ? 1 : slow_period)
        , fast_mult_(2.0 / (static_cast<double>(fast_period_) + 1.0))
        , slow_mult_(2.0 / (static_cast<double>(slow_period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            obv_.data().reserve(n);
            fast_ema_line_.data().reserve(n);
            slow_ema_line_.data().reserve(n);
        }
        const auto idx = close_.index();

        if (idx == 0) {
            obv_val_ = 0.0;
            obv_.forward(0.0);
            fast_ema_ = 0.0;
            slow_ema_ = 0.0;
            fast_ema_line_.forward(0.0);
            slow_ema_line_.forward(0.0);
            line_.forward(0.0);
            return;
        }

        const double c = close_.data()[idx];
        const double cp = close_.data()[idx - 1];
        const double v = volume_.data()[idx];
        if (c > cp)
            obv_val_ += v;
        else if (c < cp)
            obv_val_ -= v;

        obv_.forward(obv_val_);

        fast_ema_ = (obv_val_ - fast_ema_) * fast_mult_ + fast_ema_;
        slow_ema_ = (obv_val_ - slow_ema_) * slow_mult_ + slow_ema_;
        fast_ema_line_.forward(fast_ema_);
        slow_ema_line_.forward(slow_ema_);
        line_.forward(fast_ema_ - slow_ema_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

    [[nodiscard]] const Line<double>& obv() const noexcept { return obv_; }
    [[nodiscard]] const Line<double>& fast() const noexcept { return fast_ema_line_; }
    [[nodiscard]] const Line<double>& slow() const noexcept { return slow_ema_line_; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    std::size_t fast_period_;
    std::size_t slow_period_;
    double fast_mult_;
    double slow_mult_;
    double obv_val_ = 0.0;
    double fast_ema_ = 0.0;
    double slow_ema_ = 0.0;
    Line<double> obv_;
    Line<double> fast_ema_line_;
    Line<double> slow_ema_line_;
};

using ArcherOBV = AOBV;

} // namespace stratforge
