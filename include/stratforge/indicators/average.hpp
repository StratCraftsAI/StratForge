#pragma once

#include <stratforge/indicators/periodn.hpp>

#include <limits>

namespace stratforge {

/// Arithmetic mean over the trailing period.
class Average : public PeriodN<Average> {
public:
    explicit Average(const Line<double>& source, std::size_t period)
        : PeriodN(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source().size()); }
        if (in_warmup()) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto idx = source().index();
        double total = 0.0;
        for (std::size_t i = 0; i < period(); ++i) {
            total += source().data()[idx - period() + 1 + i];
        }

        line_.forward(total / static_cast<double>(period()));
    }
};

using ArithmeticMean = Average;
using Mean = Average;

} // namespace stratforge
