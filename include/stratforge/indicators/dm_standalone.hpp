#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Plus Directional Movement (raw, unsmoothed).
class PlusDM : public Indicator<PlusDM> {
public:
    PlusDM(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        if (idx == 0) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double upmove = high_.data()[idx] - high_.data()[idx - 1];
        const double downmove = low_.data()[idx - 1] - low_.data()[idx];
        line_.forward((upmove > downmove && upmove > 0.0) ? upmove : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
};

using PLUS_DM = PlusDM;

/// Minus Directional Movement (raw, unsmoothed).
class MinusDM : public Indicator<MinusDM> {
public:
    MinusDM(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        if (idx == 0) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double upmove = high_.data()[idx] - high_.data()[idx - 1];
        const double downmove = low_.data()[idx - 1] - low_.data()[idx];
        line_.forward((downmove > upmove && downmove > 0.0) ? downmove : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
};

using MINUS_DM = MinusDM;

} // namespace stratforge
