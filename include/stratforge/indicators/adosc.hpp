#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Chaikin A/D Oscillator: EMA(fast, AD) - EMA(slow, AD)
class ADOSC : public Indicator<ADOSC> {
public:
    ADOSC(const Line<double>& high,
          const Line<double>& low,
          const Line<double>& close,
          const Line<double>& volume,
          std::size_t fast_period = 3uz,
          std::size_t slow_period = 10uz)
        : high_(high), low_(low), close_(close), volume_(volume)
        , fast_period_(fast_period == 0 ? 1 : fast_period)
        , slow_period_(slow_period == 0 ? 1 : slow_period)
        , fast_mult_(2.0 / (static_cast<double>(fast_period_) + 1.0))
        , slow_mult_(2.0 / (static_cast<double>(slow_period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();

        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];
        const double v = volume_.data()[idx];
        const double range = h - l;
        const double mfm = (range == 0.0) ? 0.0 : (((c - l) - (h - c)) / range);
        const double mfv = mfm * v;

        ad_cumulative_ += mfv;

        if (!initialized_) [[unlikely]] {
            fast_ema_ = ad_cumulative_;
            slow_ema_ = ad_cumulative_;
            initialized_ = true;
            line_.forward(0.0);
            return;
        }

        fast_ema_ = (ad_cumulative_ - fast_ema_) * fast_mult_ + fast_ema_;
        slow_ema_ = (ad_cumulative_ - slow_ema_) * slow_mult_ + slow_ema_;
        line_.forward(fast_ema_ - slow_ema_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return slow_period_;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    std::size_t fast_period_;
    std::size_t slow_period_;
    double fast_mult_;
    double slow_mult_;
    double ad_cumulative_ = 0.0;
    double fast_ema_ = 0.0;
    double slow_ema_ = 0.0;
    bool initialized_ = false;
};

using ChaikinADOscillator = ADOSC;

} // namespace stratforge
