#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Population variance over the trailing period.
class Variance : public Indicator<Variance> {
public:
    explicit Variance(const Line<double>& source, std::size_t period = 5)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            sum += source_.data()[idx - i];
        }
        const double mean = sum / static_cast<double>(period_);

        double variance_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double delta = source_.data()[idx - i] - mean;
            variance_sum += delta * delta;
        }

        line_.forward(variance_sum / static_cast<double>(period_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using VAR = Variance;

} // namespace stratforge
