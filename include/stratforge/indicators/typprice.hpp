#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>

namespace stratforge {

/// Typical Price: (High + Low + Close) / 3.
class TypPrice : public Indicator<TypPrice> {
public:
    TypPrice(const Line<double>& high, const Line<double>& low,
             const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        line_.forward((high_.data()[idx] + low_.data()[idx] +
                       close_.data()[idx]) / 3.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

using TYPPRICE = TypPrice;
using TypicalPrice = TypPrice;

} // namespace stratforge
