#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace stratforge {

/// Doji candle: body is negligible relative to range.
/// Output: +100 if close >= open, -100 if close < open, 0 if no pattern.
class CDLDoji : public Indicator<CDLDoji> {
public:
    CDLDoji(const Line<double>& open, const Line<double>& high,
            const Line<double>& low, const Line<double>& close,
            double body_threshold = 0.05)
        : open_(open), high_(high), low_(low), close_(close),
          body_threshold_(body_threshold) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double o = open_.data()[idx];
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];
        const double range = h - l;
        if (range <= 0.0) { line_.forward(0.0); return; }
        const double body = std::fabs(c - o);
        if (body <= body_threshold_ * range) {
            line_.forward(c >= o ? 100.0 : -100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    double body_threshold_;
};

/// Hammer: small body at top, long lower shadow, minimal upper shadow. Bullish reversal.
/// Output: +100 if pattern detected, 0 otherwise.
class CDLHammer : public Indicator<CDLHammer> {
public:
    CDLHammer(const Line<double>& open, const Line<double>& high,
              const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double o = open_.data()[idx];
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];
        const double range = h - l;
        if (range <= 0.0) { line_.forward(0.0); return; }
        const double body = std::fabs(c - o);
        const double body_top = std::max(o, c);
        const double body_bot = std::min(o, c);
        const double upper_shadow = h - body_top;
        const double lower_shadow = body_bot - l;
        if (body < range / 3.0 && lower_shadow >= 2.0 * body && upper_shadow <= body * 0.1) {
            line_.forward(100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

/// Engulfing: current body fully contains previous body, opposite colors.
/// Output: +100 bullish engulfing, -100 bearish engulfing, 0 no pattern.
class CDLEngulfing : public Indicator<CDLEngulfing> {
public:
    CDLEngulfing(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) { line_.forward(0.0); return; }
        const double o1 = open_.data()[idx - 1];
        const double c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx];
        const double c2 = close_.data()[idx];
        const double body1_top = std::max(o1, c1);
        const double body1_bot = std::min(o1, c1);
        const double body2_top = std::max(o2, c2);
        const double body2_bot = std::min(o2, c2);
        const bool prev_bearish = c1 < o1;
        const bool prev_bullish = c1 > o1;
        const bool curr_bullish = c2 > o2;
        const bool curr_bearish = c2 < o2;
        if (prev_bearish && curr_bullish && body2_top >= body1_top && body2_bot <= body1_bot) {
            line_.forward(100.0);
        } else if (prev_bullish && curr_bearish && body2_top >= body1_top && body2_bot <= body1_bot) {
            line_.forward(-100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

/// Morning Star: 3-bar bullish reversal pattern.
/// Bar 0: large bearish, Bar 1: small body (gap down), Bar 2: large bullish closing above bar 0 midpoint.
/// Output: +100 if pattern detected, 0 otherwise.
class CDLMorningStar : public Indicator<CDLMorningStar> {
public:
    CDLMorningStar(const Line<double>& open, const Line<double>& high,
                   const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double o0 = open_.data()[idx - 2];
        const double c0 = close_.data()[idx - 2];
        const double h0 = high_.data()[idx - 2];
        const double l0 = low_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1];
        const double c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx];
        const double c2 = close_.data()[idx];
        const double range0 = h0 - l0;
        const double body0 = std::fabs(c0 - o0);
        const double body1 = std::fabs(c1 - o1);
        const double body2 = std::fabs(c2 - o2);
        const double midpoint0 = (o0 + c0) / 2.0;
        if (range0 <= 0.0) { line_.forward(0.0); return; }
        if (c0 < o0 && body0 >= 0.5 * range0 &&
            body1 < body0 * 0.5 &&
            c2 > o2 && body2 >= body1 &&
            c2 >= midpoint0) {
            line_.forward(100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 3; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

/// Evening Star: 3-bar bearish reversal pattern. Mirror of Morning Star.
/// Output: -100 if pattern detected, 0 otherwise.
class CDLEveningStar : public Indicator<CDLEveningStar> {
public:
    CDLEveningStar(const Line<double>& open, const Line<double>& high,
                   const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double o0 = open_.data()[idx - 2];
        const double c0 = close_.data()[idx - 2];
        const double h0 = high_.data()[idx - 2];
        const double l0 = low_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1];
        const double c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx];
        const double c2 = close_.data()[idx];
        const double range0 = h0 - l0;
        const double body0 = std::fabs(c0 - o0);
        const double body1 = std::fabs(c1 - o1);
        const double body2 = std::fabs(c2 - o2);
        const double midpoint0 = (o0 + c0) / 2.0;
        if (range0 <= 0.0) { line_.forward(0.0); return; }
        if (c0 > o0 && body0 >= 0.5 * range0 &&
            body1 < body0 * 0.5 &&
            c2 < o2 && body2 >= body1 &&
            c2 <= midpoint0) {
            line_.forward(-100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 3; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

/// Harami: current body contained within previous body, opposite colors.
/// Output: +100 bullish harami, -100 bearish harami, 0 no pattern.
class CDLHarami : public Indicator<CDLHarami> {
public:
    CDLHarami(const Line<double>& open, const Line<double>& high,
              const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) { line_.forward(0.0); return; }
        const double o1 = open_.data()[idx - 1];
        const double c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx];
        const double c2 = close_.data()[idx];
        const double body1_top = std::max(o1, c1);
        const double body1_bot = std::min(o1, c1);
        const double body2_top = std::max(o2, c2);
        const double body2_bot = std::min(o2, c2);
        const bool prev_bearish = c1 < o1;
        const bool prev_bullish = c1 > o1;
        const bool curr_bullish = c2 > o2;
        const bool curr_bearish = c2 < o2;
        if (prev_bearish && curr_bullish && body2_top <= body1_top && body2_bot >= body1_bot) {
            line_.forward(100.0);
        } else if (prev_bullish && curr_bearish && body2_top <= body1_top && body2_bot >= body1_bot) {
            line_.forward(-100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

/// Shooting Star: small body at bottom, long upper shadow, minimal lower shadow. Bearish reversal.
/// Output: -100 if pattern detected, 0 otherwise.
class CDLShootingStar : public Indicator<CDLShootingStar> {
public:
    CDLShootingStar(const Line<double>& open, const Line<double>& high,
                    const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double o = open_.data()[idx];
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];
        const double range = h - l;
        if (range <= 0.0) { line_.forward(0.0); return; }
        const double body = std::fabs(c - o);
        const double body_top = std::max(o, c);
        const double body_bot = std::min(o, c);
        const double upper_shadow = h - body_top;
        const double lower_shadow = body_bot - l;
        if (body < range / 3.0 && upper_shadow >= 2.0 * body && lower_shadow <= body * 0.1) {
            line_.forward(-100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

/// Hanging Man: same shape as hammer (small body at top, long lower shadow) but bearish signal.
/// Output: -100 if pattern detected, 0 otherwise.
class CDLHangingMan : public Indicator<CDLHangingMan> {
public:
    CDLHangingMan(const Line<double>& open, const Line<double>& high,
                  const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double o = open_.data()[idx];
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];
        const double range = h - l;
        if (range <= 0.0) { line_.forward(0.0); return; }
        const double body = std::fabs(c - o);
        const double body_top = std::max(o, c);
        const double body_bot = std::min(o, c);
        const double upper_shadow = h - body_top;
        const double lower_shadow = body_bot - l;
        if (body < range / 3.0 && lower_shadow >= 2.0 * body && upper_shadow <= body * 0.1) {
            line_.forward(-100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

/// Marubozu: full body candle with negligible shadows.
/// Output: +100 bullish (close > open), -100 bearish (close < open), 0 no pattern.
class CDLMarubozu : public Indicator<CDLMarubozu> {
public:
    CDLMarubozu(const Line<double>& open, const Line<double>& high,
                const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double o = open_.data()[idx];
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];
        const double range = h - l;
        if (range <= 0.0) { line_.forward(0.0); return; }
        const double body = std::fabs(c - o);
        if (body >= 0.95 * range) {
            line_.forward(c > o ? 100.0 : -100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

/// Spinning Top: small body with both shadows present.
/// Output: +100 if close >= open, -100 if close < open, 0 no pattern.
class CDLSpinningTop : public Indicator<CDLSpinningTop> {
public:
    CDLSpinningTop(const Line<double>& open, const Line<double>& high,
                   const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double o = open_.data()[idx];
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];
        const double range = h - l;
        if (range <= 0.0) { line_.forward(0.0); return; }
        const double body = std::fabs(c - o);
        const double body_top = std::max(o, c);
        const double body_bot = std::min(o, c);
        const double upper_shadow = h - body_top;
        const double lower_shadow = body_bot - l;
        if (body < 0.3 * range && upper_shadow > 0.2 * range && lower_shadow > 0.2 * range) {
            line_.forward(c >= o ? 100.0 : -100.0);
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

} // namespace stratforge
