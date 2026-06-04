#pragma once

#include <stratforge/indicators/periodn.hpp>

#include <limits>

namespace stratforge {

/// Rolling argmax: number of bars since the highest value in the trailing window.
/// Returns 0 when the current bar is the highest, period-1 when the oldest is.
class IdxMax : public PeriodN<IdxMax> {
public:
    explicit IdxMax(const Line<double>& source, std::size_t period)
        : PeriodN(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source().size()); }
        if (in_warmup()) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto idx = source().index();
        double best = source().data()[idx];
        std::size_t best_offset = 0;
        for (std::size_t i = 1; i < period(); ++i) {
            const double v = source().data()[idx - i];
            if (v > best) {
                best = v;
                best_offset = i;
            }
        }
        line_.forward(static_cast<double>(best_offset));
    }
};

} // namespace stratforge
