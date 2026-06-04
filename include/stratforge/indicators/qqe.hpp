#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Quantitative Qualitative Estimation (QQE).
/// Smoothed RSI with dynamic Bollinger-like bands.
class QQE : public Indicator<QQE> {
public:
    explicit QQE(const Line<double>& source, std::size_t period = 14uz,
                 double factor = 4.236, std::size_t smooth = 5uz)
        : source_(source)
        , period_(period == 0 ? 1 : period)
        , factor_(factor)
        , smooth_(smooth == 0 ? 1 : smooth)
        , rsi_mult_(2.0 / (static_cast<double>(period_) + 1.0))
        , smooth_mult_(2.0 / (static_cast<double>(smooth_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();

        if (idx == 0) {
            prev_close_ = source_.data()[idx];
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double change = source_.data()[idx] - prev_close_;
        prev_close_ = source_.data()[idx];

        const double gain = change > 0.0 ? change : 0.0;
        const double loss = change < 0.0 ? -change : 0.0;

        if (!rsi_initialized_) {
            avg_gain_ = gain;
            avg_loss_ = loss;
            rsi_initialized_ = true;
        } else {
            avg_gain_ = (gain - avg_gain_) * rsi_mult_ + avg_gain_;
            avg_loss_ = (loss - avg_loss_) * rsi_mult_ + avg_loss_;
        }

        const double rs = avg_loss_ == 0.0 ? 100.0 : avg_gain_ / avg_loss_;
        const double rsi = avg_loss_ == 0.0 ? 100.0 : 100.0 - (100.0 / (1.0 + rs));

        if (!smooth_initialized_) {
            smoothed_rsi_ = rsi;
            smooth_initialized_ = true;
        } else {
            smoothed_rsi_ = (rsi - smoothed_rsi_) * smooth_mult_ + smoothed_rsi_;
        }

        const double delta = std::abs(smoothed_rsi_ - prev_smoothed_rsi_);
        if (!atr_initialized_) {
            atr_rsi_ = delta;
            atr_initialized_ = true;
        } else {
            atr_rsi_ = (delta - atr_rsi_) * rsi_mult_ + atr_rsi_;
        }

        if (!dar_initialized_) {
            dar_ = atr_rsi_;
            dar_initialized_ = true;
        } else {
            dar_ = (atr_rsi_ - dar_) * rsi_mult_ + dar_;
        }

        const double band = dar_ * factor_;
        if (smoothed_rsi_ > prev_level_ + band) {
            prev_level_ = smoothed_rsi_ - band;
        } else if (smoothed_rsi_ < prev_level_ - band) {
            prev_level_ = smoothed_rsi_ + band;
        }

        prev_smoothed_rsi_ = smoothed_rsi_;
        line_.forward(smoothed_rsi_ - prev_level_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double factor_;
    std::size_t smooth_;
    double rsi_mult_;
    double smooth_mult_;
    double prev_close_ = 0.0;
    double avg_gain_ = 0.0;
    double avg_loss_ = 0.0;
    double smoothed_rsi_ = 50.0;
    double prev_smoothed_rsi_ = 50.0;
    double atr_rsi_ = 0.0;
    double dar_ = 0.0;
    double prev_level_ = 50.0;
    bool rsi_initialized_ = false;
    bool smooth_initialized_ = false;
    bool atr_initialized_ = false;
    bool dar_initialized_ = false;
};

using QuantQualEstimation = QQE;

} // namespace stratforge
