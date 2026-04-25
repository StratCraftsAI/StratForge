#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Momentum: current price minus price N bars ago.
class Momentum : public Indicator<Momentum> {
public:
    explicit Momentum(const Line<double>& source, std::size_t period = 12)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(source_.data()[idx] - source_.data()[idx - period_]);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

/// Rate of change: percent change relative to N bars ago.
class ROC : public Indicator<ROC> {
public:
    explicit ROC(const Line<double>& source, std::size_t period = 12)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double previous = source_.data()[idx - period_];
        line_.forward((source_.data()[idx] - previous) / previous);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

/// Rate of change with base 100.
class ROC100 : public Indicator<ROC100> {
public:
    explicit ROC100(const Line<double>& source, std::size_t period = 12)
        : source_(source), roc_(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        roc_.next();
        const double value = roc_.line().data().back();
        line_.forward(std::isnan(value) ? value : (value * 100.0));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return roc_.minimum_period();
    }

private:
    const Line<double>& source_;
    ROC roc_;
};

/// Momentum oscillator expressed as 100 * current / previous-N.
class MomentumOscillator : public Indicator<MomentumOscillator> {
public:
    explicit MomentumOscillator(const Line<double>& source, std::size_t period = 12)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(100.0 * (source_.data()[idx] / source_.data()[idx - period_]));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

} // namespace stratforge
