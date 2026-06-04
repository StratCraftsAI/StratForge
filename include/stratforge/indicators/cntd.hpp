#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// CntD: CntP - CntN = fraction(up) - fraction(down) over the trailing window.
/// Alpha158 formula: Mean($close>Ref($close,1),N) - Mean($close<Ref($close,1),N)
class CntD : public Indicator<CntD> {
public:
    explicit CntD(const Line<double>& source, std::size_t period)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double* p = source_.data().data();
        double up = 0.0;
        double dn = 0.0;
        const std::size_t start = idx - period_ + 1;
        for (std::size_t i = start; i <= idx; ++i) {
            if (p[i] > p[i - 1]) up += 1.0;
            else if (p[i] < p[i - 1]) dn += 1.0;
        }
        const double n = static_cast<double>(period_);
        line_.forward((up - dn) / n);
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
