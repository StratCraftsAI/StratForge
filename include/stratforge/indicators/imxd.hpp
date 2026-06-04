#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/idxmax.hpp>
#include <stratforge/indicators/idxmin.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// ImxD: (IdxMax($high,N) - IdxMin($low,N)) / N
/// Measures the temporal spread between the highest high and lowest low positions.
class ImxD : public Indicator<ImxD> {
public:
    explicit ImxD(const Line<double>& high, const Line<double>& low, std::size_t period)
        : high_(high), low_(low), period_(period == 0 ? 1 : period),
          idxmax_(high, period_), idxmin_(low, period_) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        idxmax_.next();
        idxmin_.next();

        const double mx = idxmax_.line().data().back();
        const double mn = idxmin_.line().data().back();
        if (std::isnan(mx) || std::isnan(mn)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }
        line_.forward((mx - mn) / static_cast<double>(period_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    std::size_t period_;
    IdxMax idxmax_;
    IdxMin idxmin_;
};

} // namespace stratforge
