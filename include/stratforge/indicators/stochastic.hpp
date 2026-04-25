#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Stochastic oscillator with slow %K in line() and slow %D in percD().
class Stochastic : public Indicator<Stochastic> {
public:
    Stochastic(const Line<double>& high,
               const Line<double>& low,
               const Line<double>& close,
               std::size_t period = 14,
               std::size_t period_dfast = 3,
               std::size_t period_dslow = 3)
        : high_(high)
        , low_(low)
        , close_(close)
        , period_(period)
        , period_dfast_(period_dfast)
        , period_dslow_(period_dslow) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            raw_k_.data().reserve(n);
            fast_d_.data().reserve(n);
            perc_d_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();

        if (idx + 1 < period_) [[unlikely]] {
            raw_k_.forward(nan);
            fast_d_.forward(nan);
            line_.forward(nan);
            perc_d_.forward(nan);
            return;
        }

        double highest = high_.data()[idx];
        double lowest = low_.data()[idx];
        for (std::size_t i = 1; i < period_; ++i) {
            const double h = high_.data()[idx - i];
            const double l = low_.data()[idx - i];
            if (h > highest) {
                highest = h;
            }
            if (l < lowest) {
                lowest = l;
            }
        }

        const double denominator = highest - lowest;
        const double raw_k = denominator == 0.0 ? 0.0 : 100.0 * ((close_.data()[idx] - lowest) / denominator);
        raw_k_.forward(raw_k);

        if (raw_k_.size() < period_dfast_) {
            fast_d_.forward(nan);
            line_.forward(nan);
            perc_d_.forward(nan);
            return;
        }

        const double fast_d = average_tail(raw_k_, period_dfast_);
        fast_d_.forward(fast_d);
        line_.forward(fast_d);

        if (fast_d_.size() < period_dslow_) {
            perc_d_.forward(nan);
            return;
        }

        perc_d_.forward(average_tail(fast_d_, period_dslow_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + period_dfast_ + period_dslow_ - 2;
    }

    [[nodiscard]] const Line<double>& percK() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& percD() const noexcept { return perc_d_; }

private:
    [[nodiscard]] static double average_tail(const Line<double>& line, std::size_t count) noexcept {
        const auto idx = line.index();
        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            sum += line.data()[idx - i];
        }
        return sum / static_cast<double>(count);
    }

    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    std::size_t period_;
    std::size_t period_dfast_;
    std::size_t period_dslow_;
    Line<double> raw_k_;
    Line<double> fast_d_;
    Line<double> perc_d_;
};

/// Full stochastic exposing fast %K, fast %D, and slow %D.
class StochasticFull : public Indicator<StochasticFull> {
public:
    StochasticFull(const Line<double>& high,
                   const Line<double>& low,
                   const Line<double>& close,
                   std::size_t period = 14,
                   std::size_t period_dfast = 3,
                   std::size_t period_dslow = 3)
        : high_(high)
        , low_(low)
        , close_(close)
        , period_(period)
        , period_dfast_(period_dfast)
        , period_dslow_(period_dslow) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            perc_d_.data().reserve(n);
            perc_dslow_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();

        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(nan);
            perc_d_.forward(nan);
            perc_dslow_.forward(nan);
            return;
        }

        double highest = high_.data()[idx];
        double lowest = low_.data()[idx];
        for (std::size_t i = 1; i < period_; ++i) {
            const double h = high_.data()[idx - i];
            const double l = low_.data()[idx - i];
            if (h > highest) {
                highest = h;
            }
            if (l < lowest) {
                lowest = l;
            }
        }

        const double denominator = highest - lowest;
        const double raw_k = denominator == 0.0 ? 0.0 : 100.0 * ((close_.data()[idx] - lowest) / denominator);
        line_.forward(raw_k);

        if (line_.size() < period_dfast_) {
            perc_d_.forward(nan);
            perc_dslow_.forward(nan);
            return;
        }

        perc_d_.forward(average_tail(line_, period_dfast_));
        if (perc_d_.size() < period_dslow_) {
            perc_dslow_.forward(nan);
            return;
        }

        perc_dslow_.forward(average_tail(perc_d_, period_dslow_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + period_dfast_ + period_dslow_ - 2;
    }

    [[nodiscard]] const Line<double>& percK() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& percD() const noexcept { return perc_d_; }
    [[nodiscard]] const Line<double>& percDSlow() const noexcept { return perc_dslow_; }

private:
    [[nodiscard]] static double average_tail(const Line<double>& line, std::size_t count) noexcept {
        const auto idx = line.index();
        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            sum += line.data()[idx - i];
        }
        return sum / static_cast<double>(count);
    }

    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    std::size_t period_;
    std::size_t period_dfast_;
    std::size_t period_dslow_;
    Line<double> perc_d_;
    Line<double> perc_dslow_;
};

class StochasticFast : public Indicator<StochasticFast> {
public:
    StochasticFast(const Line<double>& high,
                   const Line<double>& low,
                   const Line<double>& close,
                   std::size_t period = 14,
                   std::size_t period_dfast = 3)
        : close_(close), full_(high, low, close, period, period_dfast, 1) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            perc_d_.data().reserve(n);
        }
        full_.next();
        line_.forward(full_.percK().data().back());
        perc_d_.forward(full_.percD().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return full_.minimum_period();
    }

    [[nodiscard]] const Line<double>& percK() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& percD() const noexcept { return perc_d_; }

private:
    const Line<double>& close_;
    StochasticFull full_;
    Line<double> perc_d_;
};

using StochasticSlow = Stochastic;

/// KDJ adds a J line on top of the full stochastic K/D outputs.
class KDJ : public Indicator<KDJ> {
public:
    KDJ(const Line<double>& high,
        const Line<double>& low,
        const Line<double>& close,
        std::size_t period = 9,
        std::size_t period_dfast = 3,
        std::size_t period_dslow = 3)
        : close_(close), stochastic_(high, low, close, period, period_dfast, period_dslow) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            perc_d_.data().reserve(n);
            j_.data().reserve(n);
        }
        stochastic_.next();
        const double k = stochastic_.percD().data().back();
        const double d = stochastic_.percDSlow().data().back();
        line_.forward(k);
        perc_d_.forward(d);
        if (std::isnan(k) || std::isnan(d)) {
            j_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        j_.forward((3.0 * k) - (2.0 * d));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return stochastic_.minimum_period();
    }

    [[nodiscard]] const Line<double>& k() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& d() const noexcept { return perc_d_; }
    [[nodiscard]] const Line<double>& j() const noexcept { return j_; }

private:
    const Line<double>& close_;
    StochasticFull stochastic_;
    Line<double> perc_d_;
    Line<double> j_;
};

} // namespace stratforge
