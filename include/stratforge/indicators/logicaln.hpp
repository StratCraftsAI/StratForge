#pragma once

#include <stratforge/indicators/periodn.hpp>

#include <cmath>
#include <limits>

namespace stratforge {

/// Whether any value in the trailing period evaluates to true.
class AnyN : public PeriodN<AnyN> {
public:
    explicit AnyN(const Line<double>& source, std::size_t period)
        : PeriodN(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source().size()); }
        if (in_warmup()) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto idx = source().index();
        bool result = false;
        for (std::size_t i = 0; i < period(); ++i) {
            const double value = source().data()[idx - period() + 1 + i];
            result = result || (value != 0.0 && !std::isnan(value));
        }

        line_.forward(result ? 1.0 : 0.0);
    }
};

/// Whether all values in the trailing period evaluate to true.
class AllN : public PeriodN<AllN> {
public:
    explicit AllN(const Line<double>& source, std::size_t period)
        : PeriodN(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source().size()); }
        if (in_warmup()) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto idx = source().index();
        bool result = true;
        for (std::size_t i = 0; i < period(); ++i) {
            const double value = source().data()[idx - period() + 1 + i];
            result = result && (value != 0.0 && !std::isnan(value));
        }

        line_.forward(result ? 1.0 : 0.0);
    }
};

} // namespace stratforge
