#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Commodity Channel Index.
class CCI : public Indicator<CCI> {
public:
    CCI(const Line<double>& high,
        const Line<double>& low,
        const Line<double>& close,
        std::size_t period = 20uz,
        double factor = 0.015)
        : high_(high), low_(low), close_(close), period_(period), factor_(factor) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            typical_price_.data().reserve(n);
            tp_mean_.data().reserve(n);
            abs_deviation_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();

        const double current_tp = typical_price(idx);
        typical_price_.forward(current_tp);

        if (idx + 1 < period_) [[unlikely]] {
            tp_mean_.forward(nan);
            abs_deviation_.forward(nan);
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double typical_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            typical_sum += typical_price_.data()[idx - i];
        }
        const double tp_mean = typical_sum / static_cast<double>(period_);
        tp_mean_.forward(tp_mean);

        const double abs_dev = std::abs(current_tp - tp_mean);
        abs_deviation_.forward(abs_dev);

        if (abs_deviation_.size() < (period_ * 2) - 1) {
            line_.forward(nan);
            return;
        }

        double abs_dev_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            abs_dev_sum += abs_deviation_.data()[idx - i];
        }
        const double mean_dev = abs_dev_sum / static_cast<double>(period_);
        if (mean_dev == 0.0) {
            line_.forward(nan);
            return;
        }

        line_.forward((current_tp - tp_mean) / (factor_ * mean_dev));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return (period_ * 2) - 1;
    }

private:
    [[nodiscard]] double typical_price(std::size_t idx) const noexcept {
        return (high_.data()[idx] + low_.data()[idx] + close_.data()[idx]) / 3.0;
    }

    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    std::size_t period_;
    double factor_;
    Line<double> typical_price_;
    Line<double> tp_mean_;
    Line<double> abs_deviation_;
};

} // namespace stratforge
