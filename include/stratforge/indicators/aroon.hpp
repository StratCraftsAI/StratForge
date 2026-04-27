#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Aroon Up.
class AroonUp : public Indicator<AroonUp> {
public:
    AroonUp(const Line<double>& high, std::size_t period = 14uz)
        : high_(high), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const std::size_t distance = first_highest_distance(idx);
        line_.forward((100.0 / static_cast<double>(period_)) *
                      static_cast<double>(period_ - distance));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    [[nodiscard]] std::size_t first_highest_distance(std::size_t idx) const noexcept {
        const std::size_t lookback = period_ + 1;
        double highest = high_.data()[idx - period_];
        for (std::size_t i = 1; i < lookback; ++i) {
            const double candidate = high_.data()[idx - period_ + i];
            if (candidate > highest) {
                highest = candidate;
            }
        }

        for (std::size_t distance = 0; distance < lookback; ++distance) {
            if (high_.data()[idx - distance] == highest) {
                return distance;
            }
        }
        return period_;
    }

    const Line<double>& high_;
    std::size_t period_;
};

/// Aroon Down.
class AroonDown : public Indicator<AroonDown> {
public:
    AroonDown(const Line<double>& low, std::size_t period = 14uz)
        : low_(low), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(low_.size()); }
        const auto idx = low_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const std::size_t distance = first_lowest_distance(idx);
        line_.forward((100.0 / static_cast<double>(period_)) *
                      static_cast<double>(period_ - distance));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    [[nodiscard]] std::size_t first_lowest_distance(std::size_t idx) const noexcept {
        const std::size_t lookback = period_ + 1;
        double lowest = low_.data()[idx - period_];
        for (std::size_t i = 1; i < lookback; ++i) {
            const double candidate = low_.data()[idx - period_ + i];
            if (candidate < lowest) {
                lowest = candidate;
            }
        }

        for (std::size_t distance = 0; distance < lookback; ++distance) {
            if (low_.data()[idx - distance] == lowest) {
                return distance;
            }
        }
        return period_;
    }

    const Line<double>& low_;
    std::size_t period_;
};

/// Aroon indicator exposing up/down lines and oscillator in line().
class Aroon : public Indicator<Aroon> {
public:
    Aroon(const Line<double>& high, const Line<double>& low, std::size_t period = 14uz)
        : high_(high), aroon_up_(high, period), aroon_down_(low, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        aroon_up_.next();
        aroon_down_.next();

        const double up = aroon_up_.line().data().back();
        const double down = aroon_down_.line().data().back();
        if (std::isnan(up) || std::isnan(down)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(up - down);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return aroon_up_.minimum_period();
    }

    [[nodiscard]] const Line<double>& up() const noexcept { return aroon_up_.line(); }
    [[nodiscard]] const Line<double>& down() const noexcept { return aroon_down_.line(); }

private:
    const Line<double>& high_;
    AroonUp aroon_up_;
    AroonDown aroon_down_;
};

class AroonOscillator : public Aroon {
public:
    using Aroon::Aroon;
};

class AroonUpDown : public Indicator<AroonUpDown> {
public:
    AroonUpDown(const Line<double>& high, const Line<double>& low, std::size_t period = 14uz)
        : high_(high), up_(high, period), down_(low, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        up_.next();
        down_.next();
        line_.forward(std::numeric_limits<double>::quiet_NaN());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return up_.minimum_period();
    }

    [[nodiscard]] const Line<double>& up() const noexcept { return up_.line(); }
    [[nodiscard]] const Line<double>& down() const noexcept { return down_.line(); }
    [[nodiscard]] const Line<double>& aroonup() const noexcept { return up_.line(); }
    [[nodiscard]] const Line<double>& aroondown() const noexcept { return down_.line(); }

private:
    const Line<double>& high_;
    AroonUp up_;
    AroonDown down_;
};

class AroonUpDownOscillator : public Indicator<AroonUpDownOscillator> {
public:
    AroonUpDownOscillator(const Line<double>& high, const Line<double>& low, std::size_t period = 14uz)
        : high_(high), updown_(high, low, period), osc_(high, low, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        updown_.next();
        osc_.next();
        line_.forward(osc_.line().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return osc_.minimum_period();
    }

    [[nodiscard]] const Line<double>& up() const noexcept { return updown_.up(); }
    [[nodiscard]] const Line<double>& down() const noexcept { return updown_.down(); }

private:
    const Line<double>& high_;
    AroonUpDown updown_;
    AroonOscillator osc_;
};

using AroonIndicator = AroonUpDown;
using AroonOsc = AroonOscillator;
using AroonUpDownOsc = AroonUpDownOscillator;

} // namespace stratforge
