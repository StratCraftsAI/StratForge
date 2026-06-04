#pragma once

// WorldQuant Alpha#001–#101 (Kakushadze 2015)
// Batch A: 20 time-series factors (pure per-symbol, no rank/IndNeutralize)
// Batch B: 63 cross-sectional factors (rank→ts_rank single-symbol approx)
// Batch C: 18 cross-sectional with IndNeutralize (passthrough in single-symbol mode)
//
// Architecture: one class per alpha factor, each composing shared operators
// from alpha101_ops.hpp. Internal scratch lines are built incrementally
// in next_impl() to avoid O(N^2) recomputation.
//
// Cross-sectional approximations (single-symbol mode):
//   rank(expr)            → ts_rank(scratch, idx, min(size, 252))
//   IndNeutralize(expr,g) → passthrough (Batch C)
//   scale(expr)           → expr / running_abs_sum
//   cap                   → close * volume (dollar volume proxy)

#include <stratforge/indicators/alpha101_ops.hpp>
#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace stratforge {

namespace a101 = alpha101;

// ============================================================================
// Alpha#006: -1 * correlation(open, volume, 10)
// Inputs: O, V
// ============================================================================
class Alpha101_006 : public Indicator<Alpha101_006> {
public:
    Alpha101_006(const Line<double>& open, const Line<double>& volume)
        : open_(open), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(open_.size()); }
        const auto idx = open_.index();
        if (idx + 1 < 10uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward(-1.0 * a101::correlation(open_, volume_, idx, 10));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& open_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#007: (adv20 < volume) ? (-1*ts_rank(abs(delta(close,7)),60)*sign(delta(close,7))) : -1
// Inputs: C, V   Warmup: 60+7 = 67
// ============================================================================
class Alpha101_007 : public Indicator<Alpha101_007> {
public:
    Alpha101_007(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            abs_delta_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double d7 = a101::delta(close_, idx, 7);
        abs_delta_.forward(std::isnan(d7) ? a101::NaN : std::abs(d7));

        if (idx + 1 < 67uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double adv20 = a101::adv(volume_, idx, 20);
        const double vol = volume_.data()[idx];
        if (adv20 < vol) {
            const double tsr = a101::ts_rank(abs_delta_, abs_delta_.size() - 1, 60);
            line_.forward(-1.0 * tsr * a101::sign(d7));
        } else {
            line_.forward(-1.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 67; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> abs_delta_;
};

// ============================================================================
// Alpha#009: conditional on ts_min/ts_max of delta(close,1) over 5 bars
// Inputs: C   Warmup: 6
// ============================================================================
class Alpha101_009 : public Indicator<Alpha101_009> {
public:
    explicit Alpha101_009(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            delta1_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double d1 = a101::delta(close_, idx, 1);
        delta1_.forward(std::isnan(d1) ? a101::NaN : d1);

        if (idx + 1 < 6uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t didx = delta1_.size() - 1;
        const double mn = a101::ts_min(delta1_, didx, 5);
        const double mx = a101::ts_max(delta1_, didx, 5);

        if (0.0 < mn) {
            line_.forward(d1);
        } else if (mx < 0.0) {
            line_.forward(d1);
        } else {
            line_.forward(-1.0 * d1);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& close_;
    Line<double> delta1_;
};

// ============================================================================
// Alpha#012: sign(delta(volume,1)) * (-1 * delta(close,1))
// Inputs: C, V   Warmup: 2
// ============================================================================
class Alpha101_012 : public Indicator<Alpha101_012> {
public:
    Alpha101_012(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward(a101::sign(a101::delta(volume_, idx, 1)) *
                      (-1.0 * a101::delta(close_, idx, 1)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#021: conditional SMA/stddev/volume thresholds
// ((sma(c,8)+stddev(c,8)) < sma(c,2)) ? -1 :
//   (sma(c,2) < (sma(c,8)-stddev(c,8))) ? 1 :
//     (volume/adv20 >= 1) ? 1 : -1
// Inputs: C, V   Warmup: 20
// ============================================================================
class Alpha101_021 : public Indicator<Alpha101_021> {
public:
    Alpha101_021(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 20uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double sma8 = a101::sma(close_, idx, 8);
        const double std8 = a101::stddev(close_, idx, 8);
        const double sma2 = a101::sma(close_, idx, 2);
        const double adv20 = a101::adv(volume_, idx, 20);
        const double vol = volume_.data()[idx];

        if ((sma8 + std8) < sma2) {
            line_.forward(-1.0);
        } else if (sma2 < (sma8 - std8)) {
            line_.forward(1.0);
        } else if (adv20 > 0.0 && (vol / adv20) >= 1.0) {
            line_.forward(1.0);
        } else {
            line_.forward(-1.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#023: (SMA(high,20) < high) ? -1*delta(high,2) : 0
// Inputs: H   Warmup: 20
// ============================================================================
class Alpha101_023 : public Indicator<Alpha101_023> {
public:
    explicit Alpha101_023(const Line<double>& high) : high_(high) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        if (idx + 1 < 20uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double sma20 = a101::sma(high_, idx, 20);
        const double h = high_.data()[idx];
        if (sma20 < h) {
            line_.forward(-1.0 * a101::delta(high_, idx, 2));
        } else {
            line_.forward(0.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& high_;
};

// ============================================================================
// Alpha#024: conditional on delta(SMA(close,100),100)/delay(close,100)
// ((delta(sma(c,100),100)/delay(c,100)) <= 0.05) ?
//   -1*(close - ts_min(close,100)) :
//   -1*delta(close,3)
// Inputs: C   Warmup: 200
// ============================================================================
class Alpha101_024 : public Indicator<Alpha101_024> {
public:
    explicit Alpha101_024(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            sma100_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double s = a101::sma(close_, idx, 100);
        sma100_.forward(std::isnan(s) ? a101::NaN : s);

        if (idx + 1 < 200uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t sidx = sma100_.size() - 1;
        const double d_sma = a101::delta(sma100_, sidx, 100);
        const double delayed = a101::delay(close_, idx, 100);
        const double ratio = (delayed != 0.0) ? (d_sma / delayed) : 0.0;

        if (ratio <= 0.05) {
            line_.forward(-1.0 * (close_.data()[idx] - a101::ts_min(close_, idx, 100)));
        } else {
            line_.forward(-1.0 * a101::delta(close_, idx, 3));
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 200; }

private:
    const Line<double>& close_;
    Line<double> sma100_;
};

// ============================================================================
// Alpha#026: -1 * ts_max(correlation(ts_rank(volume,5), ts_rank(high,5), 5), 3)
// Inputs: H, V   Warmup: 12 (5 for ts_rank + 5 for corr + 3 for ts_max - overlaps)
// ============================================================================
class Alpha101_026 : public Indicator<Alpha101_026> {
public:
    Alpha101_026(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            tsr_vol_.data().reserve(high_.size());
            tsr_high_.data().reserve(high_.size());
            corr_.data().reserve(high_.size());
        }
        const auto idx = high_.index();

        const double rv = a101::ts_rank(volume_, idx, 5);
        const double rh = a101::ts_rank(high_, idx, 5);
        tsr_vol_.forward(rv);
        tsr_high_.forward(rh);

        const std::size_t sidx = tsr_vol_.size() - 1;
        const double c = (sidx + 1 >= 5)
            ? a101::correlation(tsr_vol_, tsr_high_, sidx, 5)
            : a101::NaN;
        corr_.forward(c);

        const std::size_t cidx = corr_.size() - 1;
        if (cidx + 1 < 3uz || std::isnan(c)) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        line_.forward(-1.0 * a101::ts_max(corr_, cidx, 3));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 12; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> tsr_vol_;
    Line<double> tsr_high_;
    Line<double> corr_;
};

// ============================================================================
// Alpha#028: scale(correlation(adv20, low, 5) + (high+low)/2 - close)
// Inputs: H, L, C, V   Warmup: 24 (20 for adv20 + 5 for corr - overlap)
// ============================================================================
class Alpha101_028 : public Indicator<Alpha101_028> {
public:
    Alpha101_028(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            adv20_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double av = a101::adv(volume_, idx, 20);
        adv20_.forward(std::isnan(av) ? a101::NaN : av);

        if (idx + 1 < 24uz) [[unlikely]] {
            abs_sum_ += 0.0;
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t aidx = adv20_.size() - 1;
        const double corr = a101::correlation(adv20_, low_, aidx < idx ? aidx : idx, 5);
        const double raw = corr + (high_.data()[idx] + low_.data()[idx]) / 2.0 - close_.data()[idx];
        abs_sum_ += std::abs(raw);
        line_.forward(a101::scale(raw, abs_sum_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 24; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> adv20_;
    double abs_sum_ = 0.0;
};

// ============================================================================
// Alpha#032: scale(sma(c,7)-c) + 20*scale(correlation(vwap, delay(c,5), 230))
// Inputs: H, L, C, V   Warmup: 235
// ============================================================================
class Alpha101_032 : public Indicator<Alpha101_032> {
public:
    Alpha101_032(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
            delayed_c_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        vwap_.forward(a101::vwap(high_, low_, close_, idx));
        const double dc = a101::delay(close_, idx, 5);
        delayed_c_.forward(std::isnan(dc) ? a101::NaN : dc);

        if (idx + 1 < 235uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double term1_raw = a101::sma(close_, idx, 7) - close_.data()[idx];
        abs_sum1_ += std::abs(term1_raw);
        const double term1 = a101::scale(term1_raw, abs_sum1_);

        const std::size_t vidx = vwap_.size() - 1;
        const std::size_t didx = delayed_c_.size() - 1;
        const std::size_t corr_len = std::min(vidx, didx) + 1;
        double corr_val = a101::NaN;
        if (corr_len >= 230) {
            corr_val = a101::correlation(vwap_, delayed_c_, std::min(vidx, didx), 230);
        }
        if (std::isnan(corr_val)) {
            line_.forward(a101::NaN);
            return;
        }
        abs_sum2_ += std::abs(corr_val);
        const double term2 = 20.0 * a101::scale(corr_val, abs_sum2_);

        line_.forward(term1 + term2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 235; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> delayed_c_;
    double abs_sum1_ = 0.0;
    double abs_sum2_ = 0.0;
};

// ============================================================================
// Alpha#035: ts_rank(volume,32) * (1-ts_rank(close+high-low,16)) * (1-ts_rank(returns,32))
// Inputs: H, L, C, V   Warmup: 33 (32 for ts_rank + 1 for returns)
// ============================================================================
class Alpha101_035 : public Indicator<Alpha101_035> {
public:
    Alpha101_035(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            chl_.data().reserve(close_.size());
            ret_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        chl_.forward(close_.data()[idx] + high_.data()[idx] - low_.data()[idx]);
        const double r = a101::returns(close_, idx);
        ret_.forward(std::isnan(r) ? a101::NaN : r);

        if (idx + 1 < 33uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double tr_vol = a101::ts_rank(volume_, idx, 32);
        const double tr_chl = a101::ts_rank(chl_, chl_.size() - 1, 16);
        const double tr_ret = a101::ts_rank(ret_, ret_.size() - 1, 32);

        line_.forward(tr_vol * (1.0 - tr_chl) * (1.0 - tr_ret));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 33; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> chl_;
    Line<double> ret_;
};

// ============================================================================
// Alpha#041: sqrt(high*low) - vwap
// Inputs: H, L, C   Warmup: 1
// ============================================================================
class Alpha101_041 : public Indicator<Alpha101_041> {
public:
    Alpha101_041(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double v = a101::vwap(high_, low_, close_, idx);
        line_.forward(std::sqrt(h * l) - v);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

// ============================================================================
// Alpha#043: ts_rank(volume/adv20, 20) * ts_rank(-1*delta(close,7), 8)
// Inputs: C, V   Warmup: 27 (20 for adv20 + 7 more for scratch lines)
// ============================================================================
class Alpha101_043 : public Indicator<Alpha101_043> {
public:
    Alpha101_043(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vol_ratio_.data().reserve(close_.size());
            neg_delta_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double av = a101::adv(volume_, idx, 20);
        const double vr = (std::isnan(av) || av == 0.0) ? a101::NaN
                          : volume_.data()[idx] / av;
        vol_ratio_.forward(vr);

        const double d7 = a101::delta(close_, idx, 7);
        neg_delta_.forward(std::isnan(d7) ? a101::NaN : -1.0 * d7);

        if (idx + 1 < 27uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double tr1 = a101::ts_rank(vol_ratio_, vol_ratio_.size() - 1, 20);
        const double tr2 = a101::ts_rank(neg_delta_, neg_delta_.size() - 1, 8);
        line_.forward(tr1 * tr2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 27; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vol_ratio_;
    Line<double> neg_delta_;
};

// ============================================================================
// Alpha#046: conditional on close momentum slopes
// slope = (delay(c,20)-delay(c,10))/10 - (delay(c,10)-c)/10
// if slope > 0.25 → -1; if slope < 0 → 1; else -1*(c-delay(c,1))
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha101_046 : public Indicator<Alpha101_046> {
public:
    explicit Alpha101_046(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 21uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double d20 = a101::delay(close_, idx, 20);
        const double d10 = a101::delay(close_, idx, 10);
        const double c = close_.data()[idx];
        const double slope = ((d20 - d10) / 10.0) - ((d10 - c) / 10.0);

        if (0.25 < slope) {
            line_.forward(-1.0);
        } else if (slope < 0.0) {
            line_.forward(1.0);
        } else {
            line_.forward(-1.0 * (c - a101::delay(close_, idx, 1)));
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#049: conditional on close momentum slope < -0.1
// slope = (delay(c,20)-delay(c,10))/10 - (delay(c,10)-c)/10
// if slope < -0.1 → 1; else -1*(c-delay(c,1))
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha101_049 : public Indicator<Alpha101_049> {
public:
    explicit Alpha101_049(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 21uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double d20 = a101::delay(close_, idx, 20);
        const double d10 = a101::delay(close_, idx, 10);
        const double c = close_.data()[idx];
        const double slope = ((d20 - d10) / 10.0) - ((d10 - c) / 10.0);

        if (slope < -0.1) {
            line_.forward(1.0);
        } else {
            line_.forward(-1.0 * (c - a101::delay(close_, idx, 1)));
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#051: conditional on close momentum slope < -0.05
// Same structure as 049 with threshold -0.05
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha101_051 : public Indicator<Alpha101_051> {
public:
    explicit Alpha101_051(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 21uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double d20 = a101::delay(close_, idx, 20);
        const double d10 = a101::delay(close_, idx, 10);
        const double c = close_.data()[idx];
        const double slope = ((d20 - d10) / 10.0) - ((d10 - c) / 10.0);

        if (slope < -0.05) {
            line_.forward(1.0);
        } else {
            line_.forward(-1.0 * (c - a101::delay(close_, idx, 1)));
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#053: -1 * delta(((close-low)-(high-close))/(close-low), 9)
// Inputs: H, L, C   Warmup: 10
// ============================================================================
class Alpha101_053 : public Indicator<Alpha101_053> {
public:
    Alpha101_053(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inner_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double c = close_.data()[idx];
        const double l = low_.data()[idx];
        const double h = high_.data()[idx];
        const double cl = c - l;
        const double val = (cl == 0.0) ? 0.0 : ((cl - (h - c)) / cl);
        inner_.forward(val);

        if (idx + 1 < 10uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        line_.forward(-1.0 * a101::delta(inner_, inner_.size() - 1, 9));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> inner_;
};

// ============================================================================
// Alpha#054: (-1 * (low-close) * open^5) / ((low-high) * close^5)
// Inputs: O, H, L, C   Warmup: 1
// ============================================================================
class Alpha101_054 : public Indicator<Alpha101_054> {
public:
    Alpha101_054(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double o = open_.data()[idx];
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];

        const double denom = (l - h) * std::pow(c, 5.0);
        if (denom == 0.0) {
            line_.forward(0.0);
            return;
        }
        line_.forward((-1.0 * (l - c) * std::pow(o, 5.0)) / denom);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

// ============================================================================
// Alpha#084: SignedPower(ts_rank(vwap - ts_max(vwap, 15), 20), delta(close, 5))
// Inputs: H, L, C   Warmup: 34 (15 for ts_max + 20 for ts_rank - overlap, +5 for delta)
// ============================================================================
class Alpha101_084 : public Indicator<Alpha101_084> {
public:
    Alpha101_084(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
            diff_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double v = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(v);

        const std::size_t vidx = vwap_.size() - 1;
        const double mx = (vidx + 1 >= 15) ? a101::ts_max(vwap_, vidx, 15) : a101::NaN;
        diff_.forward(std::isnan(mx) ? a101::NaN : (v - mx));

        if (idx + 1 < 34uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t didx = diff_.size() - 1;
        const double tsr = a101::ts_rank(diff_, didx, 20);
        const double dc = a101::delta(close_, idx, 5);
        line_.forward(a101::signed_power(tsr, dc));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 34; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> vwap_;
    Line<double> diff_;
};

// ============================================================================
// Alpha#101: (close - open) / ((high - low) + 0.001)
// Inputs: O, H, L, C   Warmup: 1
// ============================================================================
class Alpha101_101 : public Indicator<Alpha101_101> {
public:
    Alpha101_101(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        line_.forward((close_.data()[idx] - open_.data()[idx]) /
                      ((high_.data()[idx] - low_.data()[idx]) + 0.001));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

// ============================================================================
// Alpha#001: (rank(Ts_ArgMax(SignedPower(((returns < 0) ? stddev(returns, 20) : close), 2.), 5)) - 0.5)
// Inputs: C   Warmup: 25
// ============================================================================
class Alpha101_001 : public Indicator<Alpha101_001> {
public:
    explicit Alpha101_001(const Line<double>& close)
        : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ret_.data().reserve(close_.size());
            sp_.data().reserve(close_.size());
            argmax_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // Build returns scratch
        const double r = a101::returns(close_, idx);
        ret_.forward(std::isnan(r) ? a101::NaN : r);

        // SignedPower(cond, 2) needs stddev(returns,20) which needs 20 returns
        if (idx + 1 < 21uz) [[unlikely]] {
            sp_.forward(a101::NaN);
            argmax_.forward(a101::NaN);
            line_.forward(a101::NaN);
            return;
        }

        double base;
        if (r < 0.0) {
            base = a101::stddev(ret_, ret_.size() - 1, 20);
        } else {
            base = close_.data()[idx];
        }
        sp_.forward(a101::signed_power(base, 2.0));

        // Ts_ArgMax over 5 bars of signed_power
        if (idx + 1 < 25uz) [[unlikely]] {
            argmax_.forward(a101::NaN);
            line_.forward(a101::NaN);
            return;
        }

        const double am = a101::ts_argmax(sp_, sp_.size() - 1, 5);
        argmax_.forward(am);

        const double rk = a101::ts_rank(argmax_, argmax_.size() - 1,
                                        std::min(argmax_.size(), 252uz));
        line_.forward(rk - 0.5);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 25; }

private:
    const Line<double>& close_;
    Line<double> ret_;
    Line<double> sp_;
    Line<double> argmax_;
};

// ============================================================================
// Alpha#002: (-1 * correlation(rank(delta(log(volume), 2)), rank(((close - open) / open)), 6))
// Inputs: O, C, V   Warmup: 9
// ============================================================================
class Alpha101_002 : public Indicator<Alpha101_002> {
public:
    Alpha101_002(const Line<double>& open, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            dlv_raw_.data().reserve(close_.size());
            co_raw_.data().reserve(close_.size());
            rk_dlv_.data().reserve(close_.size());
            rk_co_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // delta(log(volume), 2)
        double dlv = a101::NaN;
        if (idx >= 2uz) {
            dlv = std::log(volume_.data()[idx]) - std::log(volume_.data()[idx - 2]);
        }
        dlv_raw_.forward(dlv);

        // (close - open) / open
        const double o = open_.data()[idx];
        const double co = (o != 0.0) ? (close_.data()[idx] - o) / o : 0.0;
        co_raw_.forward(co);

        // rank(delta(log(volume),2)) via ts_rank on raw values
        const double r_dlv = a101::ts_rank(dlv_raw_, dlv_raw_.size() - 1,
                                           std::min(dlv_raw_.size(), 252uz));
        rk_dlv_.forward(r_dlv);

        // rank((close-open)/open) via ts_rank on raw values
        const double r_co = a101::ts_rank(co_raw_, co_raw_.size() - 1,
                                          std::min(co_raw_.size(), 252uz));
        rk_co_.forward(r_co);

        if (idx + 1 < 9uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double corr = a101::correlation(rk_dlv_, rk_co_,
                                              rk_dlv_.size() - 1, 6);
        line_.forward(-1.0 * corr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 9; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> dlv_raw_;
    Line<double> co_raw_;
    Line<double> rk_dlv_;
    Line<double> rk_co_;
};

// ============================================================================
// Alpha#003: (-1 * correlation(rank(open), rank(volume), 10))
// Inputs: O, V   Warmup: 10
// ============================================================================
class Alpha101_003 : public Indicator<Alpha101_003> {
public:
    Alpha101_003(const Line<double>& open, const Line<double>& volume)
        : open_(open), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(open_.size());
            rk_open_.data().reserve(open_.size());
            rk_vol_.data().reserve(open_.size());
        }
        const auto idx = open_.index();

        const double ro = a101::ts_rank(open_, idx, std::min(idx + 1, 252uz));
        rk_open_.forward(ro);

        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rk_vol_.forward(rv);

        if (idx + 1 < 10uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double corr = a101::correlation(rk_open_, rk_vol_,
                                              rk_open_.size() - 1, 10);
        line_.forward(-1.0 * corr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& open_;
    const Line<double>& volume_;
    Line<double> rk_open_;
    Line<double> rk_vol_;
};

// ============================================================================
// Alpha#004: (-1 * Ts_Rank(rank(low), 9))
// Inputs: L   Warmup: 9
// ============================================================================
class Alpha101_004 : public Indicator<Alpha101_004> {
public:
    explicit Alpha101_004(const Line<double>& low)
        : low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(low_.size());
            rk_low_.data().reserve(low_.size());
        }
        const auto idx = low_.index();

        const double rl = a101::ts_rank(low_, idx, std::min(idx + 1, 252uz));
        rk_low_.forward(rl);

        if (idx + 1 < 9uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double tsr = a101::ts_rank(rk_low_, rk_low_.size() - 1, 9);
        line_.forward(-1.0 * tsr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 9; }

private:
    const Line<double>& low_;
    Line<double> rk_low_;
};

// ============================================================================
// Alpha#005: (rank((open - (sum(vwap, 10) / 10))) * (-1 * abs(rank((close - vwap)))))
// Inputs: O, H, L, C   Warmup: 10
// ============================================================================
class Alpha101_005 : public Indicator<Alpha101_005> {
public:
    Alpha101_005(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
            diff_ov_.data().reserve(close_.size());
            diff_cv_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double v = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(v);

        // close - vwap (available every bar)
        diff_cv_.forward(close_.data()[idx] - v);

        if (idx + 1 < 10uz) [[unlikely]] {
            diff_ov_.forward(a101::NaN);
            line_.forward(a101::NaN);
            return;
        }

        // sum(vwap, 10) / 10 = sma(vwap, 10)
        const double avg_vwap = a101::sma(vwap_, vwap_.size() - 1, 10);
        const double ov = open_.data()[idx] - avg_vwap;
        diff_ov_.forward(ov);

        // rank(open - avg_vwap)
        const double rk1 = a101::ts_rank(diff_ov_, diff_ov_.size() - 1,
                                         std::min(diff_ov_.size(), 252uz));

        // rank(close - vwap)
        const double rk2 = a101::ts_rank(diff_cv_, diff_cv_.size() - 1,
                                         std::min(diff_cv_.size(), 252uz));

        line_.forward(rk1 * (-1.0 * std::abs(rk2)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> vwap_;
    Line<double> diff_ov_;
    Line<double> diff_cv_;
};

// ============================================================================
// Alpha#008: -1 * rank(((sum(open, 5) * sum(returns, 5)) -
//            delay((sum(open, 5) * sum(returns, 5)), 10)))
// Inputs: O, C   Warmup: 16
// ============================================================================
class Alpha101_008 : public Indicator<Alpha101_008> {
public:
    Alpha101_008(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ret_.data().reserve(close_.size());
            prod_.data().reserve(close_.size());
            expr_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double r = a101::returns(close_, idx);
        ret_.forward(std::isnan(r) ? a101::NaN : r);

        // sum(open,5) * sum(returns,5) needs 5 returns, returns needs idx>=1
        if (idx + 1 < 6uz) [[unlikely]] {
            prod_.forward(a101::NaN);
            expr_.forward(a101::NaN);
            line_.forward(a101::NaN);
            return;
        }

        const double s_open = a101::sum(open_, idx, 5);
        const double s_ret = a101::sum(ret_, ret_.size() - 1, 5);
        const double p = s_open * s_ret;
        prod_.forward(p);

        // delay(prod, 10) needs 10 valid prod values → idx+1 >= 16
        if (idx + 1 < 16uz) [[unlikely]] {
            expr_.forward(a101::NaN);
            line_.forward(a101::NaN);
            return;
        }

        const double delayed = a101::delay(prod_, prod_.size() - 1, 10);
        const double e = p - delayed;
        expr_.forward(e);

        const double rk = a101::ts_rank(expr_, expr_.size() - 1,
                                        std::min(expr_.size(), 252uz));
        line_.forward(-1.0 * rk);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 16; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> ret_;
    Line<double> prod_;
    Line<double> expr_;
};

// ============================================================================
// Alpha#010: rank(((0 < ts_min(delta(close, 1), 4)) ? delta(close, 1) :
//            ((ts_max(delta(close, 1), 4) < 0) ? delta(close, 1) :
//            (-1 * delta(close, 1)))))
// Inputs: C   Warmup: 6
// ============================================================================
class Alpha101_010 : public Indicator<Alpha101_010> {
public:
    explicit Alpha101_010(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            delta1_.data().reserve(close_.size());
            cond_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double d1 = a101::delta(close_, idx, 1);
        delta1_.forward(std::isnan(d1) ? a101::NaN : d1);

        if (idx + 1 < 5uz) [[unlikely]] {
            cond_.forward(a101::NaN);
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t didx = delta1_.size() - 1;
        const double mn = a101::ts_min(delta1_, didx, 4);
        const double mx = a101::ts_max(delta1_, didx, 4);

        double val;
        if (0.0 < mn) {
            val = d1;
        } else if (mx < 0.0) {
            val = d1;
        } else {
            val = -1.0 * d1;
        }
        cond_.forward(val);

        if (idx + 1 < 6uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double rk = a101::ts_rank(cond_, cond_.size() - 1,
                                        std::min(cond_.size(), 252uz));
        line_.forward(rk);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& close_;
    Line<double> delta1_;
    Line<double> cond_;
};

// ============================================================================
// Alpha#011: ((rank(ts_max((vwap - close), 3)) + rank(ts_min((vwap - close), 3))) *
//             rank(delta(volume, 3)))
// Inputs: H, L, C, V   Warmup: 4
// ============================================================================
class Alpha101_011 : public Indicator<Alpha101_011> {
public:
    Alpha101_011(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vc_.data().reserve(close_.size());
            ts_mx_.data().reserve(close_.size());
            ts_mn_.data().reserve(close_.size());
            dvol_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // vwap - close
        const double v = a101::vwap(high_, low_, close_, idx);
        vc_.forward(v - close_.data()[idx]);

        // delta(volume, 3)
        const double dv = a101::delta(volume_, idx, 3);
        dvol_.forward(std::isnan(dv) ? a101::NaN : dv);

        if (idx + 1 < 4uz) [[unlikely]] {
            ts_mx_.forward(a101::NaN);
            ts_mn_.forward(a101::NaN);
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t vidx = vc_.size() - 1;
        ts_mx_.forward(a101::ts_max(vc_, vidx, 3));
        ts_mn_.forward(a101::ts_min(vc_, vidx, 3));

        // rank(ts_max(vc,3))
        const double rk_mx = a101::ts_rank(ts_mx_, ts_mx_.size() - 1,
                                           std::min(ts_mx_.size(), 252uz));
        // rank(ts_min(vc,3))
        const double rk_mn = a101::ts_rank(ts_mn_, ts_mn_.size() - 1,
                                           std::min(ts_mn_.size(), 252uz));
        // rank(delta(volume,3))
        const double rk_dv = a101::ts_rank(dvol_, dvol_.size() - 1,
                                           std::min(dvol_.size(), 252uz));

        line_.forward((rk_mx + rk_mn) * rk_dv);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 4; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vc_;
    Line<double> ts_mx_;
    Line<double> ts_mn_;
    Line<double> dvol_;
};

// ============================================================================
// Alpha#013: -1 * rank(covariance(rank(close), rank(volume), 5))
// Inputs: C, V   Warmup: 257 (252 for rank + 5 for cov, +252 for outer rank)
// ============================================================================
class Alpha101_013 : public Indicator<Alpha101_013> {
public:
    Alpha101_013(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            rank_close_.data().reserve(close_.size());
            rank_volume_.data().reserve(close_.size());
            cov_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double rc = a101::ts_rank(close_, idx, std::min(idx + 1, 252uz));
        rank_close_.forward((idx + 1 < 2uz) ? a101::NaN : rc);

        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rank_volume_.forward((idx + 1 < 2uz) ? a101::NaN : rv);

        const std::size_t sidx = rank_close_.size() - 1;
        const double cv = (sidx + 1 >= 5uz)
            ? a101::covariance(rank_close_, rank_volume_, sidx, 5)
            : a101::NaN;
        cov_.forward(cv);

        if (idx + 1 < 257uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t cidx = cov_.size() - 1;
        const double outer_rank = a101::ts_rank(cov_, cidx, std::min(cidx + 1, 252uz));
        line_.forward(-1.0 * outer_rank);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> rank_close_;
    Line<double> rank_volume_;
    Line<double> cov_;
};

// ============================================================================
// Alpha#014: (-1 * rank(delta(returns, 3))) * correlation(open, volume, 10)
// Inputs: O, C, V   Warmup: 256 (1 for returns + 3 for delta + 252 for rank)
// ============================================================================
class Alpha101_014 : public Indicator<Alpha101_014> {
public:
    Alpha101_014(const Line<double>& open, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            returns_.data().reserve(close_.size());
            delta_ret_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double r = a101::returns(close_, idx);
        returns_.forward(std::isnan(r) ? a101::NaN : r);

        const std::size_t ridx = returns_.size() - 1;
        const double dr = (ridx >= 3uz) ? a101::delta(returns_, ridx, 3) : a101::NaN;
        delta_ret_.forward(dr);

        if (idx + 1 < 256uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t didx = delta_ret_.size() - 1;
        const double rank_dr = a101::ts_rank(delta_ret_, didx, std::min(didx + 1, 252uz));
        const double corr = a101::correlation(open_, volume_, idx, 10);
        line_.forward(-1.0 * rank_dr * corr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 256; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> returns_;
    Line<double> delta_ret_;
};

// ============================================================================
// Alpha#015: -1 * sum(rank(correlation(rank(high), rank(volume), 3)), 3)
// Inputs: H, V   Warmup: 259 (252 for inner rank + 3 for corr + 252 for outer rank + 3 for sum, overlapping)
// ============================================================================
class Alpha101_015 : public Indicator<Alpha101_015> {
public:
    Alpha101_015(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            rank_high_.data().reserve(high_.size());
            rank_volume_.data().reserve(high_.size());
            corr_.data().reserve(high_.size());
            rank_corr_.data().reserve(high_.size());
        }
        const auto idx = high_.index();

        const double rh = a101::ts_rank(high_, idx, std::min(idx + 1, 252uz));
        rank_high_.forward((idx + 1 < 2uz) ? a101::NaN : rh);

        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rank_volume_.forward((idx + 1 < 2uz) ? a101::NaN : rv);

        const std::size_t sidx = rank_high_.size() - 1;
        const double c = (sidx + 1 >= 3uz)
            ? a101::correlation(rank_high_, rank_volume_, sidx, 3)
            : a101::NaN;
        corr_.forward(c);

        const std::size_t cidx = corr_.size() - 1;
        const double rc = std::isnan(c) ? a101::NaN
            : a101::ts_rank(corr_, cidx, std::min(cidx + 1, 252uz));
        rank_corr_.forward(rc);

        if (idx + 1 < 259uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t rcidx = rank_corr_.size() - 1;
        line_.forward(-1.0 * a101::sum(rank_corr_, rcidx, 3));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 259; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> rank_high_;
    Line<double> rank_volume_;
    Line<double> corr_;
    Line<double> rank_corr_;
};

// ============================================================================
// Alpha#016: -1 * rank(covariance(rank(high), rank(volume), 5))
// Inputs: H, V   Warmup: 257 (252 for rank + 5 for cov + 252 for outer rank)
// ============================================================================
class Alpha101_016 : public Indicator<Alpha101_016> {
public:
    Alpha101_016(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            rank_high_.data().reserve(high_.size());
            rank_volume_.data().reserve(high_.size());
            cov_.data().reserve(high_.size());
        }
        const auto idx = high_.index();

        const double rh = a101::ts_rank(high_, idx, std::min(idx + 1, 252uz));
        rank_high_.forward((idx + 1 < 2uz) ? a101::NaN : rh);

        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rank_volume_.forward((idx + 1 < 2uz) ? a101::NaN : rv);

        const std::size_t sidx = rank_high_.size() - 1;
        const double cv = (sidx + 1 >= 5uz)
            ? a101::covariance(rank_high_, rank_volume_, sidx, 5)
            : a101::NaN;
        cov_.forward(cv);

        if (idx + 1 < 257uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t cidx = cov_.size() - 1;
        const double outer_rank = a101::ts_rank(cov_, cidx, std::min(cidx + 1, 252uz));
        line_.forward(-1.0 * outer_rank);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> rank_high_;
    Line<double> rank_volume_;
    Line<double> cov_;
};

// ============================================================================
// Alpha#017: (((-1 * rank(ts_rank(close, 10))) * rank(delta(delta(close, 1), 1)))
//            * rank(ts_rank((volume / adv20), 5)))
// Inputs: C, V   Warmup: 275 (20 for adv20 + 5 for ts_rank + 252 for rank)
// ============================================================================
class Alpha101_017 : public Indicator<Alpha101_017> {
public:
    Alpha101_017(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            tsr_close_.data().reserve(close_.size());
            delta_delta_.data().reserve(close_.size());
            vol_ratio_.data().reserve(close_.size());
            tsr_vol_ratio_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // ts_rank(close, 10)
        const double trc = (idx + 1 >= 10uz) ? a101::ts_rank(close_, idx, 10) : a101::NaN;
        tsr_close_.forward(trc);

        // delta(delta(close, 1), 1) = close[t] - 2*close[t-1] + close[t-2]
        double dd = a101::NaN;
        if (idx >= 2uz) {
            const double d1_now = a101::delta(close_, idx, 1);
            const double d1_prev = a101::delta(close_, idx - 1, 1);
            if (!std::isnan(d1_now) && !std::isnan(d1_prev))
                dd = d1_now - d1_prev;
        }
        delta_delta_.forward(dd);

        // volume / adv20
        const double av = a101::adv(volume_, idx, 20);
        const double vr = (std::isnan(av) || av == 0.0) ? a101::NaN
                          : volume_.data()[idx] / av;
        vol_ratio_.forward(vr);

        // ts_rank(vol_ratio, 5)
        const std::size_t vridx = vol_ratio_.size() - 1;
        const double trv = (vridx + 1 >= 5uz) ? a101::ts_rank(vol_ratio_, vridx, 5) : a101::NaN;
        tsr_vol_ratio_.forward(trv);

        if (idx + 1 < 275uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t tcidx = tsr_close_.size() - 1;
        const std::size_t ddidx = delta_delta_.size() - 1;
        const std::size_t tvidx = tsr_vol_ratio_.size() - 1;

        const double rank1 = a101::ts_rank(tsr_close_, tcidx, std::min(tcidx + 1, 252uz));
        const double rank2 = a101::ts_rank(delta_delta_, ddidx, std::min(ddidx + 1, 252uz));
        const double rank3 = a101::ts_rank(tsr_vol_ratio_, tvidx, std::min(tvidx + 1, 252uz));

        line_.forward(-1.0 * rank1 * rank2 * rank3);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 275; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> tsr_close_;
    Line<double> delta_delta_;
    Line<double> vol_ratio_;
    Line<double> tsr_vol_ratio_;
};

// ============================================================================
// Alpha#018: -1 * rank(((stddev(abs(close - open), 5) + (close - open))
//            + correlation(close, open, 10)))
// Inputs: O, C   Warmup: 262 (10 for corr/stddev + 252 for rank)
// ============================================================================
class Alpha101_018 : public Indicator<Alpha101_018> {
public:
    Alpha101_018(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            abs_co_.data().reserve(close_.size());
            expr_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double co = close_.data()[idx] - open_.data()[idx];
        abs_co_.forward(std::abs(co));

        const std::size_t aidx = abs_co_.size() - 1;
        double val = a101::NaN;
        if (idx + 1 >= 10uz) {
            const double sd = a101::stddev(abs_co_, aidx, 5);
            const double corr = a101::correlation(close_, open_, idx, 10);
            if (!std::isnan(sd) && !std::isnan(corr))
                val = sd + co + corr;
        }
        expr_.forward(val);

        if (idx + 1 < 262uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t eidx = expr_.size() - 1;
        const double rank_val = a101::ts_rank(expr_, eidx, std::min(eidx + 1, 252uz));
        line_.forward(-1.0 * rank_val);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> abs_co_;
    Line<double> expr_;
};

// ============================================================================
// Alpha#019: (-1 * sign((close - delay(close, 7)) + delta(close, 7)))
//            * (1 + rank(1 + sum(returns, 250)))
// Inputs: C   Warmup: 503 (1 for returns + 250 for sum + 252 for rank)
// ============================================================================
class Alpha101_019 : public Indicator<Alpha101_019> {
public:
    explicit Alpha101_019(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            returns_.data().reserve(close_.size());
            sum_ret_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double r = a101::returns(close_, idx);
        returns_.forward(std::isnan(r) ? a101::NaN : r);

        const std::size_t ridx = returns_.size() - 1;
        const double sr = (ridx + 1 >= 250uz) ? a101::sum(returns_, ridx, 250) : a101::NaN;
        sum_ret_.forward(std::isnan(sr) ? a101::NaN : 1.0 + sr);

        if (idx + 1 < 503uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double c = close_.data()[idx];
        const double delayed = a101::delay(close_, idx, 7);
        const double d7 = a101::delta(close_, idx, 7);
        const double sign_val = a101::sign((c - delayed) + d7);

        const std::size_t sridx = sum_ret_.size() - 1;
        const double rank_val = a101::ts_rank(sum_ret_, sridx, std::min(sridx + 1, 252uz));

        line_.forward(-1.0 * sign_val * (1.0 + rank_val));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 503; }

private:
    const Line<double>& close_;
    Line<double> returns_;
    Line<double> sum_ret_;
};

// ============================================================================
// Alpha#020: ((-1 * rank(open - delay(high, 1))) * rank(open - delay(close, 1)))
//            * rank(open - delay(low, 1))
// Inputs: O, H, L, C   Warmup: 253 (1 for delay + 252 for rank)
// ============================================================================
class Alpha101_020 : public Indicator<Alpha101_020> {
public:
    Alpha101_020(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            oh_.data().reserve(close_.size());
            oc_.data().reserve(close_.size());
            ol_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        if (idx >= 1uz) {
            const double o = open_.data()[idx];
            oh_.forward(o - a101::delay(high_, idx, 1));
            oc_.forward(o - a101::delay(close_, idx, 1));
            ol_.forward(o - a101::delay(low_, idx, 1));
        } else {
            oh_.forward(a101::NaN);
            oc_.forward(a101::NaN);
            ol_.forward(a101::NaN);
        }

        if (idx + 1 < 253uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t sidx = oh_.size() - 1;
        const double rank1 = a101::ts_rank(oh_, sidx, std::min(sidx + 1, 252uz));
        const double rank2 = a101::ts_rank(oc_, sidx, std::min(sidx + 1, 252uz));
        const double rank3 = a101::ts_rank(ol_, sidx, std::min(sidx + 1, 252uz));

        line_.forward(-1.0 * rank1 * rank2 * rank3);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> oh_;
    Line<double> oc_;
    Line<double> ol_;
};

// ============================================================================
// Alpha#022: -1 * (delta(correlation(high, volume, 5), 5) * rank(stddev(close, 20)))
// Inputs: H, C, V   Warmup: 20
// ============================================================================
class Alpha101_022 : public Indicator<Alpha101_022> {
public:
    Alpha101_022(const Line<double>& high, const Line<double>& close,
                 const Line<double>& volume)
        : high_(high), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            corr_.data().reserve(close_.size());
            stddev_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // Build correlation(high, volume, 5) scratch
        const double c = a101::correlation(high_, volume_, idx, 5);
        corr_.forward(c);

        // Build stddev(close, 20) scratch
        const double sd = a101::stddev(close_, idx, 20);
        stddev_.forward(sd);

        if (idx + 1 < 20uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // delta(correlation, 5) needs 6 corr values minimum (corr available from idx=4)
        const std::size_t cidx = corr_.size() - 1;
        const double d_corr = a101::delta(corr_, cidx, 5);
        if (std::isnan(d_corr) || std::isnan(sd)) {
            line_.forward(a101::NaN);
            return;
        }

        // rank(stddev(close, 20)) = ts_rank on stddev scratch
        const std::size_t sidx = stddev_.size() - 1;
        const double rank_sd = a101::ts_rank(stddev_, sidx, std::min(sidx + 1, 252uz));

        line_.forward(-1.0 * d_corr * rank_sd);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& high_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> corr_;
    Line<double> stddev_;
};

// ============================================================================
// Alpha#025: rank(((((-1 * returns) * adv20) * vwap) * (high - close)))
// Inputs: H, L, C, V   Warmup: 20
// ============================================================================
class Alpha101_025 : public Indicator<Alpha101_025> {
public:
    Alpha101_025(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            expr_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double ret = a101::returns(close_, idx);
        const double adv20 = a101::adv(volume_, idx, 20);
        const double vw = a101::vwap(high_, low_, close_, idx);
        const double hc = high_.data()[idx] - close_.data()[idx];

        if (std::isnan(ret) || std::isnan(adv20)) {
            expr_.forward(a101::NaN);
        } else {
            expr_.forward((-1.0 * ret) * adv20 * vw * hc);
        }

        if (idx + 1 < 20uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t eidx = expr_.size() - 1;
        const double rank_val = a101::ts_rank(expr_, eidx, std::min(eidx + 1, 252uz));
        line_.forward(rank_val);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> expr_;
};

// ============================================================================
// Alpha#027: ((0.5 < rank((sum(correlation(rank(volume), rank(vwap), 6), 2) / 2.0))) ? -1 : 1)
// Inputs: H, L, C, V   Warmup: 8
// ============================================================================
class Alpha101_027 : public Indicator<Alpha101_027> {
public:
    Alpha101_027(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            rank_vol_.data().reserve(close_.size());
            rank_vwap_.data().reserve(close_.size());
            corr_.data().reserve(close_.size());
            expr_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // rank(volume) = ts_rank with expanding window
        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rank_vol_.forward(rv);

        // rank(vwap) - build vwap scratch, then ts_rank
        // For efficiency, compute vwap inline and rank it
        vwap_scratch_.forward(a101::vwap(high_, low_, close_, idx));
        const std::size_t vsidx = vwap_scratch_.size() - 1;
        const double rvw = a101::ts_rank(vwap_scratch_, vsidx, std::min(vsidx + 1, 252uz));
        rank_vwap_.forward(rvw);

        // correlation(rank(volume), rank(vwap), 6)
        const std::size_t ridx = rank_vol_.size() - 1;
        const double cr = (ridx + 1 >= 6uz)
            ? a101::correlation(rank_vol_, rank_vwap_, ridx, 6)
            : a101::NaN;
        corr_.forward(cr);

        // sum(correlation, 2) / 2.0
        const std::size_t cridx = corr_.size() - 1;
        if (cridx + 1 >= 2uz && !std::isnan(a101::sum(corr_, cridx, 2))) {
            expr_.forward(a101::sum(corr_, cridx, 2) / 2.0);
        } else {
            expr_.forward(a101::NaN);
        }

        if (idx + 1 < 8uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t eidx = expr_.size() - 1;
        if (std::isnan(expr_.data()[eidx])) {
            line_.forward(a101::NaN);
            return;
        }

        const double rank_expr = a101::ts_rank(expr_, eidx, std::min(eidx + 1, 252uz));
        line_.forward((0.5 < rank_expr) ? -1.0 : 1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 8; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_scratch_;
    Line<double> rank_vol_;
    Line<double> rank_vwap_;
    Line<double> corr_;
    Line<double> expr_;
};

// ============================================================================
// Alpha#029: min(product(rank(rank(scale(log(sum(ts_min(rank(rank(
//   (-1*rank(delta((close-1),5))))),2),1))))),1),5)
//   + ts_rank(delay((-1*returns),6),5)
// Simplified: ts_min(rank(rank(scale(log(ts_min(rank(rank(
//   -1*rank(delta(close,5)))),2))))),5) + ts_rank(delay(-1*returns,6),5)
// Inputs: C   Warmup: 12
// ============================================================================
class Alpha101_029 : public Indicator<Alpha101_029> {
public:
    explicit Alpha101_029(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            delta_s_.data().reserve(n);
            rank_d_.data().reserve(n);
            neg_rank_d_.data().reserve(n);
            rank_nr_.data().reserve(n);
            rank_rnr_.data().reserve(n);
            tsmin2_s_.data().reserve(n);
            scale_s_.data().reserve(n);
            rank_sc_.data().reserve(n);
            rank_rsc_.data().reserve(n);
            neg_ret_.data().reserve(n);
            delayed_nr_.data().reserve(n);
        }
        const auto idx = close_.index();

        // --- Term 1 pipeline (inside-out) ---

        // delta(close, 5)  [delta((close-1),5) = delta(close,5)]
        const double d5 = a101::delta(close_, idx, 5);
        delta_s_.forward(std::isnan(d5) ? a101::NaN : d5);

        // rank(delta) = ts_rank on delta_s_
        const std::size_t didx = delta_s_.size() - 1;
        const double rd = std::isnan(delta_s_.data()[didx])
            ? a101::NaN
            : a101::ts_rank(delta_s_, didx, std::min(didx + 1, 252uz));
        rank_d_.forward(rd);

        // -1 * rank(delta)
        neg_rank_d_.forward(std::isnan(rd) ? a101::NaN : -1.0 * rd);

        // rank(-1 * rank(delta))
        const std::size_t nridx = neg_rank_d_.size() - 1;
        const double rnr = std::isnan(neg_rank_d_.data()[nridx])
            ? a101::NaN
            : a101::ts_rank(neg_rank_d_, nridx, std::min(nridx + 1, 252uz));
        rank_nr_.forward(rnr);

        // rank(rank(-1 * rank(delta)))
        const std::size_t rnridx = rank_nr_.size() - 1;
        const double rrnr = std::isnan(rank_nr_.data()[rnridx])
            ? a101::NaN
            : a101::ts_rank(rank_nr_, rnridx, std::min(rnridx + 1, 252uz));
        rank_rnr_.forward(rrnr);

        // ts_min(rank_rnr, 2)
        const std::size_t rrnridx = rank_rnr_.size() - 1;
        const double tm2 = a101::ts_min(rank_rnr_, rrnridx, 2);
        tsmin2_s_.forward(tm2);

        // sum(ts_min, 1) = ts_min value; log(sum) = log(ts_min)
        // scale(log(x)) = log(x) / running_abs_sum
        if (!std::isnan(tm2) && tm2 > 0.0) {
            const double lg = std::log(tm2);
            abs_sum_ += std::abs(lg);
            scale_s_.forward(a101::scale(lg, abs_sum_));
        } else {
            scale_s_.forward(a101::NaN);
        }

        // rank(scale)
        const std::size_t scidx = scale_s_.size() - 1;
        const double rsc = std::isnan(scale_s_.data()[scidx])
            ? a101::NaN
            : a101::ts_rank(scale_s_, scidx, std::min(scidx + 1, 252uz));
        rank_sc_.forward(rsc);

        // rank(rank(scale))  [product(x,1) = x]
        const std::size_t rscidx = rank_sc_.size() - 1;
        const double rrsc = std::isnan(rank_sc_.data()[rscidx])
            ? a101::NaN
            : a101::ts_rank(rank_sc_, rscidx, std::min(rscidx + 1, 252uz));
        rank_rsc_.forward(rrsc);

        // --- Term 2 pipeline ---

        // -1 * returns
        const double ret = a101::returns(close_, idx);
        neg_ret_.forward(std::isnan(ret) ? a101::NaN : -1.0 * ret);

        // delay(-1*returns, 6)
        const std::size_t nretidx = neg_ret_.size() - 1;
        const double dnr = a101::delay(neg_ret_, nretidx, 6);
        delayed_nr_.forward(dnr);

        // --- Combine ---
        if (idx + 1 < 12uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // ts_min(rank_rsc, 5)
        const std::size_t rrscidx = rank_rsc_.size() - 1;
        const double term1 = a101::ts_min(rank_rsc_, rrscidx, 5);

        // ts_rank(delayed_nr, 5)
        const std::size_t dnridx = delayed_nr_.size() - 1;
        const double term2 = a101::ts_rank(delayed_nr_, dnridx, 5);

        if (std::isnan(term1) || std::isnan(term2)) {
            line_.forward(a101::NaN);
            return;
        }

        line_.forward(term1 + term2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 12; }

private:
    const Line<double>& close_;
    Line<double> delta_s_;
    Line<double> rank_d_;
    Line<double> neg_rank_d_;
    Line<double> rank_nr_;
    Line<double> rank_rnr_;
    Line<double> tsmin2_s_;
    Line<double> scale_s_;
    Line<double> rank_sc_;
    Line<double> rank_rsc_;
    Line<double> neg_ret_;
    Line<double> delayed_nr_;
    double abs_sum_ = 0.0;
};

// ============================================================================
// Alpha#030: (((1.0 - rank(sign_sum)) * sum(volume, 5)) / sum(volume, 20))
//   sign_sum = sign(close-delay(close,1)) + sign(delay(close,1)-delay(close,2))
//              + sign(delay(close,2)-delay(close,3))
// Inputs: C, V   Warmup: 20
// ============================================================================
class Alpha101_030 : public Indicator<Alpha101_030> {
public:
    Alpha101_030(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            sign_sum_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        if (idx >= 3uz) {
            const double c0 = close_.data()[idx];
            const double c1 = a101::delay(close_, idx, 1);
            const double c2 = a101::delay(close_, idx, 2);
            const double c3 = a101::delay(close_, idx, 3);
            sign_sum_.forward(a101::sign(c0 - c1) + a101::sign(c1 - c2) + a101::sign(c2 - c3));
        } else {
            sign_sum_.forward(a101::NaN);
        }

        if (idx + 1 < 20uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t sidx = sign_sum_.size() - 1;
        const double rank_ss = a101::ts_rank(sign_sum_, sidx, std::min(sidx + 1, 252uz));
        const double sum5 = a101::sum(volume_, idx, 5);
        const double sum20 = a101::sum(volume_, idx, 20);

        if (sum20 == 0.0) {
            line_.forward(0.0);
            return;
        }

        line_.forward(((1.0 - rank_ss) * sum5) / sum20);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> sign_sum_;
};

// ============================================================================
// Alpha#031: rank(rank(rank(decay_linear((-1*rank(rank(delta(close,10)))),10))))
//   + rank(-1*delta(close,3))
//   + sign(scale(correlation(adv20, low, 12)))
// Inputs: L, C, V   Warmup: 31
// ============================================================================
class Alpha101_031 : public Indicator<Alpha101_031> {
public:
    Alpha101_031(const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            delta10_.data().reserve(n);
            rank_d10_.data().reserve(n);
            rank_rd10_.data().reserve(n);
            neg_rrd10_.data().reserve(n);
            decay_.data().reserve(n);
            rank_decay1_.data().reserve(n);
            rank_decay2_.data().reserve(n);
            rank_decay3_.data().reserve(n);
            neg_d3_.data().reserve(n);
            adv20_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();

        // --- Term 1 pipeline: rank(rank(rank(decay_linear(-1*rank(rank(delta(close,10))),10)))) ---

        // delta(close, 10)
        const double d10 = a101::delta(close_, idx, 10);
        delta10_.forward(std::isnan(d10) ? a101::NaN : d10);

        // rank(delta(close, 10))
        const std::size_t d10idx = delta10_.size() - 1;
        const double rd10 = std::isnan(delta10_.data()[d10idx])
            ? a101::NaN
            : a101::ts_rank(delta10_, d10idx, std::min(d10idx + 1, 252uz));
        rank_d10_.forward(rd10);

        // rank(rank(delta(close, 10)))
        const std::size_t rd10idx = rank_d10_.size() - 1;
        const double rrd10 = std::isnan(rank_d10_.data()[rd10idx])
            ? a101::NaN
            : a101::ts_rank(rank_d10_, rd10idx, std::min(rd10idx + 1, 252uz));
        rank_rd10_.forward(rrd10);

        // -1 * rank(rank(delta(close, 10)))
        neg_rrd10_.forward(std::isnan(rrd10) ? a101::NaN : -1.0 * rrd10);

        // decay_linear(-1*rank(rank(delta(close,10))), 10)
        const std::size_t nrridx = neg_rrd10_.size() - 1;
        const double dl = a101::decay_linear(neg_rrd10_, nrridx, 10);
        decay_.forward(dl);

        // rank(rank(rank(decay_linear)))
        const std::size_t dlidx = decay_.size() - 1;
        const double r1 = std::isnan(decay_.data()[dlidx])
            ? a101::NaN
            : a101::ts_rank(decay_, dlidx, std::min(dlidx + 1, 252uz));
        rank_decay1_.forward(r1);

        const std::size_t r1idx = rank_decay1_.size() - 1;
        const double r2 = std::isnan(rank_decay1_.data()[r1idx])
            ? a101::NaN
            : a101::ts_rank(rank_decay1_, r1idx, std::min(r1idx + 1, 252uz));
        rank_decay2_.forward(r2);

        const std::size_t r2idx = rank_decay2_.size() - 1;
        const double r3 = std::isnan(rank_decay2_.data()[r2idx])
            ? a101::NaN
            : a101::ts_rank(rank_decay2_, r2idx, std::min(r2idx + 1, 252uz));
        rank_decay3_.forward(r3);

        // --- Term 2: rank(-1 * delta(close, 3)) ---
        const double d3 = a101::delta(close_, idx, 3);
        neg_d3_.forward(std::isnan(d3) ? a101::NaN : -1.0 * d3);

        // --- Term 3: sign(scale(correlation(adv20, low, 12))) ---
        const double av = a101::adv(volume_, idx, 20);
        adv20_.forward(std::isnan(av) ? a101::NaN : av);

        const std::size_t aidx = adv20_.size() - 1;
        const double cr = (aidx + 1 >= 12uz)
            ? a101::correlation(adv20_, low_, aidx < idx ? aidx : idx, 12)
            : a101::NaN;
        corr_.forward(cr);

        if (idx + 1 < 31uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // Term 1 value
        const double t1 = rank_decay3_.data()[rank_decay3_.size() - 1];

        // Term 2 value
        const std::size_t nd3idx = neg_d3_.size() - 1;
        const double t2 = a101::ts_rank(neg_d3_, nd3idx, std::min(nd3idx + 1, 252uz));

        // Term 3 value: sign(scale(correlation(adv20, low, 12)))
        const double corr_val = corr_.data()[corr_.size() - 1];
        double t3 = 0.0;
        if (!std::isnan(corr_val)) {
            corr_abs_sum_ += std::abs(corr_val);
            t3 = a101::sign(a101::scale(corr_val, corr_abs_sum_));
        }

        if (std::isnan(t1) || std::isnan(t2)) {
            line_.forward(a101::NaN);
            return;
        }

        line_.forward(t1 + t2 + t3);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 31; }

private:
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> delta10_;
    Line<double> rank_d10_;
    Line<double> rank_rd10_;
    Line<double> neg_rrd10_;
    Line<double> decay_;
    Line<double> rank_decay1_;
    Line<double> rank_decay2_;
    Line<double> rank_decay3_;
    Line<double> neg_d3_;
    Line<double> adv20_;
    Line<double> corr_;
    double corr_abs_sum_ = 0.0;
};

// ============================================================================
// Alpha#033: rank((-1 * ((1 - (open / close))^1)))  =  rank(open/close - 1)
// Inputs: O, C   Warmup: 1
// ============================================================================
class Alpha101_033 : public Indicator<Alpha101_033> {
public:
    Alpha101_033(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            expr_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double c = close_.data()[idx];
        const double o = open_.data()[idx];
        expr_.forward((c != 0.0) ? (o / c - 1.0) : 0.0);

        const std::size_t eidx = expr_.size() - 1;
        line_.forward(a101::ts_rank(expr_, eidx, std::min(eidx + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> expr_;
};

// ============================================================================
// Alpha#034: rank(((1 - rank(stddev(returns,2)/stddev(returns,5))) + (1 - rank(delta(close,1)))))
// Inputs: C   Warmup: 6
// ============================================================================
class Alpha101_034 : public Indicator<Alpha101_034> {
public:
    explicit Alpha101_034(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            std_ratio_.data().reserve(n);
            delta1_.data().reserve(n);
            expr_.data().reserve(n);
        }
        const auto idx = close_.index();

        const double r = a101::returns(close_, idx);
        ret_.forward(std::isnan(r) ? a101::NaN : r);

        const std::size_t ridx = ret_.size() - 1;
        const double sd2 = a101::stddev(ret_, ridx, 2);
        const double sd5 = a101::stddev(ret_, ridx, 5);
        if (!std::isnan(sd2) && !std::isnan(sd5) && sd5 != 0.0) {
            std_ratio_.forward(sd2 / sd5);
        } else {
            std_ratio_.forward(a101::NaN);
        }

        const double d1 = a101::delta(close_, idx, 1);
        delta1_.forward(std::isnan(d1) ? a101::NaN : d1);

        if (idx + 1 < 6uz) [[unlikely]] {
            expr_.forward(a101::NaN);
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t sridx = std_ratio_.size() - 1;
        const double rank_sr = a101::ts_rank(std_ratio_, sridx, std::min(sridx + 1, 252uz));

        const std::size_t d1idx = delta1_.size() - 1;
        const double rank_d1 = a101::ts_rank(delta1_, d1idx, std::min(d1idx + 1, 252uz));

        const double combined = (1.0 - rank_sr) + (1.0 - rank_d1);
        expr_.forward(combined);

        const std::size_t eidx = expr_.size() - 1;
        line_.forward(a101::ts_rank(expr_, eidx, std::min(eidx + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& close_;
    Line<double> ret_;
    Line<double> std_ratio_;
    Line<double> delta1_;
    Line<double> expr_;
};

// ============================================================================
// Alpha#036: 2.21*rank(corr(close-open, delay(vol,1), 15)) + 0.7*rank(open-close)
//   + 0.73*rank(Ts_Rank(delay(-ret,6),5)) + rank(abs(corr(vwap,adv20,6)))
//   + 0.6*rank(((sum(c,200)/200 - open)*(close-open)))
// Inputs: O,H,L,C,V   Warmup: 200
// ============================================================================
class Alpha101_036 : public Indicator<Alpha101_036> {
public:
    Alpha101_036(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            co_diff_.data().reserve(close_.size());
            delayed_vol_.data().reserve(close_.size());
            oc_diff_.data().reserve(close_.size());
            neg_ret_.data().reserve(close_.size());
            delayed_neg_ret_.data().reserve(close_.size());
            tsrank_dnr_.data().reserve(close_.size());
            vwap_line_.data().reserve(close_.size());
            adv20_line_.data().reserve(close_.size());
            corr1_scratch_.data().reserve(close_.size());
            corr4_scratch_.data().reserve(close_.size());
            term5_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // Build scratch lines incrementally
        co_diff_.forward(close_.data()[idx] - open_.data()[idx]);
        const double dv = a101::delay(volume_, idx, 1);
        delayed_vol_.forward(std::isnan(dv) ? a101::NaN : dv);

        oc_diff_.forward(open_.data()[idx] - close_.data()[idx]);

        const double ret = a101::returns(close_, idx);
        const double nr = std::isnan(ret) ? a101::NaN : -1.0 * ret;
        neg_ret_.forward(nr);
        const double dnr = a101::delay(neg_ret_, neg_ret_.size() - 1, 6);
        delayed_neg_ret_.forward(std::isnan(dnr) ? a101::NaN : dnr);
        const std::size_t dnr_idx = delayed_neg_ret_.size() - 1;
        const double tsr_dnr = (dnr_idx + 1 >= 5uz)
            ? a101::ts_rank(delayed_neg_ret_, dnr_idx, 5) : a101::NaN;
        tsrank_dnr_.forward(tsr_dnr);

        vwap_line_.forward(a101::vwap(high_, low_, close_, idx));
        const double av = a101::adv(volume_, idx, 20);
        adv20_line_.forward(std::isnan(av) ? a101::NaN : av);

        // Term1 scratch: correlation(close-open, delay(vol,1), 15)
        const std::size_t co_idx = co_diff_.size() - 1;
        const std::size_t dv_idx = delayed_vol_.size() - 1;
        const double corr1 = (co_idx + 1 >= 15uz && dv_idx + 1 >= 15uz)
            ? a101::correlation(co_diff_, delayed_vol_, co_idx, 15) : a101::NaN;
        corr1_scratch_.forward(std::isnan(corr1) ? a101::NaN : corr1);

        // Term4 scratch: abs(correlation(vwap, adv20, 6))
        const std::size_t vi = vwap_line_.size() - 1;
        const std::size_t ai = adv20_line_.size() - 1;
        const double corr4 = (vi + 1 >= 6uz && ai + 1 >= 6uz)
            ? a101::correlation(vwap_line_, adv20_line_, std::min(vi, ai), 6)
            : a101::NaN;
        corr4_scratch_.forward(std::isnan(corr4) ? a101::NaN : std::abs(corr4));

        // Term5 scratch: ((sum(close,200)/200) - open) * (close - open)
        const double s200 = a101::sum(close_, idx, 200);
        const double t5 = std::isnan(s200) ? a101::NaN
            : ((s200 / 200.0) - open_.data()[idx]) * (close_.data()[idx] - open_.data()[idx]);
        term5_scratch_.forward(t5);

        if (idx + 1 < 200uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // rank approximations via ts_rank on scratch lines
        const auto rank_scratch = [](const Line<double>& s) {
            const std::size_t n = s.size();
            return a101::ts_rank(s, n - 1, std::min(n, 252uz));
        };

        const double r1 = rank_scratch(corr1_scratch_);
        const double r2 = rank_scratch(oc_diff_);
        const double r3 = rank_scratch(tsrank_dnr_);
        const double r4 = rank_scratch(corr4_scratch_);
        const double r5 = rank_scratch(term5_scratch_);

        line_.forward(2.21 * r1 + 0.7 * r2 + 0.73 * r3 + r4 + 0.6 * r5);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 200; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> co_diff_;
    Line<double> delayed_vol_;
    Line<double> oc_diff_;
    Line<double> neg_ret_;
    Line<double> delayed_neg_ret_;
    Line<double> tsrank_dnr_;
    Line<double> vwap_line_;
    Line<double> adv20_line_;
    Line<double> corr1_scratch_;
    Line<double> corr4_scratch_;
    Line<double> term5_scratch_;
};

// ============================================================================
// Alpha#037: rank(correlation(delay(open-close,1), close, 200)) + rank(open-close)
// Inputs: O,C   Warmup: 202
// ============================================================================
class Alpha101_037 : public Indicator<Alpha101_037> {
public:
    Alpha101_037(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            oc_diff_.data().reserve(close_.size());
            delayed_oc_.data().reserve(close_.size());
            corr_scratch_.data().reserve(close_.size());
            oc_rank_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double oc = open_.data()[idx] - close_.data()[idx];
        oc_diff_.forward(oc);

        const std::size_t oc_idx = oc_diff_.size() - 1;
        const double doc = (oc_idx >= 1uz) ? oc_diff_.data()[oc_idx - 1] : a101::NaN;
        delayed_oc_.forward(doc);

        const std::size_t doc_idx = delayed_oc_.size() - 1;
        const double corr = (doc_idx + 1 >= 200uz)
            ? a101::correlation(delayed_oc_, close_, std::min(doc_idx, idx), 200)
            : a101::NaN;
        corr_scratch_.forward(std::isnan(corr) ? a101::NaN : corr);

        oc_rank_scratch_.forward(oc);

        if (idx + 1 < 202uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto rank_scratch = [](const Line<double>& s) {
            const std::size_t n = s.size();
            return a101::ts_rank(s, n - 1, std::min(n, 252uz));
        };

        const double r1 = rank_scratch(corr_scratch_);
        const double r2 = rank_scratch(oc_rank_scratch_);
        line_.forward(r1 + r2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 202; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> oc_diff_;
    Line<double> delayed_oc_;
    Line<double> corr_scratch_;
    Line<double> oc_rank_scratch_;
};

// ============================================================================
// Alpha#038: (-1 * rank(Ts_Rank(close, 10))) * rank(close / open)
// Inputs: O,C   Warmup: 10
// ============================================================================
class Alpha101_038 : public Indicator<Alpha101_038> {
public:
    Alpha101_038(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            tsr_close_.data().reserve(close_.size());
            co_ratio_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double tsr = (idx + 1 >= 10uz)
            ? a101::ts_rank(close_, idx, 10) : a101::NaN;
        tsr_close_.forward(tsr);

        const double o = open_.data()[idx];
        const double ratio = (o != 0.0) ? close_.data()[idx] / o : a101::NaN;
        co_ratio_.forward(ratio);

        if (idx + 1 < 10uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto rank_scratch = [](const Line<double>& s) {
            const std::size_t n = s.size();
            return a101::ts_rank(s, n - 1, std::min(n, 252uz));
        };

        const double r1 = rank_scratch(tsr_close_);
        const double r2 = rank_scratch(co_ratio_);
        line_.forward(-1.0 * r1 * r2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> tsr_close_;
    Line<double> co_ratio_;
};

// ============================================================================
// Alpha#039: (-1*rank(delta(close,7)*(1-rank(decay_linear(volume/adv20,9))))) * (1+rank(sum(returns,250)))
// Inputs: C,V   Warmup: 251
// ============================================================================
class Alpha101_039 : public Indicator<Alpha101_039> {
public:
    Alpha101_039(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vol_ratio_.data().reserve(close_.size());
            dl_scratch_.data().reserve(close_.size());
            ret_line_.data().reserve(close_.size());
            sum_ret_.data().reserve(close_.size());
            inner_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // volume / adv20
        const double av = a101::adv(volume_, idx, 20);
        const double vr = (std::isnan(av) || av == 0.0) ? a101::NaN
                          : volume_.data()[idx] / av;
        vol_ratio_.forward(vr);

        // decay_linear(volume/adv20, 9)
        const std::size_t vr_idx = vol_ratio_.size() - 1;
        const double dl = (vr_idx + 1 >= 9uz)
            ? a101::decay_linear(vol_ratio_, vr_idx, 9) : a101::NaN;
        dl_scratch_.forward(dl);

        // returns and sum(returns, 250)
        const double ret = a101::returns(close_, idx);
        ret_line_.forward(std::isnan(ret) ? a101::NaN : ret);
        const std::size_t ri = ret_line_.size() - 1;
        const double sr = (ri + 1 >= 250uz) ? a101::sum(ret_line_, ri, 250) : a101::NaN;
        sum_ret_.forward(std::isnan(sr) ? a101::NaN : sr);

        // inner = delta(close,7) * (1 - rank(decay_linear(vol/adv20, 9)))
        const double d7 = a101::delta(close_, idx, 7);
        double inner = a101::NaN;
        if (!std::isnan(d7) && !std::isnan(dl)) {
            const auto rank_scratch = [](const Line<double>& s) {
                const std::size_t n = s.size();
                return a101::ts_rank(s, n - 1, std::min(n, 252uz));
            };
            const double rank_dl = rank_scratch(dl_scratch_);
            inner = d7 * (1.0 - rank_dl);
        }
        inner_scratch_.forward(inner);

        if (idx + 1 < 251uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto rank_scratch = [](const Line<double>& s) {
            const std::size_t n = s.size();
            return a101::ts_rank(s, n - 1, std::min(n, 252uz));
        };

        const double r_inner = rank_scratch(inner_scratch_);
        const double r_sum_ret = rank_scratch(sum_ret_);
        line_.forward(-1.0 * r_inner * (1.0 + r_sum_ret));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 251; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vol_ratio_;
    Line<double> dl_scratch_;
    Line<double> ret_line_;
    Line<double> sum_ret_;
    Line<double> inner_scratch_;
};

// ============================================================================
// Alpha#040: (-1 * rank(stddev(high, 10))) * correlation(high, volume, 10)
// Inputs: H,V   Warmup: 10
// ============================================================================
class Alpha101_040 : public Indicator<Alpha101_040> {
public:
    Alpha101_040(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            std_scratch_.data().reserve(high_.size());
        }
        const auto idx = high_.index();

        const double sd = a101::stddev(high_, idx, 10);
        std_scratch_.forward(std::isnan(sd) ? a101::NaN : sd);

        if (idx + 1 < 10uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto rank_scratch = [](const Line<double>& s) {
            const std::size_t n = s.size();
            return a101::ts_rank(s, n - 1, std::min(n, 252uz));
        };

        const double r = rank_scratch(std_scratch_);
        const double corr = a101::correlation(high_, volume_, idx, 10);
        line_.forward(-1.0 * r * corr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> std_scratch_;
};

// ============================================================================
// Alpha#042: rank(vwap - close) / rank(vwap + close)
// Inputs: H,L,C   Warmup: 1
// ============================================================================
class Alpha101_042 : public Indicator<Alpha101_042> {
public:
    Alpha101_042(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vmc_.data().reserve(close_.size());
            vpc_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double v = a101::vwap(high_, low_, close_, idx);
        const double c = close_.data()[idx];
        vmc_.forward(v - c);
        vpc_.forward(v + c);

        const auto rank_scratch = [](const Line<double>& s) {
            const std::size_t n = s.size();
            return a101::ts_rank(s, n - 1, std::min(n, 252uz));
        };

        const double r1 = rank_scratch(vmc_);
        const double r2 = rank_scratch(vpc_);
        if (r2 == 0.0) {
            line_.forward(0.0);
        } else {
            line_.forward(r1 / r2);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> vmc_;
    Line<double> vpc_;
};

// ============================================================================
// Alpha#044: -1 * correlation(high, rank(volume), 5)
// Inputs: H,V   Warmup: 5
// ============================================================================
class Alpha101_044 : public Indicator<Alpha101_044> {
public:
    Alpha101_044(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            vol_rank_.data().reserve(high_.size());
        }
        const auto idx = high_.index();

        // rank(volume) = ts_rank of volume over available history (up to 252)
        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        vol_rank_.forward(rv);

        const std::size_t vr_idx = vol_rank_.size() - 1;
        if (vr_idx + 1 < 5uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        line_.forward(-1.0 * a101::correlation(high_, vol_rank_, std::min(idx, vr_idx), 5));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 5; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> vol_rank_;
};

// ============================================================================
// Alpha#045: -1 * (rank(sum(delay(close,5),20)/20) * correlation(close,volume,2)
//              * rank(correlation(sum(close,5), sum(close,20), 2)))
// Inputs: C,V   Warmup: 25
// ============================================================================
class Alpha101_045 : public Indicator<Alpha101_045> {
public:
    Alpha101_045(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            delayed_c_.data().reserve(close_.size());
            sma_delayed_.data().reserve(close_.size());
            sum5_.data().reserve(close_.size());
            sum20_.data().reserve(close_.size());
            corr2_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // delay(close, 5)
        const double dc = a101::delay(close_, idx, 5);
        delayed_c_.forward(std::isnan(dc) ? a101::NaN : dc);

        // sum(delay(close,5), 20) / 20 = SMA of delay(close,5) over 20
        const std::size_t di = delayed_c_.size() - 1;
        const double sd = (di + 1 >= 20uz) ? a101::sma(delayed_c_, di, 20) : a101::NaN;
        sma_delayed_.forward(sd);

        // sum(close, 5) and sum(close, 20)
        const double s5 = a101::sum(close_, idx, 5);
        sum5_.forward(std::isnan(s5) ? a101::NaN : s5);
        const double s20 = a101::sum(close_, idx, 20);
        sum20_.forward(std::isnan(s20) ? a101::NaN : s20);

        // correlation(sum(close,5), sum(close,20), 2)
        const std::size_t s5i = sum5_.size() - 1;
        const std::size_t s20i = sum20_.size() - 1;
        const double corr2 = (s5i + 1 >= 2uz && s20i + 1 >= 2uz)
            ? a101::correlation(sum5_, sum20_, std::min(s5i, s20i), 2) : a101::NaN;
        corr2_scratch_.forward(std::isnan(corr2) ? a101::NaN : corr2);

        if (idx + 1 < 25uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto rank_scratch = [](const Line<double>& s) {
            const std::size_t n = s.size();
            return a101::ts_rank(s, n - 1, std::min(n, 252uz));
        };

        const double r1 = rank_scratch(sma_delayed_);
        const double corr_cv = a101::correlation(close_, volume_, idx, 2);
        const double r2 = rank_scratch(corr2_scratch_);
        line_.forward(-1.0 * r1 * corr_cv * r2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 25; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> delayed_c_;
    Line<double> sma_delayed_;
    Line<double> sum5_;
    Line<double> sum20_;
    Line<double> corr2_scratch_;
};

class Alpha101_047 : public Indicator<Alpha101_047> {
public:
    Alpha101_047(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inv_close_scratch_.data().reserve(close_.size());
            high_minus_close_scratch_.data().reserve(close_.size());
            vwap_scratch_.data().reserve(close_.size());
            vwap_diff_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double c = close_.data()[idx];
        const double h = high_.data()[idx];
        const double v = volume_.data()[idx];

        inv_close_scratch_.forward((c != 0.0) ? (1.0 / c) : a101::NaN);
        high_minus_close_scratch_.forward(h - c);

        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_scratch_.forward(vw);

        // vwap - delay(vwap, 5): need at least 6 bars of vwap
        const auto vwap_sz = vwap_scratch_.size();
        if (vwap_sz >= 6uz) {
            const double vwap_delayed = vwap_scratch_.data()[vwap_sz - 1 - 5];
            vwap_diff_scratch_.forward(vw - vwap_delayed);
        } else {
            vwap_diff_scratch_.forward(a101::NaN);
        }

        // Warmup: adv20 needs 20, sum(high,5) needs 5, rank needs up to 252,
        // delay(vwap,5) needs 6. Conservative: 253
        if (idx + 1 < 253uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double rank_inv_close = a101::ts_rank(inv_close_scratch_,
            inv_close_scratch_.size() - 1, std::min(inv_close_scratch_.size(), 252uz));
        const double adv20 = a101::adv(volume_, idx, 20);

        const double part_a_num = rank_inv_close * v;
        const double part_a = (adv20 != 0.0) ? (part_a_num / adv20) : a101::NaN;

        const double rank_hmc = a101::ts_rank(high_minus_close_scratch_,
            high_minus_close_scratch_.size() - 1, std::min(high_minus_close_scratch_.size(), 252uz));
        const double sum_h5 = a101::sum(high_, idx, 5);
        const double avg_h5 = sum_h5 / 5.0;
        const double part_b = (avg_h5 != 0.0) ? ((h * rank_hmc) / avg_h5) : a101::NaN;

        const double rank_vwap_diff = a101::ts_rank(vwap_diff_scratch_,
            vwap_diff_scratch_.size() - 1, std::min(vwap_diff_scratch_.size(), 252uz));

        line_.forward(part_a * part_b - rank_vwap_diff);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> inv_close_scratch_;
    Line<double> high_minus_close_scratch_;
    Line<double> vwap_scratch_;
    Line<double> vwap_diff_scratch_;
};

class Alpha101_050 : public Indicator<Alpha101_050> {
public:
    Alpha101_050(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            rank_vol_scratch_.data().reserve(close_.size());
            rank_vwap_scratch_.data().reserve(close_.size());
            corr_scratch_.data().reserve(close_.size());
            rank_corr_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // Build rank(volume) scratch
        rank_vol_scratch_.forward(a101::NaN); // placeholder
        {
            const auto sz = rank_vol_scratch_.size();
            if (sz >= 1uz) {
                // overwrite with ts_rank of volume built so far
                // We need a scratch of raw volume to rank
                // Actually, rank(volume) = ts_rank of volume over rolling window
                // Use volume line directly
                const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
                rank_vol_scratch_.data().back() = rv;
            }
        }

        // Build rank(vwap) scratch
        const double vw = a101::vwap(high_, low_, close_, idx);
        // We need a vwap scratch to rank over
        // Store vwap first, then rank it
        if (vwap_scratch_.empty()) {
            vwap_scratch_.data().reserve(close_.size());
        }
        vwap_scratch_.forward(vw);
        {
            const double rvw = a101::ts_rank(vwap_scratch_, vwap_scratch_.size() - 1,
                std::min(vwap_scratch_.size(), 252uz));
            rank_vwap_scratch_.forward(rvw);
        }

        // correlation(rank(volume), rank(vwap), 5) needs 5 bars of both
        const auto sz_rv = rank_vol_scratch_.size();
        if (sz_rv >= 5uz) {
            const double corr = a101::correlation(rank_vol_scratch_, rank_vwap_scratch_,
                sz_rv - 1, 5);
            corr_scratch_.forward(corr);
        } else {
            corr_scratch_.forward(a101::NaN);
        }

        // rank(correlation(...)) -> ts_rank of corr_scratch
        {
            const auto csz = corr_scratch_.size();
            const double rc = a101::ts_rank(corr_scratch_, csz - 1,
                std::min(csz, 252uz));
            rank_corr_scratch_.forward(rc);
        }

        // ts_max(rank_corr, 5) needs 5 bars
        // Total warmup: 252 (rank) + 5 (corr) + 5 (ts_max) - some overlap ~ 261
        if (idx + 1 < 261uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto rcsz = rank_corr_scratch_.size();
        const double mx = a101::ts_max(rank_corr_scratch_, rcsz - 1, 5);
        line_.forward(-1.0 * mx);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 261; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_scratch_;
    Line<double> rank_vol_scratch_;
    Line<double> rank_vwap_scratch_;
    Line<double> corr_scratch_;
    Line<double> rank_corr_scratch_;
};

class Alpha101_052 : public Indicator<Alpha101_052> {
public:
    Alpha101_052(const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ts_min_low5_scratch_.data().reserve(close_.size());
            ret_ratio_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // Build ts_min(low, 5) scratch
        if (idx + 1 >= 5uz) {
            ts_min_low5_scratch_.forward(a101::ts_min(low_, idx, 5));
        } else {
            ts_min_low5_scratch_.forward(a101::NaN);
        }

        // Build returns ratio scratch: (sum(returns, 240) - sum(returns, 20)) / 220
        const double ret = a101::returns(close_, idx);
        returns_scratch_.forward(std::isnan(ret) ? a101::NaN : ret);
        {
            const auto rsz = returns_scratch_.size();
            if (rsz >= 240uz) {
                const double s240 = a101::sum(returns_scratch_, rsz - 1, 240);
                const double s20 = a101::sum(returns_scratch_, rsz - 1, 20);
                ret_ratio_scratch_.forward((s240 - s20) / 220.0);
            } else {
                ret_ratio_scratch_.forward(a101::NaN);
            }
        }

        // Warmup: ts_min(low,5)=5, delay(...,5)=10 bars of ts_min -> 10 bars of low,
        // sum(returns,240)=241 bars of close (1 for first return + 240),
        // rank needs some history. Conservative: 245
        if (idx + 1 < 245uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto tmsz = ts_min_low5_scratch_.size();
        const double cur_ts_min = ts_min_low5_scratch_.data()[tmsz - 1];
        const double delayed_ts_min = ts_min_low5_scratch_.data()[tmsz - 1 - 5];
        const double part_a = -1.0 * cur_ts_min + delayed_ts_min;

        const double rank_ret = a101::ts_rank(ret_ratio_scratch_,
            ret_ratio_scratch_.size() - 1,
            std::min(ret_ratio_scratch_.size(), 252uz));

        const double ts_rank_vol = a101::ts_rank(volume_, idx, 5);

        line_.forward(part_a * rank_ret * ts_rank_vol);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 245; }

private:
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> ts_min_low5_scratch_;
    Line<double> returns_scratch_;
    Line<double> ret_ratio_scratch_;
};

class Alpha101_055 : public Indicator<Alpha101_055> {
public:
    Alpha101_055(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            norm_scratch_.data().reserve(close_.size());
            rank_norm_scratch_.data().reserve(close_.size());
            rank_vol_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // (close - ts_min(low, 12)) / (ts_max(high, 12) - ts_min(low, 12))
        if (idx + 1 >= 12uz) {
            const double tmin = a101::ts_min(low_, idx, 12);
            const double tmax = a101::ts_max(high_, idx, 12);
            const double denom = tmax - tmin;
            const double c = close_.data()[idx];
            norm_scratch_.forward((denom != 0.0) ? ((c - tmin) / denom) : a101::NaN);
        } else {
            norm_scratch_.forward(a101::NaN);
        }

        // rank of norm
        {
            const auto nsz = norm_scratch_.size();
            rank_norm_scratch_.forward(a101::ts_rank(norm_scratch_, nsz - 1,
                std::min(nsz, 252uz)));
        }

        // rank(volume)
        rank_vol_scratch_.forward(a101::ts_rank(volume_, idx,
            std::min(idx + 1, 252uz)));

        // correlation(rank_norm, rank_vol, 6) needs 6 bars of both
        // Warmup: 12 (ts_min/max) + 252 (rank) + 6 (corr) ~ 264 conservative
        if (idx + 1 < 264uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto rnsz = rank_norm_scratch_.size();
        const double corr = a101::correlation(rank_norm_scratch_, rank_vol_scratch_,
            rnsz - 1, 6);
        line_.forward(-1.0 * corr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 264; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> norm_scratch_;
    Line<double> rank_norm_scratch_;
    Line<double> rank_vol_scratch_;
};

class Alpha101_056 : public Indicator<Alpha101_056> {
public:
    Alpha101_056(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            returns_scratch_.data().reserve(close_.size());
            sum_ret2_scratch_.data().reserve(close_.size());
            ratio_scratch_.data().reserve(close_.size());
            ret_cap_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // returns
        const double ret = a101::returns(close_, idx);
        returns_scratch_.forward(std::isnan(ret) ? a101::NaN : ret);

        // sum(returns, 2)
        const auto rsz = returns_scratch_.size();
        if (rsz >= 2uz) {
            sum_ret2_scratch_.forward(a101::sum(returns_scratch_, rsz - 1, 2));
        } else {
            sum_ret2_scratch_.forward(a101::NaN);
        }

        // sum(returns, 10) / sum(sum(returns, 2), 3)
        {
            const auto s2sz = sum_ret2_scratch_.size();
            if (rsz >= 10uz && s2sz >= 3uz) {
                const double s10 = a101::sum(returns_scratch_, rsz - 1, 10);
                const double ss23 = a101::sum(sum_ret2_scratch_, s2sz - 1, 3);
                ratio_scratch_.forward((ss23 != 0.0) ? (s10 / ss23) : a101::NaN);
            } else {
                ratio_scratch_.forward(a101::NaN);
            }
        }

        // returns * cap (cap = close * volume)
        {
            const double c = close_.data()[idx];
            const double v = volume_.data()[idx];
            ret_cap_scratch_.forward(std::isnan(ret) ? a101::NaN : ret * c * v);
        }

        // Warmup: 1 (returns) + 10 (sum) + 252 (rank) ~ 256
        if (idx + 1 < 256uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double rank_ratio = a101::ts_rank(ratio_scratch_,
            ratio_scratch_.size() - 1,
            std::min(ratio_scratch_.size(), 252uz));
        const double rank_rc = a101::ts_rank(ret_cap_scratch_,
            ret_cap_scratch_.size() - 1,
            std::min(ret_cap_scratch_.size(), 252uz));

        line_.forward(-1.0 * rank_ratio * rank_rc);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 256; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> returns_scratch_;
    Line<double> sum_ret2_scratch_;
    Line<double> ratio_scratch_;
    Line<double> ret_cap_scratch_;
};

class Alpha101_057 : public Indicator<Alpha101_057> {
public:
    Alpha101_057(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            argmax_scratch_.data().reserve(close_.size());
            rank_argmax_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // ts_argmax(close, 30)
        if (idx + 1 >= 30uz) {
            argmax_scratch_.forward(static_cast<double>(a101::ts_argmax(close_, idx, 30)));
        } else {
            argmax_scratch_.forward(a101::NaN);
        }

        // rank(ts_argmax(close, 30))
        {
            const auto asz = argmax_scratch_.size();
            rank_argmax_scratch_.forward(a101::ts_rank(argmax_scratch_, asz - 1,
                std::min(asz, 252uz)));
        }

        // decay_linear(rank_argmax, 2) needs 2 bars
        // Warmup: 30 (argmax) + 252 (rank) + 2 (decay) ~ 284
        if (idx + 1 < 284uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double c = close_.data()[idx];
        const double vw = a101::vwap(high_, low_, close_, idx);
        const auto rasz = rank_argmax_scratch_.size();
        const double dl = a101::decay_linear(rank_argmax_scratch_, rasz - 1, 2);

        if (dl != 0.0) {
            line_.forward(-1.0 * ((c - vw) / dl));
        } else {
            line_.forward(a101::NaN);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 284; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> argmax_scratch_;
    Line<double> rank_argmax_scratch_;
};

class Alpha101_060 : public Indicator<Alpha101_060> {
public:
    Alpha101_060(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            mfv_scratch_.data().reserve(close_.size());
            rank_mfv_scratch_.data().reserve(close_.size());
            argmax_scratch_.data().reserve(close_.size());
            rank_argmax_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // ((close-low)-(high-close))/(high-low) * volume  =>  (2*close - high - low)/(high - low) * volume
        {
            const double c = close_.data()[idx];
            const double h = high_.data()[idx];
            const double l = low_.data()[idx];
            const double v = volume_.data()[idx];
            const double denom = h - l;
            if (denom != 0.0) {
                mfv_scratch_.forward(((c - l - (h - c)) / denom) * v);
            } else {
                mfv_scratch_.forward(a101::NaN);
            }
        }

        // rank(mfv)
        {
            const auto msz = mfv_scratch_.size();
            rank_mfv_scratch_.forward(a101::ts_rank(mfv_scratch_, msz - 1,
                std::min(msz, 252uz)));
        }

        // ts_argmax(close, 10)
        if (idx + 1 >= 10uz) {
            argmax_scratch_.forward(static_cast<double>(a101::ts_argmax(close_, idx, 10)));
        } else {
            argmax_scratch_.forward(a101::NaN);
        }

        // rank(ts_argmax(close, 10))
        {
            const auto asz = argmax_scratch_.size();
            rank_argmax_scratch_.forward(a101::ts_rank(argmax_scratch_, asz - 1,
                std::min(asz, 252uz)));
        }

        // Warmup: 252 (rank) + 10 (argmax) ~ 262
        if (idx + 1 < 262uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // scale: running abs_sum for each rank stream
        const double rmfv = rank_mfv_scratch_.data()[rank_mfv_scratch_.size() - 1];
        const double rarg = rank_argmax_scratch_.data()[rank_argmax_scratch_.size() - 1];

        abs_sum_mfv_ += std::abs(rmfv);
        abs_sum_arg_ += std::abs(rarg);
        valid_count_++;

        const double scaled_mfv = (abs_sum_mfv_ != 0.0)
            ? a101::scale(rmfv, abs_sum_mfv_) : a101::NaN;
        const double scaled_arg = (abs_sum_arg_ != 0.0)
            ? a101::scale(rarg, abs_sum_arg_) : a101::NaN;

        line_.forward(-1.0 * (2.0 * scaled_mfv - scaled_arg));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> mfv_scratch_;
    Line<double> rank_mfv_scratch_;
    Line<double> argmax_scratch_;
    Line<double> rank_argmax_scratch_;
    double abs_sum_mfv_ = 0.0;
    double abs_sum_arg_ = 0.0;
    std::size_t valid_count_ = 0;
};

class Alpha101_061 : public Indicator<Alpha101_061> {
public:
    Alpha101_061(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_scratch_.data().reserve(close_.size());
            vwap_diff_scratch_.data().reserve(close_.size());
            adv180_scratch_.data().reserve(close_.size());
            corr_scratch_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // vwap
        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_scratch_.forward(vw);

        const auto vsz = vwap_scratch_.size();

        // vwap - ts_min(vwap, 16)
        if (vsz >= 16uz) {
            const double tmin = a101::ts_min(vwap_scratch_, vsz - 1, 16);
            vwap_diff_scratch_.forward(vw - tmin);
        } else {
            vwap_diff_scratch_.forward(a101::NaN);
        }

        // adv180
        if (idx + 1 >= 180uz) {
            adv180_scratch_.forward(a101::adv(volume_, idx, 180));
        } else {
            adv180_scratch_.forward(a101::NaN);
        }

        // correlation(vwap, adv180, 18) — need 18 bars of both
        {
            const auto a180sz = adv180_scratch_.size();
            if (a180sz >= 18uz && vsz >= 18uz) {
                // Align: both scratches have same size (one per bar), use vwap_scratch and adv180_scratch
                corr_scratch_.forward(a101::correlation(vwap_scratch_, adv180_scratch_,
                    vsz - 1, 18));
            } else {
                corr_scratch_.forward(a101::NaN);
            }
        }

        // Warmup: 180 (adv180) + 18 (corr) + 252 (rank) ~ 270 conservative
        // But the rank windows can overlap. Use 270.
        if (idx + 1 < 270uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double rank_vwap_diff = a101::ts_rank(vwap_diff_scratch_,
            vwap_diff_scratch_.size() - 1,
            std::min(vwap_diff_scratch_.size(), 252uz));
        const double rank_corr = a101::ts_rank(corr_scratch_,
            corr_scratch_.size() - 1,
            std::min(corr_scratch_.size(), 252uz));

        // Boolean: rank_vwap_diff < rank_corr -> 1.0, else 0.0
        line_.forward((rank_vwap_diff < rank_corr) ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 270; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_scratch_;
    Line<double> vwap_diff_scratch_;
    Line<double> adv180_scratch_;
    Line<double> corr_scratch_;
};

// ============================================================================
// Alpha#062: ((rank(correlation(vwap, sum(adv20, 22), 10)) < rank(((rank(open) + rank(open)) < (rank(((high + low) / 2)) + rank(high))))) * -1)
// Inputs: O, H, L, C, V   Warmup: 52
// ============================================================================
class Alpha101_062 : public Indicator<Alpha101_062> {
public:
    Alpha101_062(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
            adv20_.data().reserve(close_.size());
            sum_adv20_.data().reserve(close_.size());
            corr_scratch_.data().reserve(close_.size());
            rank_open_.data().reserve(close_.size());
            hl2_.data().reserve(close_.size());
            rank_hl2_.data().reserve(close_.size());
            rank_high_.data().reserve(close_.size());
            inner_bool_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // Build vwap scratch
        vwap_.forward(a101::vwap(high_, low_, close_, idx));

        // Build adv20 scratch
        const double av = a101::adv(volume_, idx, 20);
        adv20_.forward(std::isnan(av) ? a101::NaN : av);

        // Build sum(adv20, 22) scratch
        const std::size_t aidx = adv20_.size() - 1;
        const double s22 = a101::sum(adv20_, aidx, 22);
        sum_adv20_.forward(std::isnan(s22) ? a101::NaN : s22);

        // Build correlation(vwap, sum_adv20, 10) scratch
        const std::size_t vidx = vwap_.size() - 1;
        const std::size_t sidx = sum_adv20_.size() - 1;
        const std::size_t corr_min = std::min(vidx, sidx);
        const double corr = (corr_min + 1 >= 10uz)
            ? a101::correlation(vwap_, sum_adv20_, corr_min, 10)
            : a101::NaN;
        corr_scratch_.forward(corr);

        // Build rank(open) scratch — ts_rank approximation
        const double ro = a101::ts_rank(open_, idx, std::min(idx + 1, 252uz));
        rank_open_.forward(ro);

        // Build hl2 scratch
        const double hl2 = (high_.data()[idx] + low_.data()[idx]) / 2.0;
        hl2_.forward(hl2);

        // Build rank(hl2) scratch
        const std::size_t hidx = hl2_.size() - 1;
        const double rhl2 = a101::ts_rank(hl2_, hidx, std::min(hidx + 1, 252uz));
        rank_hl2_.forward(rhl2);

        // Build rank(high) scratch
        const double rh = a101::ts_rank(high_, idx, std::min(idx + 1, 252uz));
        rank_high_.forward(rh);

        // Inner boolean: (rank(open) + rank(open)) < (rank(hl2) + rank(high))
        const std::size_t roidx = rank_open_.size() - 1;
        const std::size_t rhl2idx = rank_hl2_.size() - 1;
        const std::size_t rhidx = rank_high_.size() - 1;
        const double lhs = rank_open_.data()[roidx] + rank_open_.data()[roidx];
        const double rhs = rank_hl2_.data()[rhl2idx] + rank_high_.data()[rhidx];
        inner_bool_.forward((lhs < rhs) ? 1.0 : 0.0);

        if (idx + 1 < 52uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // rank(correlation(...))
        const std::size_t cidx = corr_scratch_.size() - 1;
        const double rank_corr = a101::ts_rank(corr_scratch_, cidx, std::min(cidx + 1, 252uz));

        // rank(inner_bool)
        const std::size_t bidx = inner_bool_.size() - 1;
        const double rank_bool = a101::ts_rank(inner_bool_, bidx, std::min(bidx + 1, 252uz));

        line_.forward(((rank_corr < rank_bool) ? 1.0 : 0.0) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 52; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> adv20_;
    Line<double> sum_adv20_;
    Line<double> corr_scratch_;
    Line<double> rank_open_;
    Line<double> hl2_;
    Line<double> rank_hl2_;
    Line<double> rank_high_;
    Line<double> inner_bool_;
};

// ============================================================================
// Alpha#064: ((rank(correlation(sum(open*0.178404+low*(1-0.178404),13), sum(adv120,13), 17)) < rank(delta(((high+low)/2)*0.178404+vwap*(1-0.178404), 4))) * -1)
// Inputs: O, H, L, C, V   Warmup: 148
// ============================================================================
class Alpha101_064 : public Indicator<Alpha101_064> {
public:
    Alpha101_064(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            blend_ol_.data().reserve(close_.size());
            adv120_.data().reserve(close_.size());
            sum_blend_.data().reserve(close_.size());
            sum_adv_.data().reserve(close_.size());
            corr_.data().reserve(close_.size());
            blend_hv_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // blend_ol = open * 0.178404 + low * (1 - 0.178404)
        const double ol = open_.data()[idx] * 0.178404 + low_.data()[idx] * (1.0 - 0.178404);
        blend_ol_.forward(ol);

        // adv120
        const double av = a101::adv(volume_, idx, 120);
        adv120_.forward(std::isnan(av) ? a101::NaN : av);

        // sum(blend_ol, 13)
        const std::size_t blidx = blend_ol_.size() - 1;
        const double sb = a101::sum(blend_ol_, blidx, 13);
        sum_blend_.forward(std::isnan(sb) ? a101::NaN : sb);

        // sum(adv120, 13)
        const std::size_t aidx = adv120_.size() - 1;
        const double sa = a101::sum(adv120_, aidx, 13);
        sum_adv_.forward(std::isnan(sa) ? a101::NaN : sa);

        // correlation(sum_blend, sum_adv, 17)
        const std::size_t sbidx = sum_blend_.size() - 1;
        const std::size_t saidx = sum_adv_.size() - 1;
        const std::size_t corr_min = std::min(sbidx, saidx);
        const double c = (corr_min + 1 >= 17uz)
            ? a101::correlation(sum_blend_, sum_adv_, corr_min, 17)
            : a101::NaN;
        corr_.forward(c);

        // blend_hv = ((high+low)/2)*0.178404 + vwap*(1-0.178404)
        const double vw = a101::vwap(high_, low_, close_, idx);
        const double hl2 = (high_.data()[idx] + low_.data()[idx]) / 2.0;
        const double hv = hl2 * 0.178404 + vw * (1.0 - 0.178404);
        blend_hv_.forward(hv);

        if (idx + 1 < 148uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // rank(correlation)
        const std::size_t cidx = corr_.size() - 1;
        const double rank_corr = a101::ts_rank(corr_, cidx, std::min(cidx + 1, 252uz));

        // rank(delta(blend_hv, 4))
        const std::size_t hvidx = blend_hv_.size() - 1;
        const double d4 = a101::delta(blend_hv_, hvidx, 4);
        // We need a scratch for delta values to do ts_rank on them
        // Instead, since rank() is ts_rank of the expression over history,
        // we can compute the delta value and compare ranks inline.
        // For proper rank approximation we need to accumulate delta values.
        delta_hv_.forward(std::isnan(d4) ? a101::NaN : d4);
        const std::size_t didx = delta_hv_.size() - 1;
        const double rank_delta = a101::ts_rank(delta_hv_, didx, std::min(didx + 1, 252uz));

        line_.forward(((rank_corr < rank_delta) ? 1.0 : 0.0) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 148; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> blend_ol_;
    Line<double> adv120_;
    Line<double> sum_blend_;
    Line<double> sum_adv_;
    Line<double> corr_;
    Line<double> blend_hv_;
    Line<double> delta_hv_;
};

// ============================================================================
// Alpha#065: ((rank(correlation(open*0.00817205+vwap*(1-0.00817205), sum(adv60,9), 6)) < rank(open - ts_min(open, 14))) * -1)
// Inputs: O, H, L, C, V   Warmup: 73
// ============================================================================
class Alpha101_065 : public Indicator<Alpha101_065> {
public:
    Alpha101_065(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            blend_ov_.data().reserve(close_.size());
            adv60_.data().reserve(close_.size());
            sum_adv_.data().reserve(close_.size());
            corr_.data().reserve(close_.size());
            open_range_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // blend = open * 0.00817205 + vwap * (1 - 0.00817205)
        const double vw = a101::vwap(high_, low_, close_, idx);
        blend_ov_.forward(open_.data()[idx] * 0.00817205 + vw * (1.0 - 0.00817205));

        // adv60
        const double av = a101::adv(volume_, idx, 60);
        adv60_.forward(std::isnan(av) ? a101::NaN : av);

        // sum(adv60, 9)
        const std::size_t aidx = adv60_.size() - 1;
        const double sa = a101::sum(adv60_, aidx, 9);
        sum_adv_.forward(std::isnan(sa) ? a101::NaN : sa);

        // correlation(blend, sum_adv, 6)
        const std::size_t blidx = blend_ov_.size() - 1;
        const std::size_t saidx = sum_adv_.size() - 1;
        const std::size_t corr_min = std::min(blidx, saidx);
        const double c = (corr_min + 1 >= 6uz)
            ? a101::correlation(blend_ov_, sum_adv_, corr_min, 6)
            : a101::NaN;
        corr_.forward(c);

        // open - ts_min(open, 14)
        const double mn = a101::ts_min(open_, idx, 14);
        const double or_val = std::isnan(mn) ? a101::NaN : (open_.data()[idx] - mn);
        open_range_.forward(or_val);

        if (idx + 1 < 73uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // rank(correlation)
        const std::size_t cidx = corr_.size() - 1;
        const double rank_corr = a101::ts_rank(corr_, cidx, std::min(cidx + 1, 252uz));

        // rank(open - ts_min(open, 14))
        const std::size_t oridx = open_range_.size() - 1;
        const double rank_range = a101::ts_rank(open_range_, oridx, std::min(oridx + 1, 252uz));

        line_.forward(((rank_corr < rank_range) ? 1.0 : 0.0) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 73; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> blend_ov_;
    Line<double> adv60_;
    Line<double> sum_adv_;
    Line<double> corr_;
    Line<double> open_range_;
};

// ============================================================================
// Alpha#066: ((rank(decay_linear(delta(vwap,4),7)) + Ts_Rank(decay_linear(((low-vwap)/(open-((high+low)/2))),11),7)) * -1)
// Inputs: O, H, L, C, V   Warmup: 17
// ============================================================================
class Alpha101_066 : public Indicator<Alpha101_066> {
public:
    Alpha101_066(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
            delta_vwap_.data().reserve(close_.size());
            dl_delta_.data().reserve(close_.size());
            ratio_.data().reserve(close_.size());
            dl_ratio_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // vwap
        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);

        // delta(vwap, 4)
        const std::size_t vidx = vwap_.size() - 1;
        const double dv = a101::delta(vwap_, vidx, 4);
        delta_vwap_.forward(std::isnan(dv) ? a101::NaN : dv);

        // decay_linear(delta_vwap, 7)
        const std::size_t dvidx = delta_vwap_.size() - 1;
        const double dl1 = a101::decay_linear(delta_vwap_, dvidx, 7);
        dl_delta_.forward(std::isnan(dl1) ? a101::NaN : dl1);

        // (low - vwap) / (open - (high+low)/2)
        const double l = low_.data()[idx];
        const double o = open_.data()[idx];
        const double h = high_.data()[idx];
        const double hl2 = (h + l) / 2.0;
        const double denom = o - hl2;
        const double rat = (denom == 0.0) ? a101::NaN : ((l - vw) / denom);
        ratio_.forward(rat);

        // decay_linear(ratio, 11)
        const std::size_t ridx = ratio_.size() - 1;
        const double dl2 = a101::decay_linear(ratio_, ridx, 11);
        dl_ratio_.forward(std::isnan(dl2) ? a101::NaN : dl2);

        if (idx + 1 < 17uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // rank(decay_linear(delta(vwap,4),7)) — ts_rank approximation
        const std::size_t dlidx = dl_delta_.size() - 1;
        const double rank1 = a101::ts_rank(dl_delta_, dlidx, std::min(dlidx + 1, 252uz));

        // Ts_Rank(decay_linear(ratio, 11), 7)
        const std::size_t dlridx = dl_ratio_.size() - 1;
        const double tsr2 = a101::ts_rank(dl_ratio_, dlridx, 7);

        line_.forward(-1.0 * (rank1 + tsr2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 17; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> delta_vwap_;
    Line<double> dl_delta_;
    Line<double> ratio_;
    Line<double> dl_ratio_;
};

// ============================================================================
// Alpha#068: ((Ts_Rank(correlation(rank(high), rank(adv15), 9), 14) < rank(delta(close*0.518371+low*(1-0.518371), 1))) * -1)
// Inputs: H, L, C, V   Warmup: 38
// ============================================================================
class Alpha101_068 : public Indicator<Alpha101_068> {
public:
    Alpha101_068(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            adv15_.data().reserve(close_.size());
            rank_high_.data().reserve(close_.size());
            rank_adv_.data().reserve(close_.size());
            corr_.data().reserve(close_.size());
            blend_cl_.data().reserve(close_.size());
            delta_blend_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // adv15
        const double av = a101::adv(volume_, idx, 15);
        adv15_.forward(std::isnan(av) ? a101::NaN : av);

        // rank(high) — ts_rank approximation
        const double rh = a101::ts_rank(high_, idx, std::min(idx + 1, 252uz));
        rank_high_.forward(rh);

        // rank(adv15) — ts_rank approximation
        const std::size_t aidx = adv15_.size() - 1;
        const double ra = a101::ts_rank(adv15_, aidx, std::min(aidx + 1, 252uz));
        rank_adv_.forward(ra);

        // correlation(rank_high, rank_adv, 9)
        const std::size_t rhidx = rank_high_.size() - 1;
        const std::size_t raidx = rank_adv_.size() - 1;
        const std::size_t corr_min = std::min(rhidx, raidx);
        const double c = (corr_min + 1 >= 9uz)
            ? a101::correlation(rank_high_, rank_adv_, corr_min, 9)
            : a101::NaN;
        corr_.forward(c);

        // blend = close * 0.518371 + low * (1 - 0.518371)
        blend_cl_.forward(close_.data()[idx] * 0.518371 + low_.data()[idx] * (1.0 - 0.518371));

        // delta(blend, 1)
        const std::size_t blidx = blend_cl_.size() - 1;
        const double d1 = a101::delta(blend_cl_, blidx, 1);
        delta_blend_.forward(std::isnan(d1) ? a101::NaN : d1);

        if (idx + 1 < 38uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // Ts_Rank(correlation, 14)
        const std::size_t cidx = corr_.size() - 1;
        const double tsr = a101::ts_rank(corr_, cidx, 14);

        // rank(delta(blend, 1))
        const std::size_t didx = delta_blend_.size() - 1;
        const double rank_delta = a101::ts_rank(delta_blend_, didx, std::min(didx + 1, 252uz));

        line_.forward(((tsr < rank_delta) ? 1.0 : 0.0) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 38; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> adv15_;
    Line<double> rank_high_;
    Line<double> rank_adv_;
    Line<double> corr_;
    Line<double> blend_cl_;
    Line<double> delta_blend_;
};

// ============================================================================
// Alpha#071: max(Ts_Rank(decay_linear(correlation(Ts_Rank(close,3),Ts_Rank(adv180,12),18),4),16), Ts_Rank(decay_linear((rank((low+open)-(vwap+vwap))^2),16),4))
// Inputs: O, H, L, C, V   Warmup: 213
// ============================================================================
class Alpha101_071 : public Indicator<Alpha101_071> {
public:
    Alpha101_071(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            adv180_.data().reserve(close_.size());
            tsr_close_.data().reserve(close_.size());
            tsr_adv_.data().reserve(close_.size());
            corr_.data().reserve(close_.size());
            dl_corr_.data().reserve(close_.size());
            vwap_.data().reserve(close_.size());
            lo_minus_2vwap_.data().reserve(close_.size());
            rank_expr_.data().reserve(close_.size());
            rank_sq_.data().reserve(close_.size());
            dl_rank_sq_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // --- Branch 1: Ts_Rank(decay_linear(correlation(Ts_Rank(close,3),Ts_Rank(adv180,12),18),4),16) ---

        // adv180
        const double av = a101::adv(volume_, idx, 180);
        adv180_.forward(std::isnan(av) ? a101::NaN : av);

        // Ts_Rank(close, 3)
        const double trc = a101::ts_rank(close_, idx, 3);
        tsr_close_.forward(trc);

        // Ts_Rank(adv180, 12)
        const std::size_t aidx = adv180_.size() - 1;
        const double tra = (aidx + 1 >= 12uz)
            ? a101::ts_rank(adv180_, aidx, 12)
            : a101::NaN;
        tsr_adv_.forward(tra);

        // correlation(tsr_close, tsr_adv, 18)
        const std::size_t tcidx = tsr_close_.size() - 1;
        const std::size_t taidx = tsr_adv_.size() - 1;
        const std::size_t corr_min = std::min(tcidx, taidx);
        const double c = (corr_min + 1 >= 18uz)
            ? a101::correlation(tsr_close_, tsr_adv_, corr_min, 18)
            : a101::NaN;
        corr_.forward(c);

        // decay_linear(corr, 4)
        const std::size_t cidx = corr_.size() - 1;
        const double dl1 = a101::decay_linear(corr_, cidx, 4);
        dl_corr_.forward(std::isnan(dl1) ? a101::NaN : dl1);

        // --- Branch 2: Ts_Rank(decay_linear((rank((low+open)-(vwap+vwap))^2), 16), 4) ---

        // vwap
        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);

        // (low + open) - (vwap + vwap)
        const double expr = (low_.data()[idx] + open_.data()[idx]) - (vw + vw);
        lo_minus_2vwap_.forward(expr);

        // rank((low+open)-(vwap+vwap)) — ts_rank approximation
        const std::size_t lidx = lo_minus_2vwap_.size() - 1;
        const double rk = a101::ts_rank(lo_minus_2vwap_, lidx, std::min(lidx + 1, 252uz));
        rank_expr_.forward(rk);

        // rank(...)^2
        rank_sq_.forward(rk * rk);

        // decay_linear(rank_sq, 16)
        const std::size_t rsidx = rank_sq_.size() - 1;
        const double dl2 = a101::decay_linear(rank_sq_, rsidx, 16);
        dl_rank_sq_.forward(std::isnan(dl2) ? a101::NaN : dl2);

        if (idx + 1 < 213uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // Ts_Rank(dl_corr, 16)
        const std::size_t dc_idx = dl_corr_.size() - 1;
        const double tsr1 = a101::ts_rank(dl_corr_, dc_idx, 16);

        // Ts_Rank(dl_rank_sq, 4)
        const std::size_t dr_idx = dl_rank_sq_.size() - 1;
        const double tsr2 = a101::ts_rank(dl_rank_sq_, dr_idx, 4);

        line_.forward(std::max(tsr1, tsr2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 213; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> adv180_;
    Line<double> tsr_close_;
    Line<double> tsr_adv_;
    Line<double> corr_;
    Line<double> dl_corr_;
    Line<double> vwap_;
    Line<double> lo_minus_2vwap_;
    Line<double> rank_expr_;
    Line<double> rank_sq_;
    Line<double> dl_rank_sq_;
};

// ============================================================================
// Alpha#072: (rank(decay_linear(correlation((high+low)/2, adv40, 9), 10)) / rank(decay_linear(correlation(Ts_Rank(vwap,4), Ts_Rank(volume,19), 7), 3)))
// Inputs: H, L, C, V   Warmup: 58
// ============================================================================
class Alpha101_072 : public Indicator<Alpha101_072> {
public:
    Alpha101_072(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            hl2_.data().reserve(close_.size());
            adv40_.data().reserve(close_.size());
            corr1_.data().reserve(close_.size());
            dl1_.data().reserve(close_.size());
            vwap_.data().reserve(close_.size());
            tsr_vwap_.data().reserve(close_.size());
            tsr_vol_.data().reserve(close_.size());
            corr2_.data().reserve(close_.size());
            dl2_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // --- Numerator: rank(decay_linear(correlation(hl2, adv40, 9), 10)) ---

        // hl2
        hl2_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);

        // adv40
        const double av = a101::adv(volume_, idx, 40);
        adv40_.forward(std::isnan(av) ? a101::NaN : av);

        // correlation(hl2, adv40, 9)
        const std::size_t hlidx = hl2_.size() - 1;
        const std::size_t aidx = adv40_.size() - 1;
        const std::size_t cm1 = std::min(hlidx, aidx);
        const double c1 = (cm1 + 1 >= 9uz)
            ? a101::correlation(hl2_, adv40_, cm1, 9)
            : a101::NaN;
        corr1_.forward(c1);

        // decay_linear(corr1, 10)
        const std::size_t c1idx = corr1_.size() - 1;
        const double d1 = a101::decay_linear(corr1_, c1idx, 10);
        dl1_.forward(std::isnan(d1) ? a101::NaN : d1);

        // --- Denominator: rank(decay_linear(correlation(Ts_Rank(vwap,4), Ts_Rank(volume,19), 7), 3)) ---

        // vwap
        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);

        // Ts_Rank(vwap, 4)
        const std::size_t vidx = vwap_.size() - 1;
        const double trv = (vidx + 1 >= 4uz)
            ? a101::ts_rank(vwap_, vidx, 4)
            : a101::NaN;
        tsr_vwap_.forward(trv);

        // Ts_Rank(volume, 19)
        const double trvo = a101::ts_rank(volume_, idx, 19);
        tsr_vol_.forward(std::isnan(trvo) ? a101::NaN : trvo);

        // correlation(tsr_vwap, tsr_vol, 7)
        const std::size_t tvwidx = tsr_vwap_.size() - 1;
        const std::size_t tvolidx = tsr_vol_.size() - 1;
        const std::size_t cm2 = std::min(tvwidx, tvolidx);
        const double c2 = (cm2 + 1 >= 7uz)
            ? a101::correlation(tsr_vwap_, tsr_vol_, cm2, 7)
            : a101::NaN;
        corr2_.forward(c2);

        // decay_linear(corr2, 3)
        const std::size_t c2idx = corr2_.size() - 1;
        const double d2 = a101::decay_linear(corr2_, c2idx, 3);
        dl2_.forward(std::isnan(d2) ? a101::NaN : d2);

        if (idx + 1 < 58uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // rank(dl1) / rank(dl2)
        const std::size_t d1idx = dl1_.size() - 1;
        const double rank_num = a101::ts_rank(dl1_, d1idx, std::min(d1idx + 1, 252uz));

        const std::size_t d2idx = dl2_.size() - 1;
        const double rank_den = a101::ts_rank(dl2_, d2idx, std::min(d2idx + 1, 252uz));

        if (rank_den == 0.0) {
            line_.forward(a101::NaN);
        } else {
            line_.forward(rank_num / rank_den);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 58; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> hl2_;
    Line<double> adv40_;
    Line<double> corr1_;
    Line<double> dl1_;
    Line<double> vwap_;
    Line<double> tsr_vwap_;
    Line<double> tsr_vol_;
    Line<double> corr2_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#073: (max(rank(decay_linear(delta(vwap,5),3)), Ts_Rank(decay_linear(((delta(open*0.147155+low*(1-0.147155),2)/(open*0.147155+low*(1-0.147155)))*-1),3),17)) * -1)
// Inputs: O, H, L, C, V   Warmup: 22
// ============================================================================
class Alpha101_073 : public Indicator<Alpha101_073> {
public:
    Alpha101_073(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
            delta_vwap_.data().reserve(close_.size());
            dl_dvwap_.data().reserve(close_.size());
            blend_ol_.data().reserve(close_.size());
            neg_ret_.data().reserve(close_.size());
            dl_neg_ret_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // --- Branch 1: rank(decay_linear(delta(vwap, 5), 3)) ---

        // vwap
        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);

        // delta(vwap, 5)
        const std::size_t vidx = vwap_.size() - 1;
        const double dv = a101::delta(vwap_, vidx, 5);
        delta_vwap_.forward(std::isnan(dv) ? a101::NaN : dv);

        // decay_linear(delta_vwap, 3)
        const std::size_t dvidx = delta_vwap_.size() - 1;
        const double dl1 = a101::decay_linear(delta_vwap_, dvidx, 3);
        dl_dvwap_.forward(std::isnan(dl1) ? a101::NaN : dl1);

        // --- Branch 2: Ts_Rank(decay_linear(((delta(blend,2)/blend)*-1), 3), 17) ---

        // blend = open * 0.147155 + low * (1 - 0.147155)
        const double bl = open_.data()[idx] * 0.147155 + low_.data()[idx] * (1.0 - 0.147155);
        blend_ol_.forward(bl);

        // (delta(blend, 2) / blend) * -1
        const std::size_t blidx = blend_ol_.size() - 1;
        const double db = a101::delta(blend_ol_, blidx, 2);
        double nr = a101::NaN;
        if (!std::isnan(db) && bl != 0.0) {
            nr = -1.0 * (db / bl);
        }
        neg_ret_.forward(nr);

        // decay_linear(neg_ret, 3)
        const std::size_t nridx = neg_ret_.size() - 1;
        const double dl2 = a101::decay_linear(neg_ret_, nridx, 3);
        dl_neg_ret_.forward(std::isnan(dl2) ? a101::NaN : dl2);

        if (idx + 1 < 22uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // rank(dl_dvwap) — ts_rank approximation
        const std::size_t dd_idx = dl_dvwap_.size() - 1;
        const double rank1 = a101::ts_rank(dl_dvwap_, dd_idx, std::min(dd_idx + 1, 252uz));

        // Ts_Rank(dl_neg_ret, 17)
        const std::size_t dn_idx = dl_neg_ret_.size() - 1;
        const double tsr2 = a101::ts_rank(dl_neg_ret_, dn_idx, 17);

        line_.forward(-1.0 * std::max(rank1, tsr2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 22; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> delta_vwap_;
    Line<double> dl_dvwap_;
    Line<double> blend_ol_;
    Line<double> neg_ret_;
    Line<double> dl_neg_ret_;
};

// ============================================================================
// Alpha#074: ((rank(correlation(close, sum(adv30, 37), 15)) <
//             rank(correlation(rank(high*0.0261661+vwap*0.9738339), rank(volume), 11))) * -1)
// Inputs: H,L,C,V   Warmup: 80
// ============================================================================
class Alpha101_074 : public Indicator<Alpha101_074> {
public:
    Alpha101_074(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            adv30_.data().reserve(close_.size());
            sum_adv30_37_.data().reserve(close_.size());
            corr_lhs_.data().reserve(close_.size());
            weighted_.data().reserve(close_.size());
            rank_weighted_.data().reserve(close_.size());
            rank_vol_.data().reserve(close_.size());
            corr_rhs_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double av30 = a101::adv(volume_, idx, 30);
        adv30_.forward(std::isnan(av30) ? a101::NaN : av30);
        const std::size_t aidx = adv30_.size() - 1;

        const double s37 = a101::sum(adv30_, aidx, 37);
        sum_adv30_37_.forward(std::isnan(s37) ? a101::NaN : s37);
        const std::size_t sidx = sum_adv30_37_.size() - 1;

        double cl = a101::NaN;
        if (sidx + 1 >= 15 && !std::isnan(sum_adv30_37_.data()[sidx])) {
            cl = a101::correlation(close_, sum_adv30_37_, std::min(idx, sidx), 15);
        }
        corr_lhs_.forward(std::isnan(cl) ? a101::NaN : cl);

        const double vw = a101::vwap(high_, low_, close_, idx);
        weighted_.forward(high_.data()[idx] * 0.0261661 + vw * (1.0 - 0.0261661));
        const std::size_t widx = weighted_.size() - 1;

        const double rw = a101::ts_rank(weighted_, widx, std::min(weighted_.size(), 252uz));
        rank_weighted_.forward(rw);

        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rank_vol_.forward(rv);

        const std::size_t rwidx = rank_weighted_.size() - 1;
        const std::size_t rvidx = rank_vol_.size() - 1;
        double cr = a101::NaN;
        if (rwidx + 1 >= 11 && rvidx + 1 >= 11) {
            cr = a101::correlation(rank_weighted_, rank_vol_, std::min(rwidx, rvidx), 11);
        }
        corr_rhs_.forward(std::isnan(cr) ? a101::NaN : cr);

        if (idx + 1 < 80uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t clidx = corr_lhs_.size() - 1;
        const std::size_t cridx = corr_rhs_.size() - 1;
        const double rank_l = a101::ts_rank(corr_lhs_, clidx, std::min(corr_lhs_.size(), 252uz));
        const double rank_r = a101::ts_rank(corr_rhs_, cridx, std::min(corr_rhs_.size(), 252uz));

        if (std::isnan(rank_l) || std::isnan(rank_r)) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward((rank_l < rank_r ? 1.0 : 0.0) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 80; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> adv30_;
    Line<double> sum_adv30_37_;
    Line<double> corr_lhs_;
    Line<double> weighted_;
    Line<double> rank_weighted_;
    Line<double> rank_vol_;
    Line<double> corr_rhs_;
};

// ============================================================================
// Alpha#075: (rank(correlation(vwap, volume, 4)) <
//             rank(correlation(rank(low), rank(adv50), 12)))
// Inputs: H,L,C,V   Warmup: 62
// ============================================================================
class Alpha101_075 : public Indicator<Alpha101_075> {
public:
    Alpha101_075(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
            corr_lhs_.data().reserve(close_.size());
            adv50_.data().reserve(close_.size());
            rank_low_.data().reserve(close_.size());
            rank_adv50_.data().reserve(close_.size());
            corr_rhs_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        vwap_.forward(a101::vwap(high_, low_, close_, idx));
        const std::size_t vidx = vwap_.size() - 1;

        double cl = a101::NaN;
        if (vidx + 1 >= 4 && idx + 1 >= 4) {
            cl = a101::correlation(vwap_, volume_, std::min(vidx, idx), 4);
        }
        corr_lhs_.forward(std::isnan(cl) ? a101::NaN : cl);

        const double av50 = a101::adv(volume_, idx, 50);
        adv50_.forward(std::isnan(av50) ? a101::NaN : av50);
        const std::size_t aidx = adv50_.size() - 1;

        const double rl = a101::ts_rank(low_, idx, std::min(idx + 1, 252uz));
        rank_low_.forward(rl);

        const double ra = std::isnan(adv50_.data()[aidx])
            ? a101::NaN
            : a101::ts_rank(adv50_, aidx, std::min(adv50_.size(), 252uz));
        rank_adv50_.forward(std::isnan(ra) ? a101::NaN : ra);

        const std::size_t rlidx = rank_low_.size() - 1;
        const std::size_t raidx = rank_adv50_.size() - 1;
        double cr = a101::NaN;
        if (rlidx + 1 >= 12 && raidx + 1 >= 12) {
            cr = a101::correlation(rank_low_, rank_adv50_, std::min(rlidx, raidx), 12);
        }
        corr_rhs_.forward(std::isnan(cr) ? a101::NaN : cr);

        if (idx + 1 < 62uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t clidx = corr_lhs_.size() - 1;
        const std::size_t cridx = corr_rhs_.size() - 1;
        const double rank_l = a101::ts_rank(corr_lhs_, clidx, std::min(corr_lhs_.size(), 252uz));
        const double rank_r = a101::ts_rank(corr_rhs_, cridx, std::min(corr_rhs_.size(), 252uz));

        if (std::isnan(rank_l) || std::isnan(rank_r)) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward(rank_l < rank_r ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 62; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> corr_lhs_;
    Line<double> adv50_;
    Line<double> rank_low_;
    Line<double> rank_adv50_;
    Line<double> corr_rhs_;
};

// ============================================================================
// Alpha#077: min(rank(decay_linear(((H+L)/2+H)-(vwap+H), 20)),
//               rank(decay_linear(correlation((H+L)/2, adv40, 3), 6)))
// Simplifies: inner_lhs = (H+L)/2 - vwap
// Inputs: H,L,C,V   Warmup: 45
// ============================================================================
class Alpha101_077 : public Indicator<Alpha101_077> {
public:
    Alpha101_077(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            hl2_minus_vwap_.data().reserve(close_.size());
            dl_lhs_.data().reserve(close_.size());
            hl2_.data().reserve(close_.size());
            adv40_.data().reserve(close_.size());
            corr_hl2_adv40_.data().reserve(close_.size());
            dl_rhs_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double hl2 = (h + l) / 2.0;
        const double vw = a101::vwap(high_, low_, close_, idx);

        hl2_minus_vwap_.forward(hl2 - vw);
        const std::size_t hmidx = hl2_minus_vwap_.size() - 1;

        const double dl_l = a101::decay_linear(hl2_minus_vwap_, hmidx, 20);
        dl_lhs_.forward(std::isnan(dl_l) ? a101::NaN : dl_l);

        hl2_.forward(hl2);
        const std::size_t hlidx = hl2_.size() - 1;

        const double av40 = a101::adv(volume_, idx, 40);
        adv40_.forward(std::isnan(av40) ? a101::NaN : av40);
        const std::size_t aidx = adv40_.size() - 1;

        double cr = a101::NaN;
        if (hlidx + 1 >= 3 && aidx + 1 >= 3 && !std::isnan(adv40_.data()[aidx])) {
            cr = a101::correlation(hl2_, adv40_, std::min(hlidx, aidx), 3);
        }
        corr_hl2_adv40_.forward(std::isnan(cr) ? a101::NaN : cr);
        const std::size_t cidx = corr_hl2_adv40_.size() - 1;

        const double dl_r = a101::decay_linear(corr_hl2_adv40_, cidx, 6);
        dl_rhs_.forward(std::isnan(dl_r) ? a101::NaN : dl_r);

        if (idx + 1 < 45uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t dlidx = dl_lhs_.size() - 1;
        const std::size_t dridx = dl_rhs_.size() - 1;
        const double rank_l = a101::ts_rank(dl_lhs_, dlidx, std::min(dl_lhs_.size(), 252uz));
        const double rank_r = a101::ts_rank(dl_rhs_, dridx, std::min(dl_rhs_.size(), 252uz));

        if (std::isnan(rank_l) || std::isnan(rank_r)) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward(std::min(rank_l, rank_r));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 45; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> hl2_minus_vwap_;
    Line<double> dl_lhs_;
    Line<double> hl2_;
    Line<double> adv40_;
    Line<double> corr_hl2_adv40_;
    Line<double> dl_rhs_;
};

// ============================================================================
// Alpha#078: (rank(correlation(sum(low*0.352233+vwap*0.647767, 20),
//             sum(adv40, 20), 7)) ^ rank(correlation(rank(vwap), rank(volume), 6)))
// Inputs: H,L,C,V   Warmup: 65
// ============================================================================
class Alpha101_078 : public Indicator<Alpha101_078> {
public:
    Alpha101_078(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            weighted_.data().reserve(close_.size());
            sum_w20_.data().reserve(close_.size());
            adv40_.data().reserve(close_.size());
            sum_adv40_20_.data().reserve(close_.size());
            corr_lhs_.data().reserve(close_.size());
            vwap_.data().reserve(close_.size());
            rank_vwap_.data().reserve(close_.size());
            rank_vol_.data().reserve(close_.size());
            corr_rhs_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double vw = a101::vwap(high_, low_, close_, idx);
        weighted_.forward(low_.data()[idx] * 0.352233 + vw * (1.0 - 0.352233));
        const std::size_t widx = weighted_.size() - 1;

        const double sw = a101::sum(weighted_, widx, 20);
        sum_w20_.forward(std::isnan(sw) ? a101::NaN : sw);

        const double av40 = a101::adv(volume_, idx, 40);
        adv40_.forward(std::isnan(av40) ? a101::NaN : av40);
        const std::size_t aidx = adv40_.size() - 1;

        const double sa = a101::sum(adv40_, aidx, 20);
        sum_adv40_20_.forward(std::isnan(sa) ? a101::NaN : sa);

        const std::size_t swidx = sum_w20_.size() - 1;
        const std::size_t saidx = sum_adv40_20_.size() - 1;
        double cl = a101::NaN;
        if (swidx + 1 >= 7 && saidx + 1 >= 7 &&
            !std::isnan(sum_w20_.data()[swidx]) && !std::isnan(sum_adv40_20_.data()[saidx])) {
            cl = a101::correlation(sum_w20_, sum_adv40_20_, std::min(swidx, saidx), 7);
        }
        corr_lhs_.forward(std::isnan(cl) ? a101::NaN : cl);

        vwap_.forward(vw);
        const std::size_t vidx = vwap_.size() - 1;
        const double rvw = a101::ts_rank(vwap_, vidx, std::min(vwap_.size(), 252uz));
        rank_vwap_.forward(rvw);

        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rank_vol_.forward(rv);

        const std::size_t rvwidx = rank_vwap_.size() - 1;
        const std::size_t rvidx = rank_vol_.size() - 1;
        double cr = a101::NaN;
        if (rvwidx + 1 >= 6 && rvidx + 1 >= 6) {
            cr = a101::correlation(rank_vwap_, rank_vol_, std::min(rvwidx, rvidx), 6);
        }
        corr_rhs_.forward(std::isnan(cr) ? a101::NaN : cr);

        if (idx + 1 < 65uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t clidx = corr_lhs_.size() - 1;
        const std::size_t cridx = corr_rhs_.size() - 1;
        const double rank_l = a101::ts_rank(corr_lhs_, clidx, std::min(corr_lhs_.size(), 252uz));
        const double rank_r = a101::ts_rank(corr_rhs_, cridx, std::min(corr_rhs_.size(), 252uz));

        if (std::isnan(rank_l) || std::isnan(rank_r)) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward(std::pow(rank_l, rank_r));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 65; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> weighted_;
    Line<double> sum_w20_;
    Line<double> adv40_;
    Line<double> sum_adv40_20_;
    Line<double> corr_lhs_;
    Line<double> vwap_;
    Line<double> rank_vwap_;
    Line<double> rank_vol_;
    Line<double> corr_rhs_;
};

// ============================================================================
// Alpha#081: ((rank(Log(product(rank(correlation(vwap,sum(adv10,50),8))^4, 15)))
//            < rank(correlation(rank(vwap), rank(volume), 5))) * -1)
// Inputs: H,L,C,V   Warmup: 80
// ============================================================================
class Alpha101_081 : public Indicator<Alpha101_081> {
public:
    Alpha101_081(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
            adv10_.data().reserve(close_.size());
            sum_adv10_50_.data().reserve(close_.size());
            corr_vs_.data().reserve(close_.size());
            rank_corr_pow4_.data().reserve(close_.size());
            log_prod_.data().reserve(close_.size());
            rank_vwap_.data().reserve(close_.size());
            rank_vol_.data().reserve(close_.size());
            corr_rhs_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);
        const std::size_t vidx = vwap_.size() - 1;

        const double av10 = a101::adv(volume_, idx, 10);
        adv10_.forward(std::isnan(av10) ? a101::NaN : av10);
        const std::size_t aidx = adv10_.size() - 1;

        const double s50 = a101::sum(adv10_, aidx, 50);
        sum_adv10_50_.forward(std::isnan(s50) ? a101::NaN : s50);
        const std::size_t sidx = sum_adv10_50_.size() - 1;

        double cv = a101::NaN;
        if (vidx + 1 >= 8 && sidx + 1 >= 8 && !std::isnan(sum_adv10_50_.data()[sidx])) {
            cv = a101::correlation(vwap_, sum_adv10_50_, std::min(vidx, sidx), 8);
        }
        corr_vs_.forward(std::isnan(cv) ? a101::NaN : cv);
        const std::size_t cvidx = corr_vs_.size() - 1;

        // rank(correlation)^4
        const double rc = std::isnan(cv)
            ? a101::NaN
            : a101::ts_rank(corr_vs_, cvidx, std::min(corr_vs_.size(), 252uz));
        const double rcp4 = std::isnan(rc) ? a101::NaN : std::pow(rc, 4.0);
        rank_corr_pow4_.forward(rcp4);
        const std::size_t rpidx = rank_corr_pow4_.size() - 1;

        // product(rank_corr_pow4, 15) then log
        const double pr = a101::product(rank_corr_pow4_, rpidx, 15);
        const double lp = (std::isnan(pr) || pr <= 0.0) ? a101::NaN : std::log(pr);
        log_prod_.forward(lp);

        // rank(vwap) and rank(volume) for RHS
        const double rvw = a101::ts_rank(vwap_, vidx, std::min(vwap_.size(), 252uz));
        rank_vwap_.forward(rvw);

        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rank_vol_.forward(rv);

        const std::size_t rvwidx = rank_vwap_.size() - 1;
        const std::size_t rvidx = rank_vol_.size() - 1;
        double cr = a101::NaN;
        if (rvwidx + 1 >= 5 && rvidx + 1 >= 5) {
            cr = a101::correlation(rank_vwap_, rank_vol_, std::min(rvwidx, rvidx), 5);
        }
        corr_rhs_.forward(std::isnan(cr) ? a101::NaN : cr);

        if (idx + 1 < 80uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t lpidx = log_prod_.size() - 1;
        const std::size_t cridx = corr_rhs_.size() - 1;
        const double rank_l = a101::ts_rank(log_prod_, lpidx, std::min(log_prod_.size(), 252uz));
        const double rank_r = a101::ts_rank(corr_rhs_, cridx, std::min(corr_rhs_.size(), 252uz));

        if (std::isnan(rank_l) || std::isnan(rank_r)) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward((rank_l < rank_r ? 1.0 : 0.0) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 80; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> adv10_;
    Line<double> sum_adv10_50_;
    Line<double> corr_vs_;
    Line<double> rank_corr_pow4_;
    Line<double> log_prod_;
    Line<double> rank_vwap_;
    Line<double> rank_vol_;
    Line<double> corr_rhs_;
};

// ============================================================================
// Alpha#083: ((rank(delay(((H-L)/(sum(C,5)/5)), 2)) * rank(rank(volume)))
//            / (((H-L)/(sum(C,5)/5)) / (vwap - close)))
// inner = (H-L) / sma(C,5);  output = rank(delay(inner,2)) * rank(rank(vol)) / (inner / (vwap-C))
// Inputs: H,L,C,V   Warmup: 7
// ============================================================================
class Alpha101_083 : public Indicator<Alpha101_083> {
public:
    Alpha101_083(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inner_.data().reserve(close_.size());
            delayed_inner_.data().reserve(close_.size());
            rank_vol_inner_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // inner = (H-L) / sma(C,5)
        const double sma5 = a101::sma(close_, idx, 5);
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double inn = (std::isnan(sma5) || sma5 == 0.0)
            ? a101::NaN : (h - l) / sma5;
        inner_.forward(inn);
        const std::size_t iidx = inner_.size() - 1;

        // delay(inner, 2)
        const double di = a101::delay(inner_, iidx, 2);
        delayed_inner_.forward(std::isnan(di) ? a101::NaN : di);

        // rank(volume) = ts_rank(volume, min(idx+1, 252))
        const double rv = a101::ts_rank(volume_, idx, std::min(idx + 1, 252uz));
        rank_vol_inner_.forward(rv);

        if (idx + 1 < 7uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t didx = delayed_inner_.size() - 1;
        const double rank_delayed = a101::ts_rank(delayed_inner_, didx, std::min(delayed_inner_.size(), 252uz));

        // rank(rank(volume)) = ts_rank(rank_vol_inner_, ...)
        const std::size_t rvidx = rank_vol_inner_.size() - 1;
        const double rank_rank_vol = a101::ts_rank(rank_vol_inner_, rvidx, std::min(rank_vol_inner_.size(), 252uz));

        const double c = close_.data()[idx];
        const double vw = a101::vwap(high_, low_, close_, idx);
        const double vwap_diff = vw - c;

        if (std::isnan(inn) || std::isnan(rank_delayed) || std::isnan(rank_rank_vol) ||
            vwap_diff == 0.0 || inn == 0.0) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const double numer = rank_delayed * rank_rank_vol;
        const double denom = inn / vwap_diff;
        if (denom == 0.0) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward(numer / denom);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 7; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> inner_;
    Line<double> delayed_inner_;
    Line<double> rank_vol_inner_;
};

// ============================================================================
// Alpha#085: (rank(correlation(high*0.876703+close*0.123297, adv30, 10))
//            ^ rank(correlation(Ts_Rank((H+L)/2, 4), Ts_Rank(volume, 10), 7)))
// Inputs: H,L,C,V   Warmup: 39
// ============================================================================
class Alpha101_085 : public Indicator<Alpha101_085> {
public:
    Alpha101_085(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            weighted_.data().reserve(close_.size());
            adv30_.data().reserve(close_.size());
            corr_lhs_.data().reserve(close_.size());
            hl2_.data().reserve(close_.size());
            tsr_hl2_.data().reserve(close_.size());
            tsr_vol_.data().reserve(close_.size());
            corr_rhs_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // weighted = high*0.876703 + close*(1-0.876703)
        weighted_.forward(high_.data()[idx] * 0.876703 + close_.data()[idx] * (1.0 - 0.876703));
        const std::size_t widx = weighted_.size() - 1;

        // adv30
        const double av30 = a101::adv(volume_, idx, 30);
        adv30_.forward(std::isnan(av30) ? a101::NaN : av30);
        const std::size_t aidx = adv30_.size() - 1;

        // LHS: correlation(weighted, adv30, 10)
        double cl = a101::NaN;
        if (widx + 1 >= 10 && aidx + 1 >= 10 && !std::isnan(adv30_.data()[aidx])) {
            cl = a101::correlation(weighted_, adv30_, std::min(widx, aidx), 10);
        }
        corr_lhs_.forward(std::isnan(cl) ? a101::NaN : cl);

        // hl2 scratch
        hl2_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);
        const std::size_t hlidx = hl2_.size() - 1;

        // Ts_Rank(hl2, 4)
        const double trh = a101::ts_rank(hl2_, hlidx, 4);
        tsr_hl2_.forward(std::isnan(trh) ? a101::NaN : trh);

        // Ts_Rank(volume, 10)
        const double trv = a101::ts_rank(volume_, idx, 10);
        tsr_vol_.forward(std::isnan(trv) ? a101::NaN : trv);

        // RHS: correlation(tsr_hl2, tsr_vol, 7)
        const std::size_t thidx = tsr_hl2_.size() - 1;
        const std::size_t tvidx = tsr_vol_.size() - 1;
        double cr = a101::NaN;
        if (thidx + 1 >= 7 && tvidx + 1 >= 7) {
            cr = a101::correlation(tsr_hl2_, tsr_vol_, std::min(thidx, tvidx), 7);
        }
        corr_rhs_.forward(std::isnan(cr) ? a101::NaN : cr);

        if (idx + 1 < 39uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const std::size_t clidx = corr_lhs_.size() - 1;
        const std::size_t cridx = corr_rhs_.size() - 1;
        const double rank_l = a101::ts_rank(corr_lhs_, clidx, std::min(corr_lhs_.size(), 252uz));
        const double rank_r = a101::ts_rank(corr_rhs_, cridx, std::min(corr_rhs_.size(), 252uz));

        if (std::isnan(rank_l) || std::isnan(rank_r)) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward(std::pow(rank_l, rank_r));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 39; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> weighted_;
    Line<double> adv30_;
    Line<double> corr_lhs_;
    Line<double> hl2_;
    Line<double> tsr_hl2_;
    Line<double> tsr_vol_;
    Line<double> corr_rhs_;
};

// ============================================================================
// Alpha#086: ((Ts_Rank(correlation(close, sum(adv20, 15), 6), 20) <
//              rank(((open + close) - (vwap + open)))) * -1)
// Simplifies RHS inner: (O+C)-(vwap+O) = C - vwap
// Inputs: O,H,L,C,V   Warmup: 58
// ============================================================================
class Alpha101_086 : public Indicator<Alpha101_086> {
public:
    Alpha101_086(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            adv20_.data().reserve(close_.size());
            sum_adv20_15_.data().reserve(close_.size());
            corr_.data().reserve(close_.size());
            c_minus_vwap_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        // adv20
        const double av20 = a101::adv(volume_, idx, 20);
        adv20_.forward(std::isnan(av20) ? a101::NaN : av20);
        const std::size_t aidx = adv20_.size() - 1;

        // sum(adv20, 15)
        const double s15 = a101::sum(adv20_, aidx, 15);
        sum_adv20_15_.forward(std::isnan(s15) ? a101::NaN : s15);
        const std::size_t sidx = sum_adv20_15_.size() - 1;

        // correlation(close, sum_adv20_15, 6)
        double cr = a101::NaN;
        if (sidx + 1 >= 6 && !std::isnan(sum_adv20_15_.data()[sidx])) {
            cr = a101::correlation(close_, sum_adv20_15_, std::min(idx, sidx), 6);
        }
        corr_.forward(std::isnan(cr) ? a101::NaN : cr);

        // C - vwap
        const double vw = a101::vwap(high_, low_, close_, idx);
        c_minus_vwap_.forward(close_.data()[idx] - vw);

        if (idx + 1 < 58uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        // LHS: Ts_Rank(corr, 20)
        const std::size_t cidx = corr_.size() - 1;
        const double tsr_lhs = a101::ts_rank(corr_, cidx, 20);

        // RHS: rank(C - vwap) = ts_rank(c_minus_vwap, min(size, 252))
        const std::size_t cmidx = c_minus_vwap_.size() - 1;
        const double rank_rhs = a101::ts_rank(c_minus_vwap_, cmidx, std::min(c_minus_vwap_.size(), 252uz));

        if (std::isnan(tsr_lhs) || std::isnan(rank_rhs)) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }
        line_.forward((tsr_lhs < rank_rhs ? 1.0 : 0.0) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 58; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> adv20_;
    Line<double> sum_adv20_15_;
    Line<double> corr_;
    Line<double> c_minus_vwap_;
};

// ============================================================================
// Alpha#088: min(rank(decay_linear(((rank(open)+rank(low))-(rank(high)+rank(close))),8)),
//     Ts_Rank(decay_linear(correlation(Ts_Rank(close,8),Ts_Rank(adv60,21),8),7),3))
// Inputs: O,H,L,C,V   Warmup: 95
// ============================================================================
class Alpha101_088 : public Indicator<Alpha101_088> {
public:
    Alpha101_088(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            expr_.data().reserve(n);   dl_expr_.data().reserve(n);
            adv60_.data().reserve(n);  tsr_close_.data().reserve(n);
            tsr_adv60_.data().reserve(n);
            corr_.data().reserve(n);   dl_corr_.data().reserve(n);
        }
        const auto idx = close_.index();
        const auto rp = std::min(idx + 1, 252uz);

        expr_.forward(
            (a101::ts_rank(open_, idx, rp) + a101::ts_rank(low_, idx, rp)) -
            (a101::ts_rank(high_, idx, rp) + a101::ts_rank(close_, idx, rp)));
        dl_expr_.forward((idx + 1 >= 8uz)
            ? a101::decay_linear(expr_, idx, 8) : a101::NaN);

        tsr_close_.forward((idx + 1 >= 8uz)
            ? a101::ts_rank(close_, idx, 8) : a101::NaN);
        adv60_.forward(a101::adv(volume_, idx, 60));
        tsr_adv60_.forward((idx + 1 >= 80uz)
            ? a101::ts_rank(adv60_, idx, 21) : a101::NaN);
        corr_.forward((idx + 1 >= 87uz)
            ? a101::correlation(tsr_close_, tsr_adv60_, idx, 8) : a101::NaN);
        dl_corr_.forward((idx + 1 >= 93uz)
            ? a101::decay_linear(corr_, idx, 7) : a101::NaN);

        if (idx + 1 < 95uz) [[unlikely]] { line_.forward(a101::NaN); return; }

        const double lhs = a101::ts_rank(dl_expr_, idx,
                                          std::min(dl_expr_.size(), 252uz));
        const double rhs = a101::ts_rank(dl_corr_, idx, 3);
        line_.forward(std::min(lhs, rhs));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 95; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> expr_;     Line<double> dl_expr_;
    Line<double> adv60_;    Line<double> tsr_close_;
    Line<double> tsr_adv60_;
    Line<double> corr_;     Line<double> dl_corr_;
};

// ============================================================================
// Alpha#092: min(Ts_Rank(decay_linear(((((high+low)/2)+close)<(low+open)),15),19),
//     Ts_Rank(decay_linear(correlation(rank(low),rank(adv30),8),7),7))
// Inputs: O,H,L,C,V   Warmup: 50
// ============================================================================
class Alpha101_092 : public Indicator<Alpha101_092> {
public:
    Alpha101_092(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            bool_.data().reserve(n);    dl_bool_.data().reserve(n);
            adv30_.data().reserve(n);
            rank_low_.data().reserve(n); rank_adv30_.data().reserve(n);
            corr_.data().reserve(n);    dl_corr_.data().reserve(n);
        }
        const auto idx = close_.index();

        bool_.forward(
            (((high_.data()[idx] + low_.data()[idx]) / 2.0 + close_.data()[idx])
             < (low_.data()[idx] + open_.data()[idx])) ? 1.0 : 0.0);
        dl_bool_.forward((idx + 1 >= 15uz)
            ? a101::decay_linear(bool_, idx, 15) : a101::NaN);

        const auto rp = std::min(idx + 1, 252uz);
        rank_low_.forward(a101::ts_rank(low_, idx, rp));
        adv30_.forward(a101::adv(volume_, idx, 30));
        rank_adv30_.forward(a101::ts_rank(adv30_, idx, rp));
        corr_.forward((idx + 1 >= 8uz)
            ? a101::correlation(rank_low_, rank_adv30_, idx, 8) : a101::NaN);
        dl_corr_.forward((idx + 1 >= 14uz)
            ? a101::decay_linear(corr_, idx, 7) : a101::NaN);

        if (idx + 1 < 50uz) [[unlikely]] { line_.forward(a101::NaN); return; }

        const double lhs = a101::ts_rank(dl_bool_, idx, 19);
        const double rhs = a101::ts_rank(dl_corr_, idx, 7);
        line_.forward(std::min(lhs, rhs));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 50; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> bool_;     Line<double> dl_bool_;
    Line<double> adv30_;
    Line<double> rank_low_; Line<double> rank_adv30_;
    Line<double> corr_;     Line<double> dl_corr_;
};

// ============================================================================
// Alpha#094: ((rank((vwap-ts_min(vwap,12)))^Ts_Rank(correlation(Ts_Rank(vwap,20),
//     Ts_Rank(adv60,4),18),3)) * -1)
// Inputs: H,L,C,V   Warmup: 82
// ============================================================================
class Alpha101_094 : public Indicator<Alpha101_094> {
public:
    Alpha101_094(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);    diff_.data().reserve(n);
            adv60_.data().reserve(n);
            tsr_vwap_.data().reserve(n); tsr_adv60_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double v = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(v);

        diff_.forward((idx + 1 >= 12uz)
            ? v - a101::ts_min(vwap_, idx, 12) : a101::NaN);

        tsr_vwap_.forward((idx + 1 >= 20uz)
            ? a101::ts_rank(vwap_, idx, 20) : a101::NaN);
        adv60_.forward(a101::adv(volume_, idx, 60));
        tsr_adv60_.forward((idx + 1 >= 63uz)
            ? a101::ts_rank(adv60_, idx, 4) : a101::NaN);
        corr_.forward((idx + 1 >= 80uz)
            ? a101::correlation(tsr_vwap_, tsr_adv60_, idx, 18) : a101::NaN);

        if (idx + 1 < 82uz) [[unlikely]] { line_.forward(a101::NaN); return; }

        const double base = a101::ts_rank(diff_, idx,
                                           std::min(diff_.size(), 252uz));
        const double exp = a101::ts_rank(corr_, idx, 3);
        line_.forward(std::pow(base, exp) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 82; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;     Line<double> diff_;
    Line<double> adv60_;
    Line<double> tsr_vwap_; Line<double> tsr_adv60_;
    Line<double> corr_;
};

// ============================================================================
// Alpha#095: (rank((open-ts_min(open,12))) < Ts_Rank((rank(correlation(sum(((high+low)/2),
//     19),sum(adv40,19),13))^5),12))
// Inputs: O,H,L,V   Warmup: 81
// ============================================================================
class Alpha101_095 : public Indicator<Alpha101_095> {
public:
    Alpha101_095(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& volume)
        : open_(open), high_(high), low_(low), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = open_.size();
            reserve_output(n);
            diff_.data().reserve(n);
            hl2_.data().reserve(n);     sum_hl2_.data().reserve(n);
            adv40_.data().reserve(n);   sum_adv40_.data().reserve(n);
            corr_.data().reserve(n);    rank_pow5_.data().reserve(n);
        }
        const auto idx = open_.index();

        diff_.forward((idx + 1 >= 12uz)
            ? open_.data()[idx] - a101::ts_min(open_, idx, 12) : a101::NaN);

        hl2_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);
        sum_hl2_.forward((idx + 1 >= 19uz)
            ? a101::sum(hl2_, idx, 19) : a101::NaN);
        adv40_.forward(a101::adv(volume_, idx, 40));
        sum_adv40_.forward((idx + 1 >= 58uz)
            ? a101::sum(adv40_, idx, 19) : a101::NaN);
        corr_.forward((idx + 1 >= 70uz)
            ? a101::correlation(sum_hl2_, sum_adv40_, idx, 13) : a101::NaN);

        const auto rp = std::min(idx + 1, 252uz);
        const double rc = a101::ts_rank(corr_, idx, rp);
        rank_pow5_.forward(std::pow(rc, 5.0));

        if (idx + 1 < 81uz) [[unlikely]] { line_.forward(a101::NaN); return; }

        const double lhs = a101::ts_rank(diff_, idx,
                                          std::min(diff_.size(), 252uz));
        const double rhs = a101::ts_rank(rank_pow5_, idx, 12);
        line_.forward(lhs < rhs ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 81; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& volume_;
    Line<double> diff_;
    Line<double> hl2_;      Line<double> sum_hl2_;
    Line<double> adv40_;    Line<double> sum_adv40_;
    Line<double> corr_;     Line<double> rank_pow5_;
};

// ============================================================================
// Alpha#096: (max(Ts_Rank(decay_linear(correlation(rank(vwap),rank(volume),4),4),8),
//     Ts_Rank(decay_linear(Ts_ArgMax(correlation(Ts_Rank(close,7),
//     Ts_Rank(adv60,4),4),13),14),13)) * -1)
// Inputs: H,L,C,V   Warmup: 103
// ============================================================================
class Alpha101_096 : public Indicator<Alpha101_096> {
public:
    Alpha101_096(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            rank_vwap_.data().reserve(n); rank_vol_.data().reserve(n);
            corr_a_.data().reserve(n);    dl_a_.data().reserve(n);
            tsr_close_.data().reserve(n);
            adv60_.data().reserve(n);     tsr_adv60_.data().reserve(n);
            corr_b_.data().reserve(n);    argmax_.data().reserve(n);
            dl_b_.data().reserve(n);
        }
        const auto idx = close_.index();
        const auto rp = std::min(idx + 1, 252uz);

        // Term A
        vwap_.forward(a101::vwap(high_, low_, close_, idx));
        rank_vwap_.forward(a101::ts_rank(vwap_, idx, rp));
        rank_vol_.forward(a101::ts_rank(volume_, idx, rp));
        corr_a_.forward((idx + 1 >= 4uz)
            ? a101::correlation(rank_vwap_, rank_vol_, idx, 4) : a101::NaN);
        dl_a_.forward((idx + 1 >= 7uz)
            ? a101::decay_linear(corr_a_, idx, 4) : a101::NaN);

        // Term B
        tsr_close_.forward((idx + 1 >= 7uz)
            ? a101::ts_rank(close_, idx, 7) : a101::NaN);
        adv60_.forward(a101::adv(volume_, idx, 60));
        tsr_adv60_.forward((idx + 1 >= 63uz)
            ? a101::ts_rank(adv60_, idx, 4) : a101::NaN);
        corr_b_.forward((idx + 1 >= 66uz)
            ? a101::correlation(tsr_close_, tsr_adv60_, idx, 4) : a101::NaN);
        argmax_.forward((idx + 1 >= 78uz)
            ? a101::ts_argmax(corr_b_, idx, 13) : a101::NaN);
        dl_b_.forward((idx + 1 >= 91uz)
            ? a101::decay_linear(argmax_, idx, 14) : a101::NaN);

        if (idx + 1 < 103uz) [[unlikely]] { line_.forward(a101::NaN); return; }

        const double ta = a101::ts_rank(dl_a_, idx, 8);
        const double tb = a101::ts_rank(dl_b_, idx, 13);
        line_.forward(std::max(ta, tb) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 103; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> rank_vwap_; Line<double> rank_vol_;
    Line<double> corr_a_;    Line<double> dl_a_;
    Line<double> tsr_close_;
    Line<double> adv60_;     Line<double> tsr_adv60_;
    Line<double> corr_b_;    Line<double> argmax_;
    Line<double> dl_b_;
};

// ============================================================================
// Alpha#098: (rank(decay_linear(correlation(vwap,sum(adv5,26),5),7)) -
//     rank(decay_linear(Ts_Rank(Ts_ArgMin(correlation(rank(open),rank(adv15),21),9),7),8)))
// Inputs: O,H,L,C,V   Warmup: 56
// ============================================================================
class Alpha101_098 : public Indicator<Alpha101_098> {
public:
    Alpha101_098(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            adv5_.data().reserve(n);      sum_adv5_.data().reserve(n);
            corr_a_.data().reserve(n);    dl_a_.data().reserve(n);
            rank_open_.data().reserve(n);
            adv15_.data().reserve(n);     rank_adv15_.data().reserve(n);
            corr_b_.data().reserve(n);    argmin_.data().reserve(n);
            tsr_argmin_.data().reserve(n); dl_b_.data().reserve(n);
        }
        const auto idx = close_.index();
        const auto rp = std::min(idx + 1, 252uz);

        // Term A
        vwap_.forward(a101::vwap(high_, low_, close_, idx));
        adv5_.forward(a101::adv(volume_, idx, 5));
        sum_adv5_.forward((idx + 1 >= 30uz)
            ? a101::sum(adv5_, idx, 26) : a101::NaN);
        corr_a_.forward((idx + 1 >= 34uz)
            ? a101::correlation(vwap_, sum_adv5_, idx, 5) : a101::NaN);
        dl_a_.forward((idx + 1 >= 40uz)
            ? a101::decay_linear(corr_a_, idx, 7) : a101::NaN);

        // Term B
        rank_open_.forward(a101::ts_rank(open_, idx, rp));
        adv15_.forward(a101::adv(volume_, idx, 15));
        rank_adv15_.forward(a101::ts_rank(adv15_, idx, rp));
        corr_b_.forward((idx + 1 >= 21uz)
            ? a101::correlation(rank_open_, rank_adv15_, idx, 21) : a101::NaN);
        argmin_.forward((idx + 1 >= 29uz)
            ? a101::ts_argmin(corr_b_, idx, 9) : a101::NaN);
        tsr_argmin_.forward((idx + 1 >= 35uz)
            ? a101::ts_rank(argmin_, idx, 7) : a101::NaN);
        dl_b_.forward((idx + 1 >= 42uz)
            ? a101::decay_linear(tsr_argmin_, idx, 8) : a101::NaN);

        if (idx + 1 < 56uz) [[unlikely]] { line_.forward(a101::NaN); return; }

        const double ra = a101::ts_rank(dl_a_, idx,
                                         std::min(dl_a_.size(), 252uz));
        const double rb = a101::ts_rank(dl_b_, idx,
                                         std::min(dl_b_.size(), 252uz));
        line_.forward(ra - rb);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 56; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> adv5_;       Line<double> sum_adv5_;
    Line<double> corr_a_;     Line<double> dl_a_;
    Line<double> rank_open_;
    Line<double> adv15_;      Line<double> rank_adv15_;
    Line<double> corr_b_;     Line<double> argmin_;
    Line<double> tsr_argmin_; Line<double> dl_b_;
};

// ============================================================================
// Alpha#099: ((rank(correlation(sum(((high+low)/2),20),sum(adv60,20),9)) <
//     rank(correlation(low,volume,6))) * -1)
// Inputs: H,L,V   Warmup: 87
// ============================================================================
class Alpha101_099 : public Indicator<Alpha101_099> {
public:
    Alpha101_099(const Line<double>& high, const Line<double>& low,
                 const Line<double>& volume)
        : high_(high), low_(low), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            hl2_.data().reserve(n);     sum_hl2_.data().reserve(n);
            adv60_.data().reserve(n);   sum_adv60_.data().reserve(n);
            corr1_.data().reserve(n);   corr2_.data().reserve(n);
        }
        const auto idx = high_.index();

        hl2_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);
        sum_hl2_.forward((idx + 1 >= 20uz)
            ? a101::sum(hl2_, idx, 20) : a101::NaN);
        adv60_.forward(a101::adv(volume_, idx, 60));
        sum_adv60_.forward((idx + 1 >= 79uz)
            ? a101::sum(adv60_, idx, 20) : a101::NaN);
        corr1_.forward((idx + 1 >= 87uz)
            ? a101::correlation(sum_hl2_, sum_adv60_, idx, 9) : a101::NaN);

        corr2_.forward((idx + 1 >= 6uz)
            ? a101::correlation(low_, volume_, idx, 6) : a101::NaN);

        if (idx + 1 < 87uz) [[unlikely]] { line_.forward(a101::NaN); return; }

        const auto rp = std::min(idx + 1, 252uz);
        const double r1 = a101::ts_rank(corr1_, idx, rp);
        const double r2 = a101::ts_rank(corr2_, idx, rp);
        line_.forward((r1 < r2) ? -1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 87; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& volume_;
    Line<double> hl2_;      Line<double> sum_hl2_;
    Line<double> adv60_;    Line<double> sum_adv60_;
    Line<double> corr1_;    Line<double> corr2_;
};

// ============================================================================
// Batch C: Cross-sectional with IndNeutralize (18 factors)
// IndNeutralize(expr, group) → passthrough (returns expr unchanged)
// rank() → ts_rank(scratch, idx, min(size, 252))
// Fractional periods are truncated to integer.
// ============================================================================

// ============================================================================
// Alpha#048: (IndNeutralize(((corr(delta(close,1),delta(delay(close,1),1),250)
//            *delta(close,1))/close), subindustry)
//            / sum(((delta(close,1)/delay(close,1))^2), 250))
// IndNeutralize → passthrough. Inputs: C   Warmup: 252
// ============================================================================
class Alpha101_048 : public Indicator<Alpha101_048> {
public:
    explicit Alpha101_048(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            d1_.data().reserve(close_.size());
            d1_lag_.data().reserve(close_.size());
            sq_.data().reserve(close_.size());
        }
        const auto idx = close_.index();

        const double dc = a101::delta(close_, idx, 1);
        d1_.forward(std::isnan(dc) ? a101::NaN : dc);

        // delta(delay(close,1), 1) = close[t-1] - close[t-2]
        const double dc_lag = (idx >= 2uz)
            ? (close_.data()[idx - 1] - close_.data()[idx - 2]) : a101::NaN;
        d1_lag_.forward(dc_lag);

        // (delta(close,1)/delay(close,1))^2
        const double delayed = (idx >= 1uz) ? close_.data()[idx - 1] : a101::NaN;
        const double ratio = (!std::isnan(dc) && !std::isnan(delayed) && delayed != 0.0)
            ? (dc / delayed) : a101::NaN;
        sq_.forward(std::isnan(ratio) ? a101::NaN : ratio * ratio);

        if (idx + 1 < 252uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto didx = d1_.size() - 1;
        const double corr = a101::correlation(d1_, d1_lag_, didx, 250);
        const double c = close_.data()[idx];
        const double numer = (c != 0.0) ? (corr * dc / c) : 0.0;

        const auto sqidx = sq_.size() - 1;
        const double denom = a101::sum(sq_, sqidx, 250);
        line_.forward((denom != 0.0) ? (numer / denom) : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 252; }

private:
    const Line<double>& close_;
    Line<double> d1_;
    Line<double> d1_lag_;
    Line<double> sq_;
};

// ============================================================================
// Alpha#058: (-1 * Ts_Rank(decay_linear(correlation(IndNeutralize(vwap,sector),
//            volume, 3), 7), 5))
// IndNeutralize(vwap,sector) → vwap.  Inputs: H,L,C,V   Warmup: 15
// ============================================================================
class Alpha101_058 : public Indicator<Alpha101_058> {
public:
    Alpha101_058(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            corr_.data().reserve(n);
            dl_.data().reserve(n);
        }
        const auto idx = close_.index();

        vwap_.forward(a101::vwap(high_, low_, close_, idx));

        const auto vidx = vwap_.size() - 1;
        corr_.forward((vidx + 1 >= 3uz)
            ? a101::correlation(vwap_, volume_, std::min(vidx, idx), 3)
            : a101::NaN);

        const auto cidx = corr_.size() - 1;
        dl_.forward((cidx + 1 >= 7uz)
            ? a101::decay_linear(corr_, cidx, 7) : a101::NaN);

        if (idx + 1 < 15uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto didx = dl_.size() - 1;
        line_.forward(-1.0 * a101::ts_rank(dl_, didx, 5));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 15; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> corr_;
    Line<double> dl_;
};

// ============================================================================
// Alpha#059: (-1 * Ts_Rank(decay_linear(correlation(IndNeutralize(
//            vwap*0.728317 + vwap*(1-0.728317), industry),  // simplifies to vwap
//            volume, 4), 16), 8))
// IndNeutralize → passthrough, inner = vwap.  Inputs: H,L,C,V   Warmup: 28
// ============================================================================
class Alpha101_059 : public Indicator<Alpha101_059> {
public:
    Alpha101_059(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            corr_.data().reserve(n);
            dl_.data().reserve(n);
        }
        const auto idx = close_.index();

        vwap_.forward(a101::vwap(high_, low_, close_, idx));

        const auto vidx = vwap_.size() - 1;
        corr_.forward((vidx + 1 >= 4uz)
            ? a101::correlation(vwap_, volume_, std::min(vidx, idx), 4)
            : a101::NaN);

        const auto cidx = corr_.size() - 1;
        dl_.forward((cidx + 1 >= 16uz)
            ? a101::decay_linear(corr_, cidx, 16) : a101::NaN);

        if (idx + 1 < 28uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto didx = dl_.size() - 1;
        line_.forward(-1.0 * a101::ts_rank(dl_, didx, 8));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 28; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> corr_;
    Line<double> dl_;
};

// ============================================================================
// Alpha#063: (rank(decay_linear(delta(IndNeutralize(close,industry),2),8))
//            - rank(decay_linear(correlation(vwap*0.318108+open*(1-0.318108),
//              sum(adv180,37), 13), 12))) * -1
// IndNeutralize(close,industry) → close.
// Inputs: O,H,L,C,V   Warmup: 283 (180 adv + 37 sum + 13 corr + 12 dl + 252 rank ~ 283)
// ============================================================================
class Alpha101_063 : public Indicator<Alpha101_063> {
public:
    Alpha101_063(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            delta_c_.data().reserve(n);
            dl1_.data().reserve(n);
            blend_.data().reserve(n);
            adv180_.data().reserve(n);
            sum_adv_.data().reserve(n);
            corr2_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();

        // delta(close, 2) — IndNeutralize(close) = close
        const double dc = a101::delta(close_, idx, 2);
        delta_c_.forward(std::isnan(dc) ? a101::NaN : dc);

        // decay_linear(delta_c, 8)
        const auto dcidx = delta_c_.size() - 1;
        dl1_.forward((dcidx + 1 >= 8uz)
            ? a101::decay_linear(delta_c_, dcidx, 8) : a101::NaN);

        // blend = vwap*0.318108 + open*(1-0.318108)
        const double vw = a101::vwap(high_, low_, close_, idx);
        blend_.forward(vw * 0.318108 + open_.data()[idx] * (1.0 - 0.318108));

        // adv180
        adv180_.forward(a101::adv(volume_, idx, 180));

        // sum(adv180, 37)
        const auto aidx = adv180_.size() - 1;
        sum_adv_.forward((aidx + 1 >= 37uz)
            ? a101::sum(adv180_, aidx, 37) : a101::NaN);

        // correlation(blend, sum_adv, 13)
        const auto bidx = blend_.size() - 1;
        const auto saidx = sum_adv_.size() - 1;
        const auto corr_len = std::min(bidx, saidx) + 1;
        corr2_.forward((corr_len >= 13uz)
            ? a101::correlation(blend_, sum_adv_, std::min(bidx, saidx), 13)
            : a101::NaN);

        // decay_linear(corr2, 12)
        const auto c2idx = corr2_.size() - 1;
        dl2_.forward((c2idx + 1 >= 12uz)
            ? a101::decay_linear(corr2_, c2idx, 12) : a101::NaN);

        if (idx + 1 < 283uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto d1idx = dl1_.size() - 1;
        const auto d2idx = dl2_.size() - 1;
        const auto rp = 252uz;
        const double r1 = a101::ts_rank(dl1_, d1idx, std::min(d1idx + 1, rp));
        const double r2 = a101::ts_rank(dl2_, d2idx, std::min(d2idx + 1, rp));
        line_.forward((r1 - r2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 283; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> delta_c_;
    Line<double> dl1_;
    Line<double> blend_;
    Line<double> adv180_;
    Line<double> sum_adv_;
    Line<double> corr2_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#067: (rank(high - ts_min(high, 2))^rank(correlation(
//            IndNeutralize(vwap,sector), IndNeutralize(adv20,subindustry),
//            6))) * -1
// IndNeutralize → passthrough.  Inputs: H,L,C,V   Warmup: 260
// ============================================================================
class Alpha101_067 : public Indicator<Alpha101_067> {
public:
    Alpha101_067(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            adv20_.data().reserve(n);
            diff_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();

        vwap_.forward(a101::vwap(high_, low_, close_, idx));

        adv20_.forward(a101::adv(volume_, idx, 20));

        // high - ts_min(high, 2)
        diff_.forward((idx + 1 >= 2uz)
            ? (high_.data()[idx] - a101::ts_min(high_, idx, 2)) : a101::NaN);

        // correlation(vwap, adv20, 6)
        const auto vidx = vwap_.size() - 1;
        const auto aidx = adv20_.size() - 1;
        const auto cl = std::min(vidx, aidx) + 1;
        corr_.forward((cl >= 6uz)
            ? a101::correlation(vwap_, adv20_, std::min(vidx, aidx), 6)
            : a101::NaN);

        if (idx + 1 < 260uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto didx = diff_.size() - 1;
        const auto cidx = corr_.size() - 1;
        const auto rp = 252uz;
        const double r1 = a101::ts_rank(diff_, didx, std::min(didx + 1, rp));
        const double r2 = a101::ts_rank(corr_, cidx, std::min(cidx + 1, rp));
        line_.forward(std::pow(r1, r2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 260; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> adv20_;
    Line<double> diff_;
    Line<double> corr_;
};

// ============================================================================
// Alpha#069: (rank(ts_max(delta(IndNeutralize(vwap,industry),2),4))
//            ^Ts_Rank(correlation(close*0.490655+vwap*(1-0.490655),adv20,4),9))
//            * -1
// IndNeutralize(vwap) → vwap.  Inputs: H,L,C,V   Warmup: 260
// ============================================================================
class Alpha101_069 : public Indicator<Alpha101_069> {
public:
    Alpha101_069(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            delta_vwap_.data().reserve(n);
            mx_.data().reserve(n);
            blend_.data().reserve(n);
            adv20_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();

        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);

        // delta(vwap, 2)
        const auto vidx = vwap_.size() - 1;
        delta_vwap_.forward((vidx >= 2uz)
            ? a101::delta(vwap_, vidx, 2) : a101::NaN);

        // ts_max(delta_vwap, 4)
        const auto dvidx = delta_vwap_.size() - 1;
        mx_.forward((dvidx + 1 >= 4uz)
            ? a101::ts_max(delta_vwap_, dvidx, 4) : a101::NaN);

        // blend = close*0.490655 + vwap*(1-0.490655)
        blend_.forward(close_.data()[idx] * 0.490655 + vw * (1.0 - 0.490655));

        adv20_.forward(a101::adv(volume_, idx, 20));

        // correlation(blend, adv20, 4)
        const auto bidx = blend_.size() - 1;
        const auto aidx = adv20_.size() - 1;
        const auto cl = std::min(bidx, aidx) + 1;
        corr_.forward((cl >= 4uz)
            ? a101::correlation(blend_, adv20_, std::min(bidx, aidx), 4)
            : a101::NaN);

        if (idx + 1 < 260uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto mxidx = mx_.size() - 1;
        const auto cidx = corr_.size() - 1;
        const double r1 = a101::ts_rank(mx_, mxidx, std::min(mxidx + 1, 252uz));
        const double tr2 = a101::ts_rank(corr_, cidx, 9);
        line_.forward(std::pow(r1, tr2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 260; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> delta_vwap_;
    Line<double> mx_;
    Line<double> blend_;
    Line<double> adv20_;
    Line<double> corr_;
};

// ============================================================================
// Alpha#070: (rank(delta(vwap,1))^Ts_Rank(correlation(IndNeutralize(close,
//            industry), adv50, 17), 17)) * -1
// IndNeutralize(close) → close.  Inputs: H,L,C,V   Warmup: 286
// ============================================================================
class Alpha101_070 : public Indicator<Alpha101_070> {
public:
    Alpha101_070(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            delta_vwap_.data().reserve(n);
            adv50_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();

        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);

        const auto vidx = vwap_.size() - 1;
        delta_vwap_.forward((vidx >= 1uz)
            ? a101::delta(vwap_, vidx, 1) : a101::NaN);

        adv50_.forward(a101::adv(volume_, idx, 50));

        // correlation(close, adv50, 17)
        const auto aidx = adv50_.size() - 1;
        corr_.forward((std::min(idx, aidx) + 1 >= 17uz)
            ? a101::correlation(close_, adv50_, std::min(idx, aidx), 17)
            : a101::NaN);

        if (idx + 1 < 286uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto dvidx = delta_vwap_.size() - 1;
        const auto cidx = corr_.size() - 1;
        const double r1 = a101::ts_rank(delta_vwap_, dvidx, std::min(dvidx + 1, 252uz));
        const double tr2 = a101::ts_rank(corr_, cidx, 17);
        line_.forward(std::pow(r1, tr2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 286; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> delta_vwap_;
    Line<double> adv50_;
    Line<double> corr_;
};

// ============================================================================
// Alpha#076: max(rank(decay_linear(delta(vwap,1),11)),
//            Ts_Rank(decay_linear(Ts_Rank(correlation(IndNeutralize(low,sector),
//            adv81,8),19),17),19)) * -1
// IndNeutralize(low) → low.  Inputs: H,L,C,V   Warmup: 303
// ============================================================================
class Alpha101_076 : public Indicator<Alpha101_076> {
public:
    Alpha101_076(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            dv_.data().reserve(n);
            dl1_.data().reserve(n);
            adv81_.data().reserve(n);
            corr_.data().reserve(n);
            tsr_corr_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();

        // --- Branch 1: rank(decay_linear(delta(vwap,1), 11)) ---
        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);
        const auto vidx = vwap_.size() - 1;
        dv_.forward((vidx >= 1uz) ? a101::delta(vwap_, vidx, 1) : a101::NaN);
        const auto dividx = dv_.size() - 1;
        dl1_.forward((dividx + 1 >= 11uz)
            ? a101::decay_linear(dv_, dividx, 11) : a101::NaN);

        // --- Branch 2: Ts_Rank(decay_linear(Ts_Rank(corr(low,adv81,8),19),17),19) ---
        adv81_.forward(a101::adv(volume_, idx, 81));
        const auto aidx = adv81_.size() - 1;
        corr_.forward((std::min(idx, aidx) + 1 >= 8uz)
            ? a101::correlation(low_, adv81_, std::min(idx, aidx), 8)
            : a101::NaN);
        const auto cidx = corr_.size() - 1;
        tsr_corr_.forward((cidx + 1 >= 19uz)
            ? a101::ts_rank(corr_, cidx, 19) : a101::NaN);
        const auto tcidx = tsr_corr_.size() - 1;
        dl2_.forward((tcidx + 1 >= 17uz)
            ? a101::decay_linear(tsr_corr_, tcidx, 17) : a101::NaN);

        if (idx + 1 < 303uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto d1idx = dl1_.size() - 1;
        const auto d2idx = dl2_.size() - 1;
        const double r1 = a101::ts_rank(dl1_, d1idx, std::min(d1idx + 1, 252uz));
        const double tr2 = a101::ts_rank(dl2_, d2idx, 19);
        line_.forward(std::max(r1, tr2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 303; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> dv_;
    Line<double> dl1_;
    Line<double> adv81_;
    Line<double> corr_;
    Line<double> tsr_corr_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#079: (rank(delta(IndNeutralize(close*0.60733+open*(1-0.60733),sector),1))
//            < rank(correlation(Ts_Rank(vwap,3), Ts_Rank(adv150,9), 14)))
// IndNeutralize → passthrough.  Returns -1 if true, 0 otherwise.
// Inputs: O,H,L,C,V   Warmup: 275
// ============================================================================
class Alpha101_079 : public Indicator<Alpha101_079> {
public:
    Alpha101_079(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            blend_.data().reserve(n);
            delta_blend_.data().reserve(n);
            vwap_.data().reserve(n);
            tsr_vwap_.data().reserve(n);
            adv150_.data().reserve(n);
            tsr_adv_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();

        // blend = close*0.60733 + open*(1-0.60733)
        const double vw = a101::vwap(high_, low_, close_, idx);
        const double bl = close_.data()[idx] * 0.60733 + open_.data()[idx] * (1.0 - 0.60733);
        blend_.forward(bl);
        const auto bidx = blend_.size() - 1;
        delta_blend_.forward((bidx >= 1uz)
            ? a101::delta(blend_, bidx, 1) : a101::NaN);

        vwap_.forward(vw);
        const auto vidx = vwap_.size() - 1;
        tsr_vwap_.forward((vidx + 1 >= 3uz)
            ? a101::ts_rank(vwap_, vidx, 3) : a101::NaN);

        adv150_.forward(a101::adv(volume_, idx, 150));
        const auto aidx = adv150_.size() - 1;
        tsr_adv_.forward((aidx + 1 >= 9uz)
            ? a101::ts_rank(adv150_, aidx, 9) : a101::NaN);

        const auto tvidx = tsr_vwap_.size() - 1;
        const auto taidx = tsr_adv_.size() - 1;
        const auto cl = std::min(tvidx, taidx) + 1;
        corr_.forward((cl >= 14uz)
            ? a101::correlation(tsr_vwap_, tsr_adv_, std::min(tvidx, taidx), 14)
            : a101::NaN);

        if (idx + 1 < 275uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto dbidx = delta_blend_.size() - 1;
        const auto cidx = corr_.size() - 1;
        const double r1 = a101::ts_rank(delta_blend_, dbidx, std::min(dbidx + 1, 252uz));
        const double r2 = a101::ts_rank(corr_, cidx, std::min(cidx + 1, 252uz));
        line_.forward((r1 < r2) ? -1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 275; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> blend_;
    Line<double> delta_blend_;
    Line<double> vwap_;
    Line<double> tsr_vwap_;
    Line<double> adv150_;
    Line<double> tsr_adv_;
    Line<double> corr_;
};

// ============================================================================
// Alpha#080: (rank(Sign(delta(IndNeutralize(open*0.868128+high*(1-0.868128),
//            industry),4)))^Ts_Rank(correlation(high,adv10,5),5)) * -1
// IndNeutralize → passthrough.  Inputs: O,H,L,C,V   Warmup: 262
// ============================================================================
class Alpha101_080 : public Indicator<Alpha101_080> {
public:
    Alpha101_080(const Line<double>& open, const Line<double>& high,
                 const Line<double>& close, const Line<double>& volume)
        : open_(open), high_(high), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            blend_.data().reserve(n);
            delta_sign_.data().reserve(n);
            adv10_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();

        // blend = open*0.868128 + high*(1-0.868128)
        blend_.forward(open_.data()[idx] * 0.868128 + high_.data()[idx] * (1.0 - 0.868128));

        const auto bidx = blend_.size() - 1;
        const double db = (bidx >= 4uz) ? a101::delta(blend_, bidx, 4) : a101::NaN;
        delta_sign_.forward(std::isnan(db) ? a101::NaN : a101::sign(db));

        adv10_.forward(a101::adv(volume_, idx, 10));
        const auto aidx = adv10_.size() - 1;
        corr_.forward((std::min(idx, aidx) + 1 >= 5uz)
            ? a101::correlation(high_, adv10_, std::min(idx, aidx), 5)
            : a101::NaN);

        if (idx + 1 < 262uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto dsidx = delta_sign_.size() - 1;
        const auto cidx = corr_.size() - 1;
        const double r1 = a101::ts_rank(delta_sign_, dsidx, std::min(dsidx + 1, 252uz));
        const double tr2 = a101::ts_rank(corr_, cidx, 5);
        line_.forward(std::pow(r1, tr2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> blend_;
    Line<double> delta_sign_;
    Line<double> adv10_;
    Line<double> corr_;
};

// ============================================================================
// Alpha#082: min(rank(decay_linear(delta(open,1),14)),
//            Ts_Rank(decay_linear(correlation(IndNeutralize(volume,sector),
//            open*0.634196+open*(1-0.634196),  // simplifies to open
//            17), 6), 13)) * -1
// IndNeutralize(volume) → volume.  Inputs: O,C,V   Warmup: 289
// ============================================================================
class Alpha101_082 : public Indicator<Alpha101_082> {
public:
    Alpha101_082(const Line<double>& open, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            delta_o_.data().reserve(n);
            dl1_.data().reserve(n);
            corr_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();

        // delta(open, 1)
        delta_o_.forward((idx >= 1uz)
            ? a101::delta(open_, idx, 1) : a101::NaN);
        const auto doidx = delta_o_.size() - 1;
        dl1_.forward((doidx + 1 >= 14uz)
            ? a101::decay_linear(delta_o_, doidx, 14) : a101::NaN);

        // correlation(volume, open, 17) — blend simplifies to open
        corr_.forward((idx + 1 >= 17uz)
            ? a101::correlation(volume_, open_, idx, 17) : a101::NaN);
        const auto cidx = corr_.size() - 1;
        dl2_.forward((cidx + 1 >= 6uz)
            ? a101::decay_linear(corr_, cidx, 6) : a101::NaN);

        if (idx + 1 < 289uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto d1idx = dl1_.size() - 1;
        const auto d2idx = dl2_.size() - 1;
        const double r1 = a101::ts_rank(dl1_, d1idx, std::min(d1idx + 1, 252uz));
        const double tr2 = a101::ts_rank(dl2_, d2idx, 13);
        line_.forward(std::min(r1, tr2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 289; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> delta_o_;
    Line<double> dl1_;
    Line<double> corr_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#087: max(rank(decay_linear(delta(close*0.369701+vwap*(1-0.369701),1),2)),
//            Ts_Rank(decay_linear(abs(correlation(IndNeutralize(adv81,industry),
//            close,13)),4),14)) * -1
// IndNeutralize(adv81) → adv81.  Inputs: H,L,C,V   Warmup: 285
// ============================================================================
class Alpha101_087 : public Indicator<Alpha101_087> {
public:
    Alpha101_087(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            blend_.data().reserve(n);
            delta_bl_.data().reserve(n);
            dl1_.data().reserve(n);
            adv81_.data().reserve(n);
            corr_abs_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();

        // blend = close*0.369701 + vwap*(1-0.369701)
        const double vw = a101::vwap(high_, low_, close_, idx);
        blend_.forward(close_.data()[idx] * 0.369701 + vw * (1.0 - 0.369701));
        const auto bidx = blend_.size() - 1;
        delta_bl_.forward((bidx >= 1uz)
            ? a101::delta(blend_, bidx, 1) : a101::NaN);
        const auto dbidx = delta_bl_.size() - 1;
        dl1_.forward((dbidx + 1 >= 2uz)
            ? a101::decay_linear(delta_bl_, dbidx, 2) : a101::NaN);

        // adv81
        adv81_.forward(a101::adv(volume_, idx, 81));
        const auto aidx = adv81_.size() - 1;
        // correlation(adv81, close, 13)
        const auto cl = std::min(aidx, idx) + 1;
        const double c = (cl >= 13uz)
            ? a101::correlation(adv81_, close_, std::min(aidx, idx), 13)
            : a101::NaN;
        corr_abs_.forward(std::isnan(c) ? a101::NaN : std::abs(c));
        const auto caidx = corr_abs_.size() - 1;
        dl2_.forward((caidx + 1 >= 4uz)
            ? a101::decay_linear(corr_abs_, caidx, 4) : a101::NaN);

        if (idx + 1 < 285uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto d1idx = dl1_.size() - 1;
        const auto d2idx = dl2_.size() - 1;
        const double r1 = a101::ts_rank(dl1_, d1idx, std::min(d1idx + 1, 252uz));
        const double tr2 = a101::ts_rank(dl2_, d2idx, 14);
        line_.forward(std::max(r1, tr2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 285; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> blend_;
    Line<double> delta_bl_;
    Line<double> dl1_;
    Line<double> adv81_;
    Line<double> corr_abs_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#089: Ts_Rank(decay_linear(correlation(low*0.967285+low*(1-0.967285),
//            adv10, 6), 5), 3)  // inner simplifies to low
//          - Ts_Rank(decay_linear(delta(IndNeutralize(vwap,industry),3),10),15)
// IndNeutralize(vwap) → vwap.  Inputs: H,L,C,V   Warmup: 28
// ============================================================================
class Alpha101_089 : public Indicator<Alpha101_089> {
public:
    Alpha101_089(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            adv10_.data().reserve(n);
            corr_.data().reserve(n);
            dl1_.data().reserve(n);
            vwap_.data().reserve(n);
            delta_vwap_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();

        // Branch 1: decay_linear(correlation(low, adv10, 6), 5)
        adv10_.forward(a101::adv(volume_, idx, 10));
        const auto aidx = adv10_.size() - 1;
        corr_.forward((std::min(idx, aidx) + 1 >= 6uz)
            ? a101::correlation(low_, adv10_, std::min(idx, aidx), 6)
            : a101::NaN);
        const auto cidx = corr_.size() - 1;
        dl1_.forward((cidx + 1 >= 5uz)
            ? a101::decay_linear(corr_, cidx, 5) : a101::NaN);

        // Branch 2: decay_linear(delta(vwap, 3), 10)
        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);
        const auto vidx = vwap_.size() - 1;
        delta_vwap_.forward((vidx >= 3uz)
            ? a101::delta(vwap_, vidx, 3) : a101::NaN);
        const auto dvidx = delta_vwap_.size() - 1;
        dl2_.forward((dvidx + 1 >= 10uz)
            ? a101::decay_linear(delta_vwap_, dvidx, 10) : a101::NaN);

        if (idx + 1 < 28uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto d1idx = dl1_.size() - 1;
        const auto d2idx = dl2_.size() - 1;
        const double tr1 = a101::ts_rank(dl1_, d1idx, 3);
        const double tr2 = a101::ts_rank(dl2_, d2idx, 15);
        line_.forward(tr1 - tr2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 28; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> adv10_;
    Line<double> corr_;
    Line<double> dl1_;
    Line<double> vwap_;
    Line<double> delta_vwap_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#090: (rank(close - ts_max(close, 4))^Ts_Rank(correlation(
//            IndNeutralize(adv40, subindustry), low, 5), 2)) * -1
// IndNeutralize(adv40) → adv40.  Inputs: H,L,C,V   Warmup: 260
// ============================================================================
class Alpha101_090 : public Indicator<Alpha101_090> {
public:
    Alpha101_090(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            diff_.data().reserve(n);
            adv40_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();

        // close - ts_max(close, 4)
        diff_.forward((idx + 1 >= 4uz)
            ? (close_.data()[idx] - a101::ts_max(close_, idx, 4)) : a101::NaN);

        adv40_.forward(a101::adv(volume_, idx, 40));
        const auto aidx = adv40_.size() - 1;
        corr_.forward((std::min(aidx, idx) + 1 >= 5uz)
            ? a101::correlation(adv40_, low_, std::min(aidx, idx), 5)
            : a101::NaN);

        if (idx + 1 < 260uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto didx = diff_.size() - 1;
        const auto cidx = corr_.size() - 1;
        const double r1 = a101::ts_rank(diff_, didx, std::min(didx + 1, 252uz));
        const double tr2 = a101::ts_rank(corr_, cidx, 2);
        line_.forward(std::pow(r1, tr2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 260; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> diff_;
    Line<double> adv40_;
    Line<double> corr_;
};

// ============================================================================
// Alpha#091: (Ts_Rank(decay_linear(decay_linear(correlation(IndNeutralize(
//            close,industry), volume, 9), 16), 3), 4)
//          - rank(decay_linear(correlation(vwap, adv30, 4), 2))) * -1
// IndNeutralize(close) → close.  Inputs: H,L,C,V   Warmup: 284
// ============================================================================
class Alpha101_091 : public Indicator<Alpha101_091> {
public:
    Alpha101_091(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            corr1_.data().reserve(n);
            dl1a_.data().reserve(n);
            dl1b_.data().reserve(n);
            vwap_.data().reserve(n);
            adv30_.data().reserve(n);
            corr2_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();

        // Branch 1: decay_linear(decay_linear(correlation(close,volume,9),16),3)
        corr1_.forward((idx + 1 >= 9uz)
            ? a101::correlation(close_, volume_, idx, 9) : a101::NaN);
        const auto c1idx = corr1_.size() - 1;
        dl1a_.forward((c1idx + 1 >= 16uz)
            ? a101::decay_linear(corr1_, c1idx, 16) : a101::NaN);
        const auto d1aidx = dl1a_.size() - 1;
        dl1b_.forward((d1aidx + 1 >= 3uz)
            ? a101::decay_linear(dl1a_, d1aidx, 3) : a101::NaN);

        // Branch 2: decay_linear(correlation(vwap, adv30, 4), 2)
        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);
        adv30_.forward(a101::adv(volume_, idx, 30));
        const auto vidx = vwap_.size() - 1;
        const auto aidx = adv30_.size() - 1;
        const auto cl = std::min(vidx, aidx) + 1;
        corr2_.forward((cl >= 4uz)
            ? a101::correlation(vwap_, adv30_, std::min(vidx, aidx), 4)
            : a101::NaN);
        const auto c2idx = corr2_.size() - 1;
        dl2_.forward((c2idx + 1 >= 2uz)
            ? a101::decay_linear(corr2_, c2idx, 2) : a101::NaN);

        if (idx + 1 < 284uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto d1bidx = dl1b_.size() - 1;
        const auto d2idx = dl2_.size() - 1;
        const double tr1 = a101::ts_rank(dl1b_, d1bidx, 4);
        const double r2 = a101::ts_rank(dl2_, d2idx, std::min(d2idx + 1, 252uz));
        line_.forward((tr1 - r2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 284; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> corr1_;
    Line<double> dl1a_;
    Line<double> dl1b_;
    Line<double> vwap_;
    Line<double> adv30_;
    Line<double> corr2_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#093: Ts_Rank(decay_linear(correlation(IndNeutralize(vwap,industry),
//            adv81, 17), 19), 7)
//          / rank(decay_linear(delta(close*0.524434+vwap*(1-0.524434), 2), 16))
// IndNeutralize(vwap) → vwap.  Inputs: H,L,C,V   Warmup: 300
// ============================================================================
class Alpha101_093 : public Indicator<Alpha101_093> {
public:
    Alpha101_093(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            adv81_.data().reserve(n);
            corr_.data().reserve(n);
            dl1_.data().reserve(n);
            blend_.data().reserve(n);
            delta_bl_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();

        const double vw = a101::vwap(high_, low_, close_, idx);
        vwap_.forward(vw);

        adv81_.forward(a101::adv(volume_, idx, 81));

        // correlation(vwap, adv81, 17)
        const auto vidx = vwap_.size() - 1;
        const auto aidx = adv81_.size() - 1;
        const auto cl = std::min(vidx, aidx) + 1;
        corr_.forward((cl >= 17uz)
            ? a101::correlation(vwap_, adv81_, std::min(vidx, aidx), 17)
            : a101::NaN);
        const auto cidx = corr_.size() - 1;
        dl1_.forward((cidx + 1 >= 19uz)
            ? a101::decay_linear(corr_, cidx, 19) : a101::NaN);

        // blend = close*0.524434 + vwap*(1-0.524434)
        blend_.forward(close_.data()[idx] * 0.524434 + vw * (1.0 - 0.524434));
        const auto bidx = blend_.size() - 1;
        delta_bl_.forward((bidx >= 2uz)
            ? a101::delta(blend_, bidx, 2) : a101::NaN);
        const auto dbidx = delta_bl_.size() - 1;
        dl2_.forward((dbidx + 1 >= 16uz)
            ? a101::decay_linear(delta_bl_, dbidx, 16) : a101::NaN);

        if (idx + 1 < 300uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto d1idx = dl1_.size() - 1;
        const auto d2idx = dl2_.size() - 1;
        const double tr1 = a101::ts_rank(dl1_, d1idx, 7);
        const double r2 = a101::ts_rank(dl2_, d2idx, std::min(d2idx + 1, 252uz));
        line_.forward((r2 != 0.0) ? (tr1 / r2) : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 300; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_;
    Line<double> adv81_;
    Line<double> corr_;
    Line<double> dl1_;
    Line<double> blend_;
    Line<double> delta_bl_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#097: (rank(decay_linear(delta(IndNeutralize(low*0.721001+vwap*(1-0.721001),
//            industry),3),20))
//          - Ts_Rank(decay_linear(Ts_Rank(correlation(Ts_Rank(low,7),
//            Ts_Rank(adv60,17),4),18),15),6)) * -1
// IndNeutralize → passthrough.  Inputs: H,L,C,V   Warmup: 310
// ============================================================================
class Alpha101_097 : public Indicator<Alpha101_097> {
public:
    Alpha101_097(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            blend_.data().reserve(n);
            delta_bl_.data().reserve(n);
            dl1_.data().reserve(n);
            tsr_low_.data().reserve(n);
            adv60_.data().reserve(n);
            tsr_adv_.data().reserve(n);
            corr_.data().reserve(n);
            tsr_corr_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();

        // Branch 1: decay_linear(delta(blend, 3), 20)
        const double vw = a101::vwap(high_, low_, close_, idx);
        blend_.forward(low_.data()[idx] * 0.721001 + vw * (1.0 - 0.721001));
        const auto bidx = blend_.size() - 1;
        delta_bl_.forward((bidx >= 3uz)
            ? a101::delta(blend_, bidx, 3) : a101::NaN);
        const auto dbidx = delta_bl_.size() - 1;
        dl1_.forward((dbidx + 1 >= 20uz)
            ? a101::decay_linear(delta_bl_, dbidx, 20) : a101::NaN);

        // Branch 2: decay_linear(Ts_Rank(corr(Ts_Rank(low,7),Ts_Rank(adv60,17),4),18),15)
        tsr_low_.forward((idx + 1 >= 7uz)
            ? a101::ts_rank(low_, idx, 7) : a101::NaN);
        adv60_.forward(a101::adv(volume_, idx, 60));
        const auto aidx = adv60_.size() - 1;
        tsr_adv_.forward((aidx + 1 >= 17uz)
            ? a101::ts_rank(adv60_, aidx, 17) : a101::NaN);

        const auto tlidx = tsr_low_.size() - 1;
        const auto taidx = tsr_adv_.size() - 1;
        const auto cl = std::min(tlidx, taidx) + 1;
        corr_.forward((cl >= 4uz)
            ? a101::correlation(tsr_low_, tsr_adv_, std::min(tlidx, taidx), 4)
            : a101::NaN);
        const auto cidx = corr_.size() - 1;
        tsr_corr_.forward((cidx + 1 >= 18uz)
            ? a101::ts_rank(corr_, cidx, 18) : a101::NaN);
        const auto tcidx = tsr_corr_.size() - 1;
        dl2_.forward((tcidx + 1 >= 15uz)
            ? a101::decay_linear(tsr_corr_, tcidx, 15) : a101::NaN);

        if (idx + 1 < 310uz) [[unlikely]] {
            line_.forward(a101::NaN);
            return;
        }

        const auto d1idx = dl1_.size() - 1;
        const auto d2idx = dl2_.size() - 1;
        const double r1 = a101::ts_rank(dl1_, d1idx, std::min(d1idx + 1, 252uz));
        const double tr2 = a101::ts_rank(dl2_, d2idx, 6);
        line_.forward((r1 - tr2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 310; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> blend_;
    Line<double> delta_bl_;
    Line<double> dl1_;
    Line<double> tsr_low_;
    Line<double> adv60_;
    Line<double> tsr_adv_;
    Line<double> corr_;
    Line<double> tsr_corr_;
    Line<double> dl2_;
};

// ============================================================================
// Alpha#100: (0 - (1 * (1.5*scale(IndNeutralize(IndNeutralize(rank(
//            ((close-low)-(high-close))/(high-low)*volume),subind),subind))
//          - scale(IndNeutralize(correlation(close,rank(adv20),5)
//            - rank(ts_argmin(close,30)),subind))) * (volume/adv20)))
// IndNeutralize → passthrough. rank → ts_rank.
// Inputs: H,L,C,V   Warmup: 282
// ============================================================================
class Alpha101_100 : public Indicator<Alpha101_100> {
public:
    Alpha101_100(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            inner1_raw_.data().reserve(n);
            rank_inner1_.data().reserve(n);
            adv20_.data().reserve(n);
            rank_adv20_.data().reserve(n);
            corr_.data().reserve(n);
            argmin_.data().reserve(n);
            rank_argmin_.data().reserve(n);
            inner2_raw_.data().reserve(n);
        }
        const auto idx = close_.index();

        // inner1 = ((close-low)-(high-close))/(high-low) * volume
        const double h = high_.data()[idx];
        const double l = low_.data()[idx];
        const double c = close_.data()[idx];
        const double hl = h - l;
        const double body = (hl != 0.0) ? (((c - l) - (h - c)) / hl) : 0.0;
        inner1_raw_.forward(body * volume_.data()[idx]);

        // rank(inner1) → ts_rank
        const auto i1idx = inner1_raw_.size() - 1;
        rank_inner1_.forward(a101::ts_rank(inner1_raw_, i1idx,
            std::min(i1idx + 1, 252uz)));

        // rank(adv20) → ts_rank of adv20 scratch
        adv20_.forward(a101::adv(volume_, idx, 20));
        const auto aidx = adv20_.size() - 1;
        rank_adv20_.forward(a101::ts_rank(adv20_, aidx,
            std::min(aidx + 1, 252uz)));

        // correlation(close, rank_adv20, 5)
        const auto raidx = rank_adv20_.size() - 1;
        corr_.forward((std::min(idx, raidx) + 1 >= 5uz)
            ? a101::correlation(close_, rank_adv20_, std::min(idx, raidx), 5)
            : a101::NaN);

        // ts_argmin(close, 30) → rank(ts_argmin)
        if (idx + 1 >= 30uz) {
            const double am = a101::ts_argmin(close_, idx, 30);
            argmin_.forward(am);
        } else {
            argmin_.forward(a101::NaN);
        }
        const auto amidx = argmin_.size() - 1;
        rank_argmin_.forward(a101::ts_rank(argmin_, amidx,
            std::min(amidx + 1, 252uz)));

        // inner2 = correlation(close, rank(adv20), 5) - rank(ts_argmin(close, 30))
        const auto cidx = corr_.size() - 1;
        const auto ramidx = rank_argmin_.size() - 1;
        const double cv = corr_.data()[cidx];
        const double ram = rank_argmin_.data()[ramidx];
        inner2_raw_.forward((std::isnan(cv) || std::isnan(ram))
            ? a101::NaN : (cv - ram));

        if (idx + 1 < 282uz) [[unlikely]] {
            abs_sum1_ += std::isnan(rank_inner1_.data().back()) ? 0.0
                       : std::abs(rank_inner1_.data().back());
            abs_sum2_ += std::isnan(inner2_raw_.data().back()) ? 0.0
                       : std::abs(inner2_raw_.data().back());
            line_.forward(a101::NaN);
            return;
        }

        const double ri1 = rank_inner1_.data().back();
        abs_sum1_ += std::abs(ri1);
        const double scaled1 = a101::scale(ri1, abs_sum1_);

        const double i2 = inner2_raw_.data().back();
        abs_sum2_ += std::abs(i2);
        const double scaled2 = a101::scale(i2, abs_sum2_);

        const double av = adv20_.data().back();
        const double vol = volume_.data()[idx];
        const double vol_ratio = (av != 0.0) ? (vol / av) : 0.0;

        line_.forward(-(1.5 * scaled1 - scaled2) * vol_ratio);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 282; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> inner1_raw_;
    Line<double> rank_inner1_;
    Line<double> adv20_;
    Line<double> rank_adv20_;
    Line<double> corr_;
    Line<double> argmin_;
    Line<double> rank_argmin_;
    Line<double> inner2_raw_;
    double abs_sum1_ = 0.0;
    double abs_sum2_ = 0.0;
};

} // namespace stratforge
