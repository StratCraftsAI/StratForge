#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Alpha158 VWAP ratio: typical_price / previous_close.
/// Alpha158 formula: $vwap / Ref($close,1)
/// Uses typical price (H+L+C)/3 as the VWAP proxy when no explicit VWAP field.
class Alpha158VwapRatio : public Indicator<Alpha158VwapRatio> {
public:
    explicit Alpha158VwapRatio(const Line<double>& high,
                               const Line<double>& low,
                               const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double tp = (high_.data()[idx] + low_.data()[idx] + close_.data()[idx]) / 3.0;
        const double prev_close = close_.data()[idx - 1];
        if (prev_close == 0.0) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }
        line_.forward(tp / prev_close);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

} // namespace stratforge
