#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Aberration Bands: CCI-like deviation bands around SMA.
/// line() = mid (SMA), upper/lower at ±(mean deviation * mult).
class Aberration : public Indicator<Aberration> {
public:
    explicit Aberration(const Line<double>& source, std::size_t period = 5uz,
                        double mult = 2.0)
        : source_(source), period_(period == 0 ? 1 : period), mult_(mult) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            upper_.data().reserve(n);
            lower_.data().reserve(n);
            signal_.data().reserve(n);
        }
        const auto idx = source_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(nan);
            upper_.forward(nan);
            lower_.forward(nan);
            signal_.forward(nan);
            return;
        }

        const double* p = &source_.data()[idx - period_ + 1];
        const double sma = simd::reduce_sum(p, period_) / static_cast<double>(period_);

        double dev_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i)
            dev_sum += std::abs(p[i] - sma);
        const double mean_dev = dev_sum / static_cast<double>(period_);

        line_.forward(sma);
        upper_.forward(sma + mult_ * mean_dev);
        lower_.forward(sma - mult_ * mean_dev);
        signal_.forward(upper_.data().back() - lower_.data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] const Line<double>& mid() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& upper() const noexcept { return upper_; }
    [[nodiscard]] const Line<double>& lower() const noexcept { return lower_; }
    [[nodiscard]] const Line<double>& signal() const noexcept { return signal_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double mult_;
    Line<double> upper_;
    Line<double> lower_;
    Line<double> signal_;
};

using ABERRATION = Aberration;

} // namespace stratforge
