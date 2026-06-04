#pragma once

#include <stratforge/indicators/periodn.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Rolling R-squared: regress source against time index {0..N-1},
/// return the coefficient of determination.
/// Alpha158 formula: Rsquared($close, N)
class RollingRsquared : public PeriodN<RollingRsquared> {
public:
    explicit RollingRsquared(const Line<double>& source, std::size_t period)
        : PeriodN(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source().size()); }
        if (in_warmup()) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto idx = source().index();
        const auto n = static_cast<double>(period());
        const double* p = &source().data()[idx - period() + 1];

        double sum_y = 0.0;
        double sum_y2 = 0.0;
        double sum_xy = 0.0;
        for (std::size_t i = 0; i < period(); ++i) {
            const double y = p[i];
            sum_y += y;
            sum_y2 += y * y;
            sum_xy += static_cast<double>(i) * y;
        }

        const double mean_x = (n - 1.0) / 2.0;
        const double mean_y = sum_y / n;
        const double sxx = (n * n - 1.0) * n / 12.0;
        const double sxy = sum_xy - n * mean_x * mean_y;
        const double syy = sum_y2 - n * mean_y * mean_y;

        if (sxx == 0.0 || syy == 0.0) {
            line_.forward(syy == 0.0 ? 1.0 : 0.0);
            return;
        }

        const double r2 = (sxy * sxy) / (sxx * syy);
        line_.forward(r2);
    }
};

} // namespace stratforge
