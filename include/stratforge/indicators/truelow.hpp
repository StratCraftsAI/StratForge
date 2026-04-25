#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Minimum of current low and previous close.
class TrueLow : public Indicator<TrueLow> {
public:
    TrueLow(const Line<double>& low, const Line<double>& close)
        : low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx == 0) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(std::min(low_.data()[idx], close_.data()[idx - 1]));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

private:
    const Line<double>& low_;
    const Line<double>& close_;
};

} // namespace stratforge
