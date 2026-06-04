#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// McGinley Dynamic — adaptive moving average that adjusts speed
/// based on price distance from the average.
/// MD = MD_prev + (close - MD_prev) / (k * period * (close/MD_prev)^4)
class McGinleyDynamic : public Indicator<McGinleyDynamic> {
public:
    explicit McGinleyDynamic(const Line<double>& source, std::size_t period = 10uz, double k = 0.6)
        : source_(source), period_(period == 0 ? 1 : period), k_(k) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        const double close = source_.data()[idx];

        if (idx == 0) {
            md_ = close;
            line_.forward(close);
            return;
        }

        if (md_ == 0.0) {
            md_ = close;
            line_.forward(close);
            return;
        }

        const double ratio = close / md_;
        const double ratio4 = ratio * ratio * ratio * ratio;
        const double denom = k_ * static_cast<double>(period_) * ratio4;
        if (denom == 0.0) {
            line_.forward(md_);
            return;
        }
        md_ = md_ + (close - md_) / denom;
        line_.forward(md_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double k_;
    double md_ = 0.0;
};

using MCGD = McGinleyDynamic;

} // namespace stratforge
