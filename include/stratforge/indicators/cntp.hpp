#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// CntP: fraction of bars in the trailing window where close > previous close.
/// Alpha158 formula: Mean($close > Ref($close,1), N)
class CntP : public Indicator<CntP> {
public:
    explicit CntP(const Line<double>& source, std::size_t period)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double* p = source_.data().data();
        double count = 0.0;
        const std::size_t start = idx - period_ + 1;
        for (std::size_t i = start; i <= idx; ++i) {
            if (p[i] > p[i - 1]) count += 1.0;
        }
        line_.forward(count / static_cast<double>(period_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

} // namespace stratforge
