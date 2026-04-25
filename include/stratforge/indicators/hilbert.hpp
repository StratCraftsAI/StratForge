#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace stratforge {

namespace detail {

/// Shared Hilbert Transform DSP state matching TA-Lib's exact implementation.
/// Even/odd branching is based on the absolute bar index (today % 2).
struct HilbertState {
    // 4-period WMA state
    double period_wma_sum = 0.0;
    double period_wma_sub = 0.0;
    double trailing_wma_value = 0.0;
    double smoothed_value = 0.0;

    // Hilbert Transform circular buffers (size 3 each, even and odd)
    std::array<double, 3> detrender_odd{};
    std::array<double, 3> detrender_even{};
    double detrender = 0.0;
    double prev_detrender_odd = 0.0;
    double prev_detrender_even = 0.0;
    double prev_detrender_input_odd = 0.0;
    double prev_detrender_input_even = 0.0;

    std::array<double, 3> q1_odd{};
    std::array<double, 3> q1_even{};
    double Q1 = 0.0;
    double prev_q1_odd = 0.0;
    double prev_q1_even = 0.0;
    double prev_q1_input_odd = 0.0;
    double prev_q1_input_even = 0.0;

    std::array<double, 3> jI_odd{};
    std::array<double, 3> jI_even{};
    double jI = 0.0;
    double prev_jI_odd = 0.0;
    double prev_jI_even = 0.0;
    double prev_jI_input_odd = 0.0;
    double prev_jI_input_even = 0.0;

    std::array<double, 3> jQ_odd{};
    std::array<double, 3> jQ_even{};
    double jQ = 0.0;
    double prev_jQ_odd = 0.0;
    double prev_jQ_even = 0.0;
    double prev_jQ_input_odd = 0.0;
    double prev_jQ_input_even = 0.0;

    // I1 history: separate even/odd prev2/prev3 for delayed in-phase component
    double I1_for_even_prev2 = 0.0;
    double I1_for_even_prev3 = 0.0;
    double I1_for_odd_prev2 = 0.0;
    double I1_for_odd_prev3 = 0.0;

    // Quadrature smoothing
    double I2 = 0.0;
    double Q2 = 0.0;
    double prev_I2 = 0.0;
    double prev_Q2 = 0.0;

    // Period detection
    double Re = 0.0;
    double Im = 0.0;
    double period = 0.0;
    double smooth_period = 0.0;

    // Hilbert circular buffer index (wraps at 3, only increments on even bars)
    int hilbert_idx = 0;

    // TA-Lib constants
    static constexpr double a = 0.0962;
    static constexpr double b = 0.5769;
    static constexpr double rad2deg = 180.0 / std::numbers::pi;

    /// Perform Hilbert Transform on a single variable (even path).
    static void ht_even(
        double input, double adjusted_prev_period,
        std::array<double, 3>& buf, int idx,
        double& var, double& prev_var, double& prev_input) noexcept
    {
        const double ht = a * input;
        var = -buf[static_cast<std::size_t>(idx)];
        buf[static_cast<std::size_t>(idx)] = ht;
        var += ht;
        var -= prev_var;
        prev_var = b * prev_input;
        var += prev_var;
        prev_input = input;
        var *= adjusted_prev_period;
    }

    /// Perform Hilbert Transform on a single variable (odd path).
    static void ht_odd(
        double input, double adjusted_prev_period,
        std::array<double, 3>& buf, int idx,
        double& var, double& prev_var, double& prev_input) noexcept
    {
        const double ht = a * input;
        var = -buf[static_cast<std::size_t>(idx)];
        buf[static_cast<std::size_t>(idx)] = ht;
        var += ht;
        var -= prev_var;
        prev_var = b * prev_input;
        var += prev_var;
        prev_input = input;
        var *= adjusted_prev_period;
    }

