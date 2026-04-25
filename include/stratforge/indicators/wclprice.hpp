#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>

namespace stratforge {

/// Weighted Close Price: (High + Low + 2 * Close) / 4.
class WclPrice : public Indicator<WclPrice> {
public:
    WclPrice(const Line<double>& high, const Line<double>& low,
             const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        line_.forward((high_.data()[idx] + low_.data()[idx] +
                       2.0 * close_.data()[idx]) / 4.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

using WCLPRICE = WclPrice;
using WeightedClosePrice = WclPrice;

} // namespace stratforge
