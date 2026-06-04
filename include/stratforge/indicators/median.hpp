#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace stratforge {

/// Rolling Median over a trailing window.
class Median : public Indicator<Median> {
public:
    explicit Median(const Line<double>& source, std::size_t period = 30uz)
        : source_(source), period_(period == 0 ? 1 : period), buf_(period_) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const std::size_t start = idx - period_ + 1;
        for (std::size_t i = 0; i < period_; ++i)
            buf_[i] = source_.data()[start + i];

        const std::size_t mid = period_ / 2;
        std::nth_element(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(mid), buf_.end());

        if (period_ % 2 == 1) {
            line_.forward(buf_[mid]);
        } else {
            const double upper = buf_[mid];
            const double lower = *std::max_element(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(mid));
            line_.forward((lower + upper) / 2.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    std::vector<double> buf_;
};

using MEDIAN = Median;
using RollingMedian = Median;

} // namespace stratforge