    /// Run the full Hilbert chain for the current bar.
    /// @param today  The absolute bar index (0-based) — used for even/odd check.
    /// @return Phase angle in degrees (for MAMA use).
    double compute_step(int today) noexcept {
        const double adjusted_prev_period = 0.075 * period + 0.54;
        double phase = 0.0;

        if ((today % 2) == 0) {
            // Even bar
            ht_even(smoothed_value, adjusted_prev_period,
                detrender_even, hilbert_idx,
                detrender, prev_detrender_even, prev_detrender_input_even);

            ht_even(detrender, adjusted_prev_period,
                q1_even, hilbert_idx,
                Q1, prev_q1_even, prev_q1_input_even);

            ht_even(I1_for_even_prev3, adjusted_prev_period,
                jI_even, hilbert_idx,
                jI, prev_jI_even, prev_jI_input_even);

            ht_even(Q1, adjusted_prev_period,
                jQ_even, hilbert_idx,
                jQ, prev_jQ_even, prev_jQ_input_even);

            if (++hilbert_idx == 3) {
                hilbert_idx = 0;
            }

            Q2 = (0.2 * (Q1 + jI)) + (0.8 * prev_Q2);
            I2 = (0.2 * (I1_for_even_prev3 - jQ)) + (0.8 * prev_I2);

            // Update opposite side's I1 history
            I1_for_odd_prev3 = I1_for_odd_prev2;
            I1_for_odd_prev2 = detrender;

            if (I1_for_even_prev3 != 0.0) {
                phase = std::atan(Q1 / I1_for_even_prev3) * rad2deg;
            }
        } else {
            // Odd bar
            ht_odd(smoothed_value, adjusted_prev_period,
                detrender_odd, hilbert_idx,
                detrender, prev_detrender_odd, prev_detrender_input_odd);

            ht_odd(detrender, adjusted_prev_period,
                q1_odd, hilbert_idx,
                Q1, prev_q1_odd, prev_q1_input_odd);

            ht_odd(I1_for_odd_prev3, adjusted_prev_period,
                jI_odd, hilbert_idx,
                jI, prev_jI_odd, prev_jI_input_odd);

            ht_odd(Q1, adjusted_prev_period,
                jQ_odd, hilbert_idx,
                jQ, prev_jQ_odd, prev_jQ_input_odd);

            Q2 = (0.2 * (Q1 + jI)) + (0.8 * prev_Q2);
            I2 = (0.2 * (I1_for_odd_prev3 - jQ)) + (0.8 * prev_I2);

            // Update opposite side's I1 history
            I1_for_even_prev3 = I1_for_even_prev2;
            I1_for_even_prev2 = detrender;

            if (I1_for_odd_prev3 != 0.0) {
                phase = std::atan(Q1 / I1_for_odd_prev3) * rad2deg;
            }
        }

        // Period detection
        Re = (0.2 * ((I2 * prev_I2) + (Q2 * prev_Q2))) + (0.8 * Re);
        Im = (0.2 * ((I2 * prev_Q2) - (Q2 * prev_I2))) + (0.8 * Im);
        prev_Q2 = Q2;
        prev_I2 = I2;

        double temp_period = period;
        if (Im != 0.0 && Re != 0.0) {
            period = 360.0 / (std::atan(Im / Re) * rad2deg);
        }
        double upper = 1.5 * temp_period;
        if (period > upper) period = upper;
        double lower = 0.67 * temp_period;
        if (period < lower) period = lower;
        if (period < 6.0) {
            period = 6.0;
        } else if (period > 50.0) {
            period = 50.0;
        }
        period = (0.2 * period) + (0.8 * temp_period);
        smooth_period = (0.33 * period) + (0.67 * smooth_period);

        return phase;
    }
};

} // namespace detail

/// Hilbert Transform - Trendline (HT_TRENDLINE)
/// TA-Lib compatible. Lookback: 63 bars.
class HT_Trendline : public Indicator<HT_Trendline> {
public:
    explicit HT_Trendline(const Line<double>& source)
        : source_(source) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const auto idx = source_.index();
        const double price = source_.data()[idx];

        // TA-Lib warmup: lookback = 63
        // startIdx = lookbackTotal (63)
        // trailingWMAIdx = startIdx - lookbackTotal = 0
        // The first 3 bars are unrolled WMA init
        // Then 34 bars of WMA warmup loop
        // Then the main loop starts at bar 37 (today = 37)

        if (today_ < 3) {
            // Unrolled WMA init: bars 0, 1, 2
            if (today_ == 0) {
                ht_.period_wma_sub = price;
                ht_.period_wma_sum = price;
            } else if (today_ == 1) {
                ht_.period_wma_sub += price;
                ht_.period_wma_sum += price * 2.0;
            } else {
                ht_.period_wma_sub += price;
                ht_.period_wma_sum += price * 3.0;
            }
            ht_.trailing_wma_value = 0.0;
            today_++;
            line_.forward(nan);
            return;
        }

        // WMA step (DO_PRICE_WMA)
        do_price_wma(price);

        if (today_ < 37) {
            // Still in the 34-iteration WMA warmup
            today_++;
            line_.forward(nan);
            return;
        }

        // Run Hilbert step
        ht_.compute_step(today_);

        // DC Period
        const int dc_period = static_cast<int>(ht_.smooth_period + 0.5);

        // Average raw price over DCPeriod
        double temp = 0.0;
        for (int i = 0; i < dc_period; ++i) {
            temp += source_.data()[idx - static_cast<std::size_t>(i)];
        }
        if (dc_period > 0) {
            temp = temp / static_cast<double>(dc_period);
        }

