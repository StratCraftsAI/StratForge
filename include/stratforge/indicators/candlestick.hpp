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

/// Three White Soldiers: three consecutive bullish candles with higher closes.
/// Output: +100 if pattern detected, 0 otherwise.
class CDLThreeWhiteSoldiers : public Indicator<CDLThreeWhiteSoldiers> {
public:
    CDLThreeWhiteSoldiers(const Line<double>& open, const Line<double>& high,
                          const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double o0 = open_.data()[idx - 2], c0 = close_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        if (c0 > o0 && c1 > o1 && c2 > o2 &&
            c1 > c0 && c2 > c1 &&
            o1 >= o0 && o1 <= c0 &&
            o2 >= o1 && o2 <= c1) {
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

/// Three Black Crows: three consecutive bearish candles with lower closes.
/// Output: -100 if pattern detected, 0 otherwise.
class CDLThreeBlackCrows : public Indicator<CDLThreeBlackCrows> {
public:
    CDLThreeBlackCrows(const Line<double>& open, const Line<double>& high,
                       const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double o0 = open_.data()[idx - 2], c0 = close_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        if (c0 < o0 && c1 < o1 && c2 < o2 &&
            c1 < c0 && c2 < c1 &&
            o1 <= o0 && o1 >= c0 &&
            o2 <= o1 && o2 >= c1) {
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

/// Piercing Line: bearish candle followed by bullish candle closing above midpoint.
/// Output: +100 if pattern detected, 0 otherwise.
class CDLPiercingLine : public Indicator<CDLPiercingLine> {
public:
    CDLPiercingLine(const Line<double>& open, const Line<double>& high,
                    const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) { line_.forward(0.0); return; }
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        const double midpoint = (o1 + c1) / 2.0;
        if (c1 < o1 && c2 > o2 && o2 < c1 && c2 > midpoint && c2 < o1) {
            line_.forward(100.0);
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

/// Dark Cloud Cover: bullish candle followed by bearish candle closing below midpoint.
/// Output: -100 if pattern detected, 0 otherwise.
class CDLDarkCloudCover : public Indicator<CDLDarkCloudCover> {
public:
    CDLDarkCloudCover(const Line<double>& open, const Line<double>& high,
                      const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) { line_.forward(0.0); return; }
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        const double midpoint = (o1 + c1) / 2.0;
        if (c1 > o1 && c2 < o2 && o2 > c1 && c2 < midpoint && c2 > o1) {
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

/// Abandoned Baby: star pattern with gaps on both sides (doji star).
/// Output: +100 bullish, -100 bearish, 0 no pattern.
class CDLAbandonedBaby : public Indicator<CDLAbandonedBaby> {
public:
    CDLAbandonedBaby(const Line<double>& open, const Line<double>& high,
                     const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double o0 = open_.data()[idx - 2], c0 = close_.data()[idx - 2];
        const double h0 = high_.data()[idx - 2], l0 = low_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double h1 = high_.data()[idx - 1], l1 = low_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        const double h2 = high_.data()[idx],     l2 = low_.data()[idx];
        const double body1 = std::fabs(c1 - o1);
        const double range1 = h1 - l1;
        const bool star_doji = range1 > 0.0 && body1 <= 0.1 * range1;
        // Bullish: bearish bar 0, gap-down doji, gap-up bullish bar 2
        if (c0 < o0 && star_doji && h1 < l0 && l2 > h1 && c2 > o2) {
            line_.forward(100.0);
        }
        // Bearish: bullish bar 0, gap-up doji, gap-down bearish bar 2
        else if (c0 > o0 && star_doji && l1 > h0 && h2 < l1 && c2 < o2) {
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

/// Three Inside: harami pattern followed by confirmation bar.
/// Output: +100 bullish, -100 bearish, 0 no pattern.
class CDLThreeInside : public Indicator<CDLThreeInside> {
public:
    CDLThreeInside(const Line<double>& open, const Line<double>& high,
                   const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double o0 = open_.data()[idx - 2], c0 = close_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double c2 = close_.data()[idx];
        const double body0_top = std::max(o0, c0);
        const double body0_bot = std::min(o0, c0);
        const double body1_top = std::max(o1, c1);
        const double body1_bot = std::min(o1, c1);
        // Bullish: bearish bar 0, bullish harami bar 1, bar 2 closes above bar 0 open
        if (c0 < o0 && c1 > o1 &&
            body1_top <= body0_top && body1_bot >= body0_bot &&
            c2 > body0_top) {
            line_.forward(100.0);
        }
        // Bearish: bullish bar 0, bearish harami bar 1, bar 2 closes below bar 0 open
        else if (c0 > o0 && c1 < o1 &&
                 body1_top <= body0_top && body1_bot >= body0_bot &&
                 c2 < body0_bot) {
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

/// Three Outside: engulfing pattern followed by confirmation bar.
/// Output: +100 bullish, -100 bearish, 0 no pattern.
class CDLThreeOutside : public Indicator<CDLThreeOutside> {
public:
    CDLThreeOutside(const Line<double>& open, const Line<double>& high,
                    const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double o0 = open_.data()[idx - 2], c0 = close_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double c2 = close_.data()[idx];
        const double body0_top = std::max(o0, c0);
        const double body0_bot = std::min(o0, c0);
        const double body1_top = std::max(o1, c1);
        const double body1_bot = std::min(o1, c1);
        // Bullish: bearish bar 0, bullish engulfing bar 1, bar 2 closes higher
        if (c0 < o0 && c1 > o1 &&
            body1_top >= body0_top && body1_bot <= body0_bot &&
            c2 > c1) {
            line_.forward(100.0);
        }
        // Bearish: bullish bar 0, bearish engulfing bar 1, bar 2 closes lower
        else if (c0 > o0 && c1 < o1 &&
                 body1_top >= body0_top && body1_bot <= body0_bot &&
                 c2 < c1) {
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

/// Kicking: marubozu gap reversal. Opposite-color marubozus with a gap between.
/// Output: +100 bullish, -100 bearish, 0 no pattern.
class CDLKicking : public Indicator<CDLKicking> {
public:
    CDLKicking(const Line<double>& open, const Line<double>& high,
               const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) { line_.forward(0.0); return; }
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double h1 = high_.data()[idx - 1], l1 = low_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        const double h2 = high_.data()[idx],     l2 = low_.data()[idx];
        const double range1 = h1 - l1;
        const double range2 = h2 - l2;
        if (range1 <= 0.0 || range2 <= 0.0) { line_.forward(0.0); return; }
        const double body1 = std::fabs(c1 - o1);
        const double body2 = std::fabs(c2 - o2);
        const bool maru1 = body1 >= 0.9 * range1;
        const bool maru2 = body2 >= 0.9 * range2;
        // Bullish kicking: bearish marubozu then gap-up bullish marubozu
        if (maru1 && maru2 && c1 < o1 && c2 > o2 && l2 > h1) {
            line_.forward(100.0);
        }
        // Bearish kicking: bullish marubozu then gap-down bearish marubozu
        else if (maru1 && maru2 && c1 > o1 && c2 < o2 && h2 < l1) {
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

/// Belt Hold: opens at extreme (high or low) and closes strongly in opposite direction.
/// Output: +100 bullish, -100 bearish, 0 no pattern.
class CDLBeltHold : public Indicator<CDLBeltHold> {
public:
    CDLBeltHold(const Line<double>& open, const Line<double>& high,
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
        // Bullish belt hold: opens at low, strong bullish body
        if (o == l && c > o && body >= 0.6 * range) {
            line_.forward(100.0);
        }
        // Bearish belt hold: opens at high, strong bearish body
        else if (o == h && c < o && body >= 0.6 * range) {
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

/// Counter Attack: opposite-color candles that close at approximately the same level.
/// Output: +100 bullish, -100 bearish, 0 no pattern.
class CDLCounterAttack : public Indicator<CDLCounterAttack> {
public:
    CDLCounterAttack(const Line<double>& open, const Line<double>& high,
                     const Line<double>& low, const Line<double>& close,
                     double tolerance = 0.005)
        : open_(open), high_(high), low_(low), close_(close),
          tolerance_(tolerance) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) { line_.forward(0.0); return; }
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double h1 = high_.data()[idx - 1], l1 = low_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        const double range1 = h1 - l1;
        if (range1 <= 0.0) { line_.forward(0.0); return; }
        const double body1 = std::fabs(c1 - o1);
        const double body2 = std::fabs(c2 - o2);
        const double close_diff = std::fabs(c2 - c1);
        const bool closes_match = close_diff <= tolerance_ * range1;
        // Bullish: bearish bar 1, bullish bar 2, closes match
        if (c1 < o1 && c2 > o2 && body1 >= 0.3 * range1 && body2 > 0.0 && closes_match) {
            line_.forward(100.0);
        }
        // Bearish: bullish bar 1, bearish bar 2, closes match
        else if (c1 > o1 && c2 < o2 && body1 >= 0.3 * range1 && body2 > 0.0 && closes_match) {
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
    double tolerance_;
};

/// Homing Pigeon: two bullish candles where the second is contained within the first.
/// Output: +100 if pattern detected, 0 otherwise.
class CDLHomingPigeon : public Indicator<CDLHomingPigeon> {
public:
    CDLHomingPigeon(const Line<double>& open, const Line<double>& high,
                    const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) { line_.forward(0.0); return; }
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        // Both bullish, second body inside first body
        if (c1 > o1 && c2 > o2 &&
            o2 >= o1 && c2 <= c1) {
            line_.forward(100.0);
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

/// Matching Low: two bearish candles with same (or very close) close.
/// Output: +100 if pattern detected (bullish reversal), 0 otherwise.
class CDLMatchingLow : public Indicator<CDLMatchingLow> {
public:
    CDLMatchingLow(const Line<double>& open, const Line<double>& high,
                   const Line<double>& low, const Line<double>& close,
                   double tolerance = 0.001)
        : open_(open), high_(high), low_(low), close_(close),
          tolerance_(tolerance) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1) { line_.forward(0.0); return; }
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        if (c1 < o1 && c2 < o2) {
            const double price_scale = std::fabs(c1) > 0.0 ? std::fabs(c1) : 1.0;
            if (std::fabs(c2 - c1) <= tolerance_ * price_scale) {
                line_.forward(100.0);
                return;
            }
        }
        line_.forward(0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    double tolerance_;
};

/// Tasuki Gap: gap followed by partial fill by third bar.
/// Output: +100 bullish (upside gap), -100 bearish (downside gap), 0 no pattern.
class CDLTasukiGap : public Indicator<CDLTasukiGap> {
public:
    CDLTasukiGap(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double o0 = open_.data()[idx - 2], c0 = close_.data()[idx - 2];
        const double h0 = high_.data()[idx - 2], l0 = low_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double h1 = high_.data()[idx - 1], l1 = low_.data()[idx - 1];
        const double o2 = open_.data()[idx],     c2 = close_.data()[idx];
        // Upside Tasuki Gap: two bullish bars with gap up, third bar bearish partially fills gap
        if (c0 > o0 && c1 > o1 && l1 > h0 &&
            c2 < o2 &&
            o2 >= o1 && o2 <= c1 &&
            c2 < l1 && c2 > h0) {
            line_.forward(100.0);
        }
        // Downside Tasuki Gap: two bearish bars with gap down, third bar bullish partially fills gap
        else if (c0 < o0 && c1 < o1 && h1 < l0 &&
                 c2 > o2 &&
                 o2 <= o1 && o2 >= c1 &&
                 c2 > h1 && c2 < l0) {
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

/// Upside Gap Two Crows (simplified as Upside Gap): two bullish candles with upside gap,
/// third candle doesn't fill the gap.
/// Output: +100 if pattern detected, 0 otherwise.
class CDLUpsideGap : public Indicator<CDLUpsideGap> {
public:
    CDLUpsideGap(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 2) { line_.forward(0.0); return; }
        const double c0 = close_.data()[idx - 2];
        const double h0 = high_.data()[idx - 2];
        const double o1 = open_.data()[idx - 1], c1 = close_.data()[idx - 1];
        const double l1 = low_.data()[idx - 1];
        const double o2 = open_.data()[idx], c2 = close_.data()[idx];
        const double l2 = low_.data()[idx];
        // Two bullish bars with gap up, third bullish bar doesn't fill gap
        if (c0 > open_.data()[idx - 2] && c1 > o1 && l1 > h0 && // gap up
            c2 > o2 && l2 > h0) { // third bar bullish and gap still open
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

} // namespace stratforge
