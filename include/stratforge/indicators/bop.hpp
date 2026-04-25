#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Balance of Power: (Close - Open) / (High - Low).
class BalanceOfPower : public Indicator<BalanceOfPower> {
public:
    BalanceOfPower(const Line<double>& open, const Line<double>& high,
                   const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double range = high_.data()[idx] - low_.data()[idx];
        if (range == 0.0) {
            line_.forward(0.0);
            return;
        }
        line_.forward((close_.data()[idx] - open_.data()[idx]) / range);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

using BOP = BalanceOfPower;

} // namespace stratforge
