#pragma once

#include <stratforge/indicators/periodn.hpp>

#include <limits>

namespace stratforge {

/// Sum of the trailing period using fsum-style precision.
/// Note: scalar loop — typical period (5-50) is too small for SIMD benefit.
class SumN : public PeriodN<SumN> {
public:
    explicit SumN(const Line<double>& source, std::size_t period)
        : PeriodN(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source().size()); }
        if (in_warmup()) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto idx = source().index();
        const double* p = &source().data()[idx - period() + 1];
        double sum = 0.0;
        for (std::size_t i = 0; i < period(); ++i) { sum += p[i]; }
        line_.forward(sum);
    }
};

using Sum = SumN;

} // namespace stratforge
