#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>

namespace stratforge {

/// Average Price: (Open + High + Low + Close) / 4.
class AvgPrice : public Indicator<AvgPrice> {
public:
    AvgPrice(const Line<double>& open, const Line<double>& high,
             const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        line_.forward((open_.data()[idx] + high_.data()[idx] +
                       low_.data()[idx] + close_.data()[idx]) / 4.0);
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

using AVGPRICE = AvgPrice;
using AveragePrice = AvgPrice;

} // namespace stratforge
