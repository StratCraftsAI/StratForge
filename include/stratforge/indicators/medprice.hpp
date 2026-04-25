#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>

namespace stratforge {

/// Median Price: (High + Low) / 2.
class MedPrice : public Indicator<MedPrice> {
public:
    MedPrice(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        line_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
};

using MEDPRICE = MedPrice;
using MedianPrice = MedPrice;

} // namespace stratforge
