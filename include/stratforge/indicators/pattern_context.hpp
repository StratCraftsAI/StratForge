#pragma once

/// @file pattern_context.hpp
/// @brief Line-agnostic candle-pattern context wrappers for TICKET_SF012.

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/pattern_context_common.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace stratforge {

namespace pattern_context {

inline constexpr double unavailable = std::numeric_limits<double>::quiet_NaN();

[[nodiscard]] inline int direction(double signal) noexcept {
    if (!std::isfinite(signal) || signal == 0.0) return 0;
    return signal > 0.0 ? 1 : -1;
}

[[nodiscard]] inline double clamp_component(double value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : unavailable;
}

} // namespace pattern_context

class PatternConfirmation : public Indicator<PatternConfirmation> {
public:
    PatternConfirmation(const Line<double>& signal, const Line<double>& price,
                        const Line<double>& confirmation_threshold,
                        const Line<double>& invalidation_threshold,
                        std::size_t expiry_bars,
                        SignalOverlapPolicy overlap = SignalOverlapPolicy::replace)
        : signal_(signal), price_(price), confirmation_threshold_(confirmation_threshold),
          invalidation_threshold_(invalidation_threshold), lifecycle_(expiry_bars, overlap) {}

    void next_impl() {
        reserve_if_needed();
        const auto idx = signal_.index();
        const auto output = lifecycle_.update({signal_.data()[idx], price_.data()[idx],
                                               confirmation_threshold_.data()[idx],
                                               invalidation_threshold_.data()[idx]});
        line_.forward(static_cast<double>(static_cast<int>(output.state)));
        event_id_.forward(static_cast<double>(output.event_id));
        age_.forward(static_cast<double>(output.age));
        direction_.forward(static_cast<double>(output.direction));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1uz; }
    [[nodiscard]] std::size_t accessor_minimum_period_impl(std::string_view) const noexcept {
        return 1uz;
    }
    [[nodiscard]] const Line<double>& state() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& event_id() const noexcept { return event_id_; }
    [[nodiscard]] const Line<double>& age() const noexcept { return age_; }
    [[nodiscard]] const Line<double>& direction() const noexcept { return direction_; }
    [[nodiscard]] std::size_t expiry_bars() const noexcept { return lifecycle_.expiry_bars(); }
    [[nodiscard]] SignalOverlapPolicy overlap_policy() const noexcept {
        return lifecycle_.overlap_policy();
    }

private:
    void reserve_if_needed() {
        if (!line_.empty()) return;
        reserve_output(signal_.size());
        event_id_.data().reserve(signal_.size());
        age_.data().reserve(signal_.size());
        direction_.data().reserve(signal_.size());
    }

    const Line<double>& signal_;
    const Line<double>& price_;
    const Line<double>& confirmation_threshold_;
    const Line<double>& invalidation_threshold_;
    pattern_context::SignalLifecycle lifecycle_;
    Line<double> event_id_;
    Line<double> age_;
    Line<double> direction_;
};

class TrendContext : public Indicator<TrendContext> {
public:
    TrendContext(const Line<double>& signal, const Line<double>& trend)
        : signal_(signal), trend_(trend) {}

    void next_impl() {
        if (line_.empty()) {
            reserve_output(signal_.size());
            direction_.data().reserve(signal_.size());
        }
        const auto idx = signal_.index();
        const int signal_direction = pattern_context::direction(signal_.data()[idx]);
        const int trend_direction = pattern_context::direction(trend_.data()[idx]);
        const int direction = signal_direction != 0 && std::isfinite(trend_.data()[idx])
                                  ? signal_direction
                                  : 0;
        const int alignment = direction == 0 || trend_direction == 0
                                  ? 0
                                  : (direction == trend_direction ? 1 : -1);
        line_.forward(static_cast<double>(alignment));
        direction_.forward(static_cast<double>(direction));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1uz; }
    [[nodiscard]] const Line<double>& alignment() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& direction() const noexcept { return direction_; }

private:
    const Line<double>& signal_;
    const Line<double>& trend_;
    Line<double> direction_;
};

class VolumeConfirmation : public Indicator<VolumeConfirmation> {
public:
    VolumeConfirmation(const Line<double>& signal, const Line<double>& volume,
                       const Line<double>& baseline, double threshold = 1.0)
        : signal_(signal), volume_(volume), baseline_(baseline), threshold_(threshold) {
        if (!std::isfinite(threshold_) || threshold_ < 0.0) {
            throw std::invalid_argument("volume confirmation threshold must be finite and non-negative");
        }
    }

    void next_impl() {
        if (line_.empty()) {
            reserve_output(signal_.size());
            confirmed_.data().reserve(signal_.size());
        }
        const auto idx = signal_.index();
        const double volume = volume_.data()[idx];
        const double baseline = baseline_.data()[idx];
        double ratio = pattern_context::unavailable;
        if (pattern_context::direction(signal_.data()[idx]) != 0 && std::isfinite(volume) &&
            volume >= 0.0 && std::isfinite(baseline) && baseline > 0.0) {
            ratio = volume / baseline;
        }
        line_.forward(ratio);
        confirmed_.forward(std::isfinite(ratio) && ratio >= threshold_ ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1uz; }
    [[nodiscard]] const Line<double>& ratio() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& confirmed() const noexcept { return confirmed_; }
    [[nodiscard]] double threshold() const noexcept { return threshold_; }

private:
    const Line<double>& signal_;
    const Line<double>& volume_;
    const Line<double>& baseline_;
    double threshold_;
    Line<double> confirmed_;
};

class SupportResistanceContext : public Indicator<SupportResistanceContext> {
public:
    SupportResistanceContext(const Line<double>& signal, const Line<double>& price,
                             const Line<double>& support,
                             const Line<double>& resistance)
        : signal_(signal), price_(price), support_(support), resistance_(resistance) {}

    void next_impl() {
        if (line_.empty()) {
            reserve_output(signal_.size());
            resistance_distance_.data().reserve(signal_.size());
        }
        const auto idx = signal_.index();
        const double price = price_.data()[idx];
        const bool has_signal = pattern_context::direction(signal_.data()[idx]) != 0;
        const double support = support_.data()[idx];
        const double resistance = resistance_.data()[idx];
        line_.forward(has_signal && std::isfinite(price) && std::isfinite(support)
                          ? price - support
                          : pattern_context::unavailable);
        resistance_distance_.forward(has_signal && std::isfinite(price) &&
                                                 std::isfinite(resistance)
                                         ? price - resistance
                                         : pattern_context::unavailable);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1uz; }
    [[nodiscard]] std::size_t accessor_minimum_period_impl(std::string_view) const noexcept {
        return 1uz;
    }
    [[nodiscard]] const Line<double>& support_distance() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& resistance_distance() const noexcept {
        return resistance_distance_;
    }

private:
    const Line<double>& signal_;
    const Line<double>& price_;
    const Line<double>& support_;
    const Line<double>& resistance_;
    Line<double> resistance_distance_;
};

struct CandleQualityWeights {
    double geometry{1.0};
    double trend{1.0};
    double confirmation{1.0};
    double volume{1.0};
};

class CandleQualityScore : public Indicator<CandleQualityScore> {
public:
    CandleQualityScore(const Line<double>& geometry, const Line<double>& trend,
                       const Line<double>& confirmation, const Line<double>& volume,
                       CandleQualityWeights weights = {})
        : geometry_(geometry), trend_(trend), confirmation_(confirmation), volume_(volume),
          weights_(weights) {
        for (double weight : weight_array()) {
            if (!std::isfinite(weight) || weight < 0.0) {
                throw std::invalid_argument("candle quality weights must be finite and non-negative");
            }
        }
    }

    void next_impl() {
        reserve_if_needed();
        const auto idx = geometry_.index();
        const std::array<double, 4> components{
            pattern_context::clamp_component(geometry_.data()[idx]),
            pattern_context::clamp_component(trend_.data()[idx]),
            pattern_context::clamp_component(confirmation_.data()[idx]),
            pattern_context::clamp_component(volume_.data()[idx])};
        geometry_component_.forward(components[0]);
        trend_component_.forward(components[1]);
        confirmation_component_.forward(components[2]);
        volume_component_.forward(components[3]);

        const auto weights = weight_array();
        double numerator = 0.0;
        double denominator = 0.0;
        bool valid = true;
        for (std::size_t i = 0; i < components.size(); ++i) {
            valid = valid && std::isfinite(components[i]);
            numerator += weights[i] * components[i];
            denominator += weights[i];
        }
        line_.forward(valid && denominator > 0.0 ? numerator / denominator
                                                  : pattern_context::unavailable);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1uz; }
    [[nodiscard]] std::size_t accessor_minimum_period_impl(std::string_view) const noexcept {
        return 1uz;
    }
    [[nodiscard]] const Line<double>& score() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& geometry() const noexcept { return geometry_component_; }
    [[nodiscard]] const Line<double>& trend() const noexcept { return trend_component_; }
    [[nodiscard]] const Line<double>& confirmation() const noexcept {
        return confirmation_component_;
    }
    [[nodiscard]] const Line<double>& volume() const noexcept { return volume_component_; }
    [[nodiscard]] CandleQualityWeights weights() const noexcept { return weights_; }

private:
    [[nodiscard]] std::array<double, 4> weight_array() const noexcept {
        return {weights_.geometry, weights_.trend, weights_.confirmation, weights_.volume};
    }

    void reserve_if_needed() {
        if (!line_.empty()) return;
        reserve_output(geometry_.size());
        geometry_component_.data().reserve(geometry_.size());
        trend_component_.data().reserve(geometry_.size());
        confirmation_component_.data().reserve(geometry_.size());
        volume_component_.data().reserve(geometry_.size());
    }

    const Line<double>& geometry_;
    const Line<double>& trend_;
    const Line<double>& confirmation_;
    const Line<double>& volume_;
    CandleQualityWeights weights_;
    Line<double> geometry_component_;
    Line<double> trend_component_;
    Line<double> confirmation_component_;
    Line<double> volume_component_;
};

} // namespace stratforge