        // Weighted trendline
        const double trendline = (4.0 * temp + 3.0 * i_trend1_ +
                                  2.0 * i_trend2_ + i_trend3_) / 10.0;
        i_trend3_ = i_trend2_;
        i_trend2_ = i_trend1_;
        i_trend1_ = temp;

        today_++;

        if (idx < 63) {
            line_.forward(nan);
        } else {
            line_.forward(trendline);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 63;
    }

private:
    void do_price_wma(double new_price) noexcept {
        ht_.period_wma_sub += new_price;
        ht_.period_wma_sub -= ht_.trailing_wma_value;
        ht_.period_wma_sum += new_price * 4.0;
        ht_.trailing_wma_value = source_.data()[source_.index() - 3];
        ht_.smoothed_value = ht_.period_wma_sum * 0.1;
        ht_.period_wma_sum -= ht_.period_wma_sub;
    }

    const Line<double>& source_;
    detail::HilbertState ht_;
    double i_trend1_ = 0.0;
    double i_trend2_ = 0.0;
    double i_trend3_ = 0.0;
    int today_ = 0;
};

using HilbertTrendline = HT_Trendline;

/// MESA Adaptive Moving Average (MAMA) with dual output: mama + fama.
/// TA-Lib compatible. Lookback: 32 bars.
class MAMA : public Indicator<MAMA> {
public:
    explicit MAMA(const Line<double>& source,
                  double fast_limit = 0.5,
                  double slow_limit = 0.05)
        : source_(source)
        , fast_limit_(fast_limit)
        , slow_limit_(slow_limit) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            fama_.data().reserve(n);
        }
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const auto idx = source_.index();
        const double price = source_.data()[idx];

        // TA-Lib MAMA warmup: lookback = 32
        // WMA init: 3 bars unrolled
        // WMA loop: 9 iterations
        // Main loop starts at bar 12

        if (today_ < 3) {
            if (today_ == 0) {
                ht_.period_wma_sub = price;
                ht_.period_wma_sum = price;
            } else if (today_ == 1) {
                ht_.period_wma_sub += price;
                ht_.period_wma_sum += price * 2.0;
            } else {
                ht_.period_wma_sub += price;
                ht_.period_wma_sum += price * 3.0;
            }
            ht_.trailing_wma_value = 0.0;
            today_++;
            line_.forward(nan);
            fama_.forward(nan);
            return;
        }

        // WMA step
        do_price_wma(price);

        if (today_ < 12) {
            // Still in WMA warmup (9 iterations after the 3 init bars)
            today_++;
            line_.forward(nan);
            fama_.forward(nan);
            return;
        }

        // Run Hilbert step (returns phase angle)
        double phase = ht_.compute_step(today_);

        // Delta phase
        double delta_phase = prev_phase_ - phase;
        prev_phase_ = phase;
        if (delta_phase < 1.0) {
            delta_phase = 1.0;
        }

        // Adaptive alpha
        double alpha;
        if (delta_phase > 1.0) {
            alpha = fast_limit_ / delta_phase;
            if (alpha < slow_limit_) {
                alpha = slow_limit_;
            }
        } else {
            alpha = fast_limit_;
        }

        // MAMA/FAMA update
        mama_value_ = alpha * price + (1.0 - alpha) * mama_value_;
        const double half_alpha = alpha * 0.5;
        fama_value_ = half_alpha * mama_value_ + (1.0 - half_alpha) * fama_value_;

        today_++;

        if (idx < 32) {
            line_.forward(nan);
            fama_.forward(nan);
        } else {
            line_.forward(mama_value_);
            fama_.forward(fama_value_);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 32;
    }

    [[nodiscard]] const Line<double>& mama() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& fama() const noexcept { return fama_; }

private:
    void do_price_wma(double new_price) noexcept {
        ht_.period_wma_sub += new_price;
        ht_.period_wma_sub -= ht_.trailing_wma_value;
        ht_.period_wma_sum += new_price * 4.0;
        ht_.trailing_wma_value = source_.data()[source_.index() - 3];
        ht_.smoothed_value = ht_.period_wma_sum * 0.1;
        ht_.period_wma_sum -= ht_.period_wma_sub;
    }

    const Line<double>& source_;
    double fast_limit_;
    double slow_limit_;
    detail::HilbertState ht_;
    Line<double> fama_;
    double mama_value_ = 0.0;
    double fama_value_ = 0.0;
    double prev_phase_ = 0.0;
    int today_ = 0;
};

using MESAAdaptiveMovingAverage = MAMA;

} // namespace stratforge
