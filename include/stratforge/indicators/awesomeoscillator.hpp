#pragma once

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/sma.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Bill Williams Awesome Oscillator using median price.
class AwesomeOscillator : public Indicator<AwesomeOscillator> {
public:
    explicit AwesomeOscillator(const Line<double>& high,
                               const Line<double>& low,
                               std::size_t fast = 5uz,
                               std::size_t slow = 34uz)
        : high_(high), low_(low), fast_sma_(median_, fast), slow_sma_(median_, slow) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            median_.data().reserve(n);
        }
        median_.forward((high_.data()[high_.index()] + low_.data()[low_.index()]) / 2.0);
        fast_sma_.next();
        slow_sma_.next();

        const double fast = fast_sma_.line().data().back();
        const double slow = slow_sma_.line().data().back();
        if (std::isnan(fast) || std::isnan(slow)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(fast - slow);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return slow_sma_.minimum_period();
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    Line<double> median_;
    SMA fast_sma_;
    SMA slow_sma_;
};

using AwesomeOsc = AwesomeOscillator;
using AO = AwesomeOscillator;

/// Bill Williams Acceleration/Deceleration Oscillator.
class AccelerationDecelerationOscillator : public Indicator<AccelerationDecelerationOscillator> {
public:
    explicit AccelerationDecelerationOscillator(const Line<double>& high,
                                                const Line<double>& low,
                                                std::size_t period = 5uz)
        : high_(high), awesome_(high, low), average_(awesome_.line(), period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        awesome_.next();
        average_.next();

        const double ao = awesome_.line().data().back();
        const double avg = average_.line().data().back();
        if (std::isnan(ao) || std::isnan(avg)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(ao - avg);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return awesome_.minimum_period() + average_.minimum_period() - 1;
    }

private:
    const Line<double>& high_;
    AwesomeOscillator awesome_;
    SMA average_;
};

using AccDeOsc = AccelerationDecelerationOscillator;

} // namespace stratforge
