#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>

namespace stratforge {

/// Price Distance: (high - low) / close
class PriceDistance : public Indicator<PriceDistance> {
public:
    PriceDistance(const Line<double>& high,
                 const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double c = close_.data()[idx];
        if (c == 0.0) {
            line_.forward(0.0);
            return;
        }
        line_.forward((high_.data()[idx] - low_.data()[idx]) / c);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

using PDIST = PriceDistance;

} // namespace stratforge
