#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// VSumD: VSumP - VSumN = volume on up-bars minus volume on down-bars.
/// Alpha158 formula: Sum($volume*($close>Ref($close,1)),N) - Sum($volume*($close<Ref($close,1)),N)
class VSumD : public Indicator<VSumD> {
public:
    explicit VSumD(const Line<double>& close, const Line<double>& volume, std::size_t period)
        : close_(close), volume_(volume), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double* c = close_.data().data();
        const double* v = volume_.data().data();
        double up = 0.0;
        double dn = 0.0;
        const std::size_t start = idx - period_ + 1;
        for (std::size_t i = start; i <= idx; ++i) {
            if (c[i] > c[i - 1]) up += v[i];
            else if (c[i] < c[i - 1]) dn += v[i];
        }
        line_.forward(up - dn);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    std::size_t period_;
};

} // namespace stratforge
