#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Short Run: count of consecutive bars where fast < slow.
class ShortRun : public Indicator<ShortRun> {
public:
    ShortRun(const Line<double>& fast, const Line<double>& slow, std::size_t period = 2uz)
        : fast_(fast), slow_(slow), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(fast_.size()); }
        const auto idx = fast_.index();

        if (fast_.data()[idx] < slow_.data()[idx]) {
            count_++;
        } else {
            count_ = 0;
        }

        line_.forward(count_ >= period_ ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& fast_;
    const Line<double>& slow_;
    std::size_t period_;
    std::size_t count_ = 0;
};

using SHORT_RUN = ShortRun;

} // namespace stratforge
