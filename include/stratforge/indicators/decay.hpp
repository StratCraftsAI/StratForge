#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Linear Decay: value decreases by 1/period each bar after a peak, clamped to 0.
class Decay : public Indicator<Decay> {
public:
    explicit Decay(const Line<double>& source, std::size_t period = 5uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        const double current = source_.data()[idx];
        const double step = 1.0 / static_cast<double>(period_);

        if (idx == 0) {
            line_.forward(current);
            return;
        }

        const double prev = line_.data()[idx - 1];
        line_.forward(std::max(current, prev - step));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using DECAY = Decay;

} // namespace stratforge
