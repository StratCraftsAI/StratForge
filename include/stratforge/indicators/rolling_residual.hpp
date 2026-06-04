#pragma once

#include <stratforge/indicators/periodn.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Rolling OLS residual: regress source against time index {0..N-1},
/// return the residual of the most recent bar (i.e. actual - predicted).
/// Alpha158 formula: Residual($close, N)
class RollingResidual : public PeriodN<RollingResidual> {
public:
    explicit RollingResidual(const Line<double>& source, std::size_t period)
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
        double sum_xy = 0.0;
        for (std::size_t i = 0; i < period(); ++i) {
            sum_y += p[i];
            sum_xy += static_cast<double>(i) * p[i];
        }

        // x = {0,1,...,N-1}: mean_x = (N-1)/2, sum_x = N(N-1)/2
        // sxx = sum((x-mean_x)^2) = N(N-1)(2N-1)/6 - N*mean_x^2 ... simplified:
        // sxx = (N^2 - 1)*N/12
        const double mean_x = (n - 1.0) / 2.0;
        const double mean_y = sum_y / n;
        const double sxx = (n * n - 1.0) * n / 12.0;
        const double sxy = sum_xy - n * mean_x * mean_y;

        const double slope = sxx == 0.0 ? 0.0 : sxy / sxx;
        const double intercept = mean_y - slope * mean_x;
        const double predicted = slope * (n - 1.0) + intercept;
        line_.forward(p[period() - 1] - predicted);
    }
};

} // namespace stratforge
