#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Archer Moving Averages Trends (AMAT): compares fast vs slow EMA
/// to detect trend direction. Outputs +1 (uptrend), -1 (downtrend), 0 (neutral).
class AMAT : public Indicator<AMAT> {
public:
    explicit AMAT(const Line<double>& source,
                  std::size_t fast_period = 8uz,
                  std::size_t slow_period = 21uz)
        : source_(source)
        , fast_period_(fast_period == 0 ? 1 : fast_period)
        , slow_period_(slow_period == 0 ? 1 : slow_period)
        , fast_mult_(2.0 / (static_cast<double>(fast_period_) + 1.0))
        , slow_mult_(2.0 / (static_cast<double>(slow_period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        const double val = source_.data()[idx];

        if (idx == 0) {
            fast_ema_ = val;
            slow_ema_ = val;
            line_.forward(0.0);
            return;
        }

        fast_ema_ = (val - fast_ema_) * fast_mult_ + fast_ema_;
        slow_ema_ = (val - slow_ema_) * slow_mult_ + slow_ema_;

        if (fast_ema_ > slow_ema_)
            line_.forward(1.0);
        else if (fast_ema_ < slow_ema_)
            line_.forward(-1.0);
        else
            line_.forward(0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& source_;
    std::size_t fast_period_;
    std::size_t slow_period_;
    double fast_mult_;
    double slow_mult_;
    double fast_ema_ = 0.0;
    double slow_ema_ = 0.0;
};

using ArcherMovingAveragesTrends = AMAT;

} // namespace stratforge
