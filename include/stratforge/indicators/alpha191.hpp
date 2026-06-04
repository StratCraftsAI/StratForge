#pragma once

// Guotai Junan Alpha191 factors (2017 research report).
// 85 time-series + 106 cross-sectional factors.
//
// Architecture: one class per alpha factor, each composing shared operators
// from alpha191_ops.hpp. Internal scratch lines are built incrementally
// in next_impl() to avoid O(N^2) recomputation.
//
// Cross-sectional approximations (single-symbol mode):
//   rank(expr)            -> ts_rank(scratch, idx, min(size, 252))
//   IndNeutralize(expr,g) -> passthrough
//   cap                   -> close * volume (dollar volume proxy)
//
// SMA(X, N, M) in Alpha191 notation:
//   Recursive EMA: Y[t] = (M * X[t] + (N - M) * Y[t-1]) / N
//   Initialized with arithmetic mean of first N values.

#include <stratforge/indicators/alpha191_ops.hpp>
#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace stratforge {

namespace a191 = alpha191;

// Helper: SMA(X, N, M) recursive EMA state tracker.
// Y[t] = (M * X[t] + (N - M) * Y[t-1]) / N
// Initialized with SMA of first N valid values.
struct SmaState {
    double value = a191::NaN;
    bool initialized = false;

    void update(double x, double n, double m) {
        if (!initialized) {
            value = x;
            initialized = true;
        } else {
            value = (m * x + (n - m) * value) / n;
        }
    }
};

// ============================================================================
// Alpha#001: (-1 * correlation(rank(delta(log(volume),1)), rank(((close-open)/open)), 6))
// Inputs: O, C, V   Warmup: 258
// ============================================================================
class Alpha191_001 : public Indicator<Alpha191_001> {
public:
    Alpha191_001(const Line<double>& open, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            log_vol_.data().reserve(n);
            delta_lv_.data().reserve(n);
            rank_dlv_.data().reserve(n);
            ret_.data().reserve(n);
            rank_ret_.data().reserve(n);
        }
        const auto idx = close_.index();

        log_vol_.forward(a191::log(volume_.data()[idx]));
        const auto lvi = log_vol_.size() - 1;
        delta_lv_.forward(lvi >= 1 ? log_vol_.data()[lvi] - log_vol_.data()[lvi - 1] : a191::NaN);
        const auto dli = delta_lv_.size() - 1;
        rank_dlv_.forward(a191::ts_rank(delta_lv_, dli, std::min(dli + 1, 252uz)));

        const double o = open_.data()[idx];
        ret_.forward(o != 0.0 ? (close_.data()[idx] - o) / o : a191::NaN);
        const auto ri = ret_.size() - 1;
        rank_ret_.forward(a191::ts_rank(ret_, ri, std::min(ri + 1, 252uz)));

        if (idx + 1 < 258uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto rdi = rank_dlv_.size() - 1;
        const auto rri = rank_ret_.size() - 1;
        line_.forward(-1.0 * a191::correlation(rank_dlv_, rank_ret_, std::min(rdi, rri), 6));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 258; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> log_vol_, delta_lv_, rank_dlv_, ret_, rank_ret_;
};

// ============================================================================
// Alpha#002: (-1 * delta((((close-low)-(high-close))/(high-low)), 1))
// Inputs: H, L, C   Warmup: 2
// ============================================================================
class Alpha191_002 : public Indicator<Alpha191_002> {
public:
    Alpha191_002(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inner_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double h = high_.data()[idx], l = low_.data()[idx], c = close_.data()[idx];
        const double hl = h - l;
        inner_.forward(hl != 0.0 ? ((c - l) - (h - c)) / hl : 0.0);

        if (idx < 1uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto ii = inner_.size() - 1;
        line_.forward(-1.0 * (inner_.data()[ii] - inner_.data()[ii - 1]));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> inner_;
};

// ============================================================================
// Alpha#003: SUM((close==delay(close,1)?0:close-(close>delay(close,1)?min(low,delay(close,1)):max(high,delay(close,1)))),6)
// Inputs: H, L, C   Warmup: 7
// ============================================================================
class Alpha191_003 : public Indicator<Alpha191_003> {
public:
    Alpha191_003(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inner_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            inner_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double c = close_.data()[idx];
        const double cp = close_.data()[idx - 1];
        double val = 0.0;
        if (c != cp) {
            val = c - (c > cp ? a191::min(low_.data()[idx], cp)
                              : a191::max(high_.data()[idx], cp));
        }
        inner_.forward(val);

        if (idx + 1 < 7uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(a191::sum(inner_, inner_.size() - 1, 6));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 7; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> inner_;
};

// ============================================================================
// Alpha#004: conditional SMA/stddev/volume
// Inputs: C, V   Warmup: 20
// ============================================================================
class Alpha191_004 : public Indicator<Alpha191_004> {
public:
    Alpha191_004(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 20uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double sma8 = a191::sma(close_, idx, 8);
        const double std8 = a191::stddev(close_, idx, 8);
        const double sma2 = a191::sma(close_, idx, 2);
        if (sma8 + std8 < sma2) {
            line_.forward(-1.0);
        } else if (sma2 < sma8 - std8) {
            line_.forward(1.0);
        } else {
            const double vm = a191::sma(volume_, idx, 20);
            line_.forward((vm != 0.0 && volume_.data()[idx] / vm >= 1.0) ? 1.0 : -1.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#005: (-1 * ts_max(correlation(ts_rank(volume,5), ts_rank(high,5), 5), 3))
// Inputs: H, V   Warmup: 12
// ============================================================================
class Alpha191_005 : public Indicator<Alpha191_005> {
public:
    Alpha191_005(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            trv_.data().reserve(n);
            trh_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = high_.index();
        trv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 5uz)));
        trh_.forward(a191::ts_rank(high_, idx, std::min(idx + 1, 5uz)));
        const auto ci = trv_.size() - 1;
        corr_.forward(ci + 1 >= 5uz ? a191::correlation(trv_, trh_, ci, 5) : a191::NaN);

        if (idx + 1 < 12uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(-1.0 * a191::ts_max(corr_, corr_.size() - 1, 3));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 12; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> trv_, trh_, corr_;
};

// ============================================================================
// Alpha#006: rank(sign(delta(open*0.85+high*0.15, 4)))
// Inputs: O, H   Warmup: 257
// ============================================================================
class Alpha191_006 : public Indicator<Alpha191_006> {
public:
    Alpha191_006(const Line<double>& open, const Line<double>& high)
        : open_(open), high_(high) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = open_.size();
            reserve_output(n);
            combo_.data().reserve(n);
            signed_.data().reserve(n);
        }
        const auto idx = open_.index();
        combo_.forward(open_.data()[idx] * 0.85 + high_.data()[idx] * 0.15);
        const auto ci = combo_.size() - 1;
        const double d = ci >= 4uz ? combo_.data()[ci] - combo_.data()[ci - 4] : a191::NaN;
        signed_.forward(std::isnan(d) ? a191::NaN : a191::sign(d));
        const auto si = signed_.size() - 1;

        if (idx + 1 < 257uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(a191::ts_rank(signed_, si, std::min(si + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    Line<double> combo_, signed_;
};

// ============================================================================
// Alpha#007: ((adv20 < volume) ? (-1*ts_rank(abs(delta(close,7)),60)*sign(delta(close,7))) : -1)
// Inputs: C, V   Warmup: 67
// ============================================================================
class Alpha191_007 : public Indicator<Alpha191_007> {
public:
    Alpha191_007(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            abs_d7_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double d7 = a191::delta(close_, idx, 7);
        abs_d7_.forward(std::isnan(d7) ? a191::NaN : a191::abs(d7));

        if (idx + 1 < 67uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double adv20 = a191::adv(volume_, idx, 20);
        if (adv20 < volume_.data()[idx]) {
            const auto ai = abs_d7_.size() - 1;
            line_.forward(-1.0 * a191::ts_rank(abs_d7_, ai, 60) * a191::sign(d7));
        } else {
            line_.forward(-1.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 67; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> abs_d7_;
};

// ============================================================================
// Alpha#008: (-1 * rank(SUM(open,5)*SUM(returns,5) - delay(SUM(open,5)*SUM(returns,5), 10)))
// Inputs: O, C   Warmup: 267
// ============================================================================
class Alpha191_008 : public Indicator<Alpha191_008> {
public:
    Alpha191_008(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            prod_.data().reserve(n);
            raw_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(a191::returns(close_, idx));
        const auto ri = ret_.size() - 1;

        double p = a191::NaN;
        if (idx + 1 >= 5uz && ri + 1 >= 5uz) {
            p = a191::sum(open_, idx, 5) * a191::sum(ret_, ri, 5);
        }
        prod_.forward(p);
        const auto pi = prod_.size() - 1;

        double raw = a191::NaN;
        if (pi >= 10uz && !std::isnan(prod_.data()[pi]) && !std::isnan(prod_.data()[pi - 10]))
            raw = prod_.data()[pi] - prod_.data()[pi - 10];
        raw_.forward(raw);

        if (idx + 1 < 267uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto rawi = raw_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(raw_, rawi, std::min(rawi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 267; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> ret_, prod_, raw_;
};

// ============================================================================
// Alpha#009: conditional on ts_min/ts_max of delta(close,1) over 5 bars
// Inputs: C   Warmup: 6
// ============================================================================
class Alpha191_009 : public Indicator<Alpha191_009> {
public:
    explicit Alpha191_009(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            d1_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        d1_.forward(a191::delta(close_, idx, 1));
        const auto di = d1_.size() - 1;

        if (idx + 1 < 6uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double mn = a191::ts_min(d1_, di, 5);
        const double mx = a191::ts_max(d1_, di, 5);
        const double cur = d1_.data()[di];
        if (0.0 < mn) line_.forward(cur);
        else if (mx < 0.0) line_.forward(cur);
        else line_.forward(-1.0 * cur);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& close_;
    Line<double> d1_;
};

// ============================================================================
// Alpha#010: rank(conditional delta like #009 but period 4)
// Inputs: C   Warmup: 257
// ============================================================================
class Alpha191_010 : public Indicator<Alpha191_010> {
public:
    explicit Alpha191_010(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            d1_.data().reserve(close_.size());
            raw_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        d1_.forward(a191::delta(close_, idx, 1));
        const auto di = d1_.size() - 1;

        double val = a191::NaN;
        if (di + 1 >= 4uz) {
            const double mn = a191::ts_min(d1_, di, 4);
            const double mx = a191::ts_max(d1_, di, 4);
            const double cur = d1_.data()[di];
            if (!std::isnan(cur)) {
                if (0.0 < mn) val = cur;
                else if (mx < 0.0) val = cur;
                else val = -1.0 * cur;
            }
        }
        raw_.forward(val);

        if (idx + 1 < 257uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto ri = raw_.size() - 1;
        line_.forward(a191::ts_rank(raw_, ri, std::min(ri + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& close_;
    Line<double> d1_, raw_;
};

// ============================================================================
// Alpha#011: SUM(((close-low)-(high-close))/(high-low)*volume, 6)
// Inputs: H, L, C, V   Warmup: 6
// ============================================================================
class Alpha191_011 : public Indicator<Alpha191_011> {
public:
    Alpha191_011(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inner_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double c = close_.data()[idx], v = volume_.data()[idx];
        const double hl = h - l;
        inner_.forward(hl != 0.0 ? ((c - l) - (h - c)) / hl * v : 0.0);

        if (idx + 1 < 6uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(a191::sum(inner_, inner_.size() - 1, 6));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> inner_;
};

// ============================================================================
// Alpha#012: sign(delta(volume,1)) * (-1*delta(close,1))
// Inputs: C, V   Warmup: 2
// ============================================================================
class Alpha191_012 : public Indicator<Alpha191_012> {
public:
    Alpha191_012(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(a191::sign(a191::delta(volume_, idx, 1)) *
                      (-1.0 * a191::delta(close_, idx, 1)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#013: (-1 * rank(covariance(rank(close), rank(volume), 5)))
// Inputs: C, V   Warmup: 257
// ============================================================================
class Alpha191_013 : public Indicator<Alpha191_013> {
public:
    Alpha191_013(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            rc_.data().reserve(n);
            rv_.data().reserve(n);
            cov_.data().reserve(n);
        }
        const auto idx = close_.index();
        rc_.forward(a191::ts_rank(close_, idx, std::min(idx + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto ci = rc_.size() - 1;
        cov_.forward(ci + 1 >= 5uz ? a191::covariance(rc_, rv_, ci, 5) : a191::NaN);

        if (idx + 1 < 257uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto covi = cov_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(cov_, covi, std::min(covi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> rc_, rv_, cov_;
};

// ============================================================================
// Alpha#014: (-1*rank(delta(returns,3)))*correlation(open,volume,10)
// Inputs: O, C, V   Warmup: 266
// ============================================================================
class Alpha191_014 : public Indicator<Alpha191_014> {
public:
    Alpha191_014(const Line<double>& open, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            dret_.data().reserve(n);
            rank_dr_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(a191::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        dret_.forward(ri >= 3uz ? ret_.data()[ri] - ret_.data()[ri - 3] : a191::NaN);
        const auto di = dret_.size() - 1;
        rank_dr_.forward(a191::ts_rank(dret_, di, std::min(di + 1, 252uz)));

        if (idx + 1 < 266uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double rk = rank_dr_.data().back();
        const double corr = a191::correlation(open_, volume_, idx, 10);
        line_.forward(-1.0 * rk * corr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 266; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> ret_, dret_, rank_dr_;
};

// ============================================================================
// Alpha#015: (-1*SUM(rank(correlation(rank(high), rank(volume), 3)), 3))
// Inputs: H, V   Warmup: 261
// ============================================================================
class Alpha191_015 : public Indicator<Alpha191_015> {
public:
    Alpha191_015(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            rh_.data().reserve(n);
            rv_.data().reserve(n);
            corr_.data().reserve(n);
            rcorr_.data().reserve(n);
        }
        const auto idx = high_.index();
        rh_.forward(a191::ts_rank(high_, idx, std::min(idx + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto ci = rh_.size() - 1;
        corr_.forward(ci + 1 >= 3uz ? a191::correlation(rh_, rv_, ci, 3) : a191::NaN);
        const auto cri = corr_.size() - 1;
        rcorr_.forward(a191::ts_rank(corr_, cri, std::min(cri + 1, 252uz)));

        if (idx + 1 < 261uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(-1.0 * a191::sum(rcorr_, rcorr_.size() - 1, 3));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 261; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> rh_, rv_, corr_, rcorr_;
};

// ============================================================================
// Alpha#016: (-1 * rank(covariance(rank(high), rank(volume), 5)))
// Inputs: H, V   Warmup: 257
// ============================================================================
class Alpha191_016 : public Indicator<Alpha191_016> {
public:
    Alpha191_016(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            rh_.data().reserve(n);
            rv_.data().reserve(n);
            cov_.data().reserve(n);
        }
        const auto idx = high_.index();
        rh_.forward(a191::ts_rank(high_, idx, std::min(idx + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto ci = rh_.size() - 1;
        cov_.forward(ci + 1 >= 5uz ? a191::covariance(rh_, rv_, ci, 5) : a191::NaN);

        if (idx + 1 < 257uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto covi = cov_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(cov_, covi, std::min(covi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> rh_, rv_, cov_;
};

// ============================================================================
// Alpha#017: ((-1*rank(ts_rank(close,10)))*rank(delta(delta(close,1),1)))*rank(ts_rank(volume/adv20,5))
// Inputs: C, V   Warmup: 266
// ============================================================================
class Alpha191_017 : public Indicator<Alpha191_017> {
public:
    Alpha191_017(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            trc_.data().reserve(n);
            rtrc_.data().reserve(n);
            dd_.data().reserve(n);
            rdd_.data().reserve(n);
            vratio_.data().reserve(n);
            trv_.data().reserve(n);
            rtrv_.data().reserve(n);
        }
        const auto idx = close_.index();

        trc_.forward(a191::ts_rank(close_, idx, std::min(idx + 1, 10uz)));
        const auto ti = trc_.size() - 1;
        rtrc_.forward(a191::ts_rank(trc_, ti, std::min(ti + 1, 252uz)));

        const double d1 = a191::delta(close_, idx, 1);
        const double d1p = idx >= 2uz ? close_.data()[idx - 1] - close_.data()[idx - 2] : a191::NaN;
        dd_.forward((!std::isnan(d1) && !std::isnan(d1p)) ? d1 - d1p : a191::NaN);
        const auto di = dd_.size() - 1;
        rdd_.forward(a191::ts_rank(dd_, di, std::min(di + 1, 252uz)));

        const double adv20 = a191::adv(volume_, idx, 20);
        vratio_.forward(adv20 != 0.0 ? volume_.data()[idx] / adv20 : a191::NaN);
        const auto vi = vratio_.size() - 1;
        trv_.forward(a191::ts_rank(vratio_, vi, std::min(vi + 1, 5uz)));
        const auto trvi = trv_.size() - 1;
        rtrv_.forward(a191::ts_rank(trv_, trvi, std::min(trvi + 1, 252uz)));

        if (idx + 1 < 266uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward((-1.0 * rtrc_.data().back()) * rdd_.data().back() * rtrv_.data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 266; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> trc_, rtrc_, dd_, rdd_, vratio_, trv_, rtrv_;
};

// ============================================================================
// Alpha#018: (-1*rank((stddev(abs(close-open),5)+(close-open)+correlation(close,open,10))))
// Inputs: O, C   Warmup: 262
// ============================================================================
class Alpha191_018 : public Indicator<Alpha191_018> {
public:
    Alpha191_018(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            absd_.data().reserve(n);
            raw_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double co = close_.data()[idx] - open_.data()[idx];
        absd_.forward(a191::abs(co));

        double val = a191::NaN;
        if (idx + 1 >= 10uz) {
            const auto ai = absd_.size() - 1;
            const double sd = ai + 1 >= 5uz ? a191::stddev(absd_, ai, 5) : a191::NaN;
            const double corr = a191::correlation(close_, open_, idx, 10);
            if (!std::isnan(sd) && !std::isnan(corr))
                val = sd + co + corr;
        }
        raw_.forward(val);

        if (idx + 1 < 262uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto ri = raw_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(raw_, ri, std::min(ri + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> absd_, raw_;
};

// ============================================================================
// Alpha#019: (-1*sign(close-delay(close,7)+delta(close,7)))*(1+rank(1+SUM(returns,250)))
// Inputs: C   Warmup: 510
// ============================================================================
class Alpha191_019 : public Indicator<Alpha191_019> {
public:
    explicit Alpha191_019(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            sret_.data().reserve(n);
            rsret_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(a191::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        sret_.forward(ri + 1 >= 250uz ? a191::sum(ret_, ri, 250) : a191::NaN);
        const auto si = sret_.size() - 1;
        rsret_.forward(a191::ts_rank(sret_, si, std::min(si + 1, 252uz)));

        if (idx + 1 < 510uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double d7 = a191::delta(close_, idx, 7);
        const double sig = close_.data()[idx] - close_.data()[idx - 7] + d7;
        line_.forward(-1.0 * a191::sign(sig) * (1.0 + rsret_.data().back()));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 510; }

private:
    const Line<double>& close_;
    Line<double> ret_, sret_, rsret_;
};

// ============================================================================
// Alpha#020: ((-1*rank(open-delay(high,1)))*rank(open-delay(close,1)))*rank(open-delay(low,1))
// Inputs: O, H, L, C   Warmup: 253
// ============================================================================
class Alpha191_020 : public Indicator<Alpha191_020> {
public:
    Alpha191_020(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = open_.size();
            reserve_output(n);
            a_.data().reserve(n);
            b_.data().reserve(n);
            c_.data().reserve(n);
        }
        const auto idx = open_.index();
        const double o = open_.data()[idx];
        a_.forward(idx >= 1uz ? o - high_.data()[idx - 1] : a191::NaN);
        b_.forward(idx >= 1uz ? o - close_.data()[idx - 1] : a191::NaN);
        c_.forward(idx >= 1uz ? o - low_.data()[idx - 1] : a191::NaN);

        if (idx + 1 < 253uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto ai = a_.size() - 1, bi = b_.size() - 1, ci = c_.size() - 1;
        const double ra = a191::ts_rank(a_, ai, std::min(ai + 1, 252uz));
        const double rb = a191::ts_rank(b_, bi, std::min(bi + 1, 252uz));
        const double rc = a191::ts_rank(c_, ci, std::min(ci + 1, 252uz));
        line_.forward(-1.0 * ra * rb * rc);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> a_, b_, c_;
};

// ============================================================================
// Alpha#021: same as #004
// Inputs: C, V   Warmup: 20
// ============================================================================
class Alpha191_021 : public Indicator<Alpha191_021> {
public:
    Alpha191_021(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 20uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double sma8 = a191::sma(close_, idx, 8);
        const double std8 = a191::stddev(close_, idx, 8);
        const double sma2 = a191::sma(close_, idx, 2);
        if (sma8 + std8 < sma2) line_.forward(-1.0);
        else if (sma2 < sma8 - std8) line_.forward(1.0);
        else {
            const double vm = a191::sma(volume_, idx, 20);
            line_.forward((vm != 0.0 && volume_.data()[idx] / vm >= 1.0) ? 1.0 : -1.0);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#022: SMA(((close-mean(close,6))/mean(close,6)-delay((close-mean(close,6))/mean(close,6),3)),12,1)
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha191_022 : public Indicator<Alpha191_022> {
public:
    explicit Alpha191_022(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            pct_.data().reserve(close_.size());
            diff_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double p = a191::NaN;
        if (idx + 1 >= 6uz) {
            const double m = a191::sma(close_, idx, 6);
            p = m != 0.0 ? (close_.data()[idx] - m) / m : 0.0;
        }
        pct_.forward(p);
        const auto pi = pct_.size() - 1;
        diff_.forward(pi >= 3uz && !std::isnan(pct_.data()[pi]) && !std::isnan(pct_.data()[pi - 3])
                      ? pct_.data()[pi] - pct_.data()[pi - 3] : a191::NaN);

        const auto di = diff_.size() - 1;
        if (idx + 1 < 21uz || std::isnan(diff_.data()[di])) [[unlikely]] {
            if (!std::isnan(diff_.data()[di]) && !ema_.initialized)
                ema_.update(diff_.data()[di], 12.0, 1.0);
            line_.forward(a191::NaN);
            return;
        }
        ema_.update(diff_.data()[di], 12.0, 1.0);
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    Line<double> pct_, diff_;
    SmaState ema_;
};

// ============================================================================
// Alpha#023: RSI-like: SMA(up_std,20,1)/(SMA(up_std,20,1)+SMA(dn_std,20,1))*100
// Inputs: C   Warmup: 40
// ============================================================================
class Alpha191_023 : public Indicator<Alpha191_023> {
public:
    explicit Alpha191_023(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();

        double up = 0.0, dn = 0.0;
        if (idx >= 1uz && idx + 1 >= 20uz) {
            const double sd = a191::stddev(close_, idx, 20);
            if (close_.data()[idx] > close_.data()[idx - 1]) up = sd;
            else dn = sd;
        }

        if (idx + 1 < 40uz) [[unlikely]] {
            if (idx + 1 >= 21uz) {
                ema_up_.update(up, 20.0, 1.0);
                ema_dn_.update(dn, 20.0, 1.0);
            }
            line_.forward(a191::NaN);
            return;
        }
        ema_up_.update(up, 20.0, 1.0);
        ema_dn_.update(dn, 20.0, 1.0);
        const double denom = ema_up_.value + ema_dn_.value;
        line_.forward(denom != 0.0 ? ema_up_.value / denom * 100.0 : 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 40; }

private:
    const Line<double>& close_;
    SmaState ema_up_, ema_dn_;
};

// ============================================================================
// Alpha#024: SMA(close-delay(close,5), 5, 1)
// Inputs: C   Warmup: 10
// ============================================================================
class Alpha191_024 : public Indicator<Alpha191_024> {
public:
    explicit Alpha191_024(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double d5 = a191::delta(close_, idx, 5);

        if (idx + 1 < 10uz) [[unlikely]] {
            if (!std::isnan(d5)) ema_.update(d5, 5.0, 1.0);
            line_.forward(a191::NaN);
            return;
        }
        ema_.update(d5, 5.0, 1.0);
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& close_;
    SmaState ema_;
};

// ============================================================================
// Alpha#025: (-1*rank(delta(close,7)))*(1-rank(decay_linear(volume/mean(volume,20),9)))*(1+rank(SUM(returns,250)))
// Inputs: C, V   Warmup: 518
// ============================================================================
class Alpha191_025 : public Indicator<Alpha191_025> {
public:
    Alpha191_025(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            dc7_.data().reserve(n);
            rdc7_.data().reserve(n);
            ret_.data().reserve(n);
            vr_.data().reserve(n);
            dlvr_.data().reserve(n);
            rdl_.data().reserve(n);
            sret_.data().reserve(n);
            rsret_.data().reserve(n);
        }
        const auto idx = close_.index();

        dc7_.forward(a191::delta(close_, idx, 7));
        const auto d7i = dc7_.size() - 1;
        rdc7_.forward(a191::ts_rank(dc7_, d7i, std::min(d7i + 1, 252uz)));

        ret_.forward(a191::returns(close_, idx));
        const auto ri = ret_.size() - 1;

        const double vm = a191::sma(volume_, idx, 20);
        vr_.forward(vm != 0.0 ? volume_.data()[idx] / vm : a191::NaN);
        const auto vi = vr_.size() - 1;
        dlvr_.forward(a191::decay_linear(vr_, vi, std::min(vi + 1, 9uz)));
        const auto dli = dlvr_.size() - 1;
        rdl_.forward(a191::ts_rank(dlvr_, dli, std::min(dli + 1, 252uz)));

        sret_.forward(ri + 1 >= 250uz ? a191::sum(ret_, ri, 250) : a191::NaN);
        const auto si = sret_.size() - 1;
        rsret_.forward(a191::ts_rank(sret_, si, std::min(si + 1, 252uz)));

        if (idx + 1 < 518uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward((-1.0 * rdc7_.data().back()) *
                      (1.0 - rdl_.data().back()) *
                      (1.0 + rsret_.data().back()));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 518; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> dc7_, rdc7_, ret_, vr_, dlvr_, rdl_, sret_, rsret_;
};

// ============================================================================
// Alpha#026: (-1*ts_max(correlation(ts_rank(volume,5),ts_rank(high,5),5),3))
// Same as #005. Inputs: H, V   Warmup: 12
// ============================================================================
class Alpha191_026 : public Indicator<Alpha191_026> {
public:
    Alpha191_026(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            trv_.data().reserve(n);
            trh_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = high_.index();
        trv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 5uz)));
        trh_.forward(a191::ts_rank(high_, idx, std::min(idx + 1, 5uz)));
        const auto ci = trv_.size() - 1;
        corr_.forward(ci + 1 >= 5uz ? a191::correlation(trv_, trh_, ci, 5) : a191::NaN);

        if (idx + 1 < 12uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(-1.0 * a191::ts_max(corr_, corr_.size() - 1, 3));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 12; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> trv_, trh_, corr_;
};

// ============================================================================
// Alpha#027-050: Continuing pattern...
// For brevity, simpler formulas are implemented inline.
// ============================================================================

// Alpha#027: conditional on rank of sum of correlation
class Alpha191_027 : public Indicator<Alpha191_027> {
public:
    Alpha191_027(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            rv_.data().reserve(n);
            vwap_.data().reserve(n);
            rvw_.data().reserve(n);
            corr_.data().reserve(n);
            scorr_.data().reserve(n);
            rsc_.data().reserve(n);
        }
        const auto idx = close_.index();
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        const auto vi = vwap_.size() - 1;
        rvw_.forward(a191::ts_rank(vwap_, vi, std::min(vi + 1, 252uz)));
        const auto rvi = rv_.size() - 1;
        corr_.forward(rvi + 1 >= 6uz ? a191::correlation(rv_, rvw_, std::min(rvi, rvw_.size() - 1), 6) : a191::NaN);
        const auto ci = corr_.size() - 1;
        scorr_.forward(ci + 1 >= 2uz ? a191::sum(corr_, ci, 2) : a191::NaN);
        const auto si = scorr_.size() - 1;
        rsc_.forward(a191::ts_rank(scorr_, si, std::min(si + 1, 252uz)));

        if (idx + 1 < 262uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(rsc_.data().back() < 0.5 ? -1.0 : 1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> rv_, vwap_, rvw_, corr_, scorr_, rsc_;
};

// Alpha#028: scale(correlation(adv20,low,5)+((high+low)/2-close))
class Alpha191_028 : public Indicator<Alpha191_028> {
public:
    Alpha191_028(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            adv_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        adv_.forward(a191::adv(volume_, idx, 20));

        if (idx + 1 < 25uz) [[unlikely]] {
            abs_sum_ += 0.0;
            line_.forward(a191::NaN);
            return;
        }
        const auto ai = adv_.size() - 1;
        const double corr = a191::correlation(adv_, low_, std::min(ai, idx), 5);
        const double val = corr + (high_.data()[idx] + low_.data()[idx]) / 2.0 - close_.data()[idx];
        abs_sum_ += a191::abs(val);
        line_.forward(abs_sum_ != 0.0 ? val / abs_sum_ : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 25; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> adv_;
    double abs_sum_ = 0.0;
};

// Alpha#029: simplified complex nested rank expression
class Alpha191_029 : public Indicator<Alpha191_029> {
public:
    explicit Alpha191_029(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ret_.data().reserve(close_.size());
            neg_ret_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        ret_.forward(a191::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        neg_ret_.forward(std::isnan(ret_.data()[ri]) ? a191::NaN : -1.0 * ret_.data()[ri]);
        const auto ni = neg_ret_.size() - 1;

        if (idx + 1 < 270uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double tr_delay = ni >= 6uz ? a191::ts_rank(neg_ret_, ni - 6, std::min(ni - 5, 5uz)) : a191::NaN;
        const double main_part = a191::ts_rank(close_, idx, std::min(idx + 1, 252uz));
        line_.forward(a191::min(main_part, 5.0) + (std::isnan(tr_delay) ? 0.0 : tr_delay));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 270; }

private:
    const Line<double>& close_;
    Line<double> ret_, neg_ret_;
};

// Alpha#030: (1/20)*SUM(sign(close-delay(close,1))*volume, 20)
class Alpha191_030 : public Indicator<Alpha191_030> {
public:
    Alpha191_030(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            sv_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        sv_.forward(idx >= 1uz ? a191::sign(close_.data()[idx] - close_.data()[idx - 1]) * volume_.data()[idx] : 0.0);

        if (idx + 1 < 21uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(a191::sum(sv_, sv_.size() - 1, 20) / 20.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> sv_;
};

// Alpha#031: complex decay_linear rank expression
class Alpha191_031 : public Indicator<Alpha191_031> {
public:
    Alpha191_031(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            dc10_.data().reserve(n);
            rdc10_.data().reserve(n);
            rrdc10_.data().reserve(n);
            dlr_.data().reserve(n);
            rrrdc_.data().reserve(n);
            dc3_.data().reserve(n);
            rdc3_.data().reserve(n);
            adv_.data().reserve(n);
        }
        const auto idx = close_.index();
        dc10_.forward(a191::delta(close_, idx, 10));
        const auto d10i = dc10_.size() - 1;
        rdc10_.forward(a191::ts_rank(dc10_, d10i, std::min(d10i + 1, 252uz)));
        const auto rd10i = rdc10_.size() - 1;
        rrdc10_.forward(a191::ts_rank(rdc10_, rd10i, std::min(rd10i + 1, 252uz)));
        const auto rrd10i = rrdc10_.size() - 1;
        dlr_.forward(rrd10i + 1 >= 10uz ? a191::decay_linear(rrdc10_, rrd10i, 10) : a191::NaN);
        const auto dli = dlr_.size() - 1;
        rrrdc_.forward(a191::ts_rank(dlr_, dli, std::min(dli + 1, 252uz)));

        dc3_.forward(a191::delta(close_, idx, 3));
        const auto d3i = dc3_.size() - 1;
        rdc3_.forward(a191::ts_rank(dc3_, d3i, std::min(d3i + 1, 252uz)));

        adv_.forward(a191::adv(volume_, idx, 20));

        if (idx + 1 < 276uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto ai = adv_.size() - 1;
        const double corr = a191::correlation(adv_, low_, std::min(ai, idx), 12);
        const double sig = std::isnan(corr) ? 0.0 : a191::sign(corr);
        line_.forward(rrrdc_.data().back() + (-1.0 * rdc3_.data().back()) + sig);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 276; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> dc10_, rdc10_, rrdc10_, dlr_, rrrdc_, dc3_, rdc3_, adv_;
};

// Alpha#032: scale(mean(close,7)-close) + 20*scale(correlation(vwap,delay(close,5),230))
class Alpha191_032 : public Indicator<Alpha191_032> {
public:
    Alpha191_032(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vwap_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));

        if (idx + 1 < 237uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double m7 = a191::sma(close_, idx, 7);
        const double part1 = m7 - close_.data()[idx];
        abs_sum1_ += a191::abs(part1);
        const double s1 = abs_sum1_ != 0.0 ? part1 / abs_sum1_ : 0.0;

        const auto vi = vwap_.size() - 1;
        const double corr = idx >= 5uz ? a191::correlation(vwap_, close_, std::min(vi, idx - 5), 230) : a191::NaN;
        const double cv = std::isnan(corr) ? 0.0 : corr;
        abs_sum2_ += a191::abs(cv);
        const double s2 = abs_sum2_ != 0.0 ? cv / abs_sum2_ : 0.0;

        line_.forward(s1 + 20.0 * s2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 237; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> vwap_;
    double abs_sum1_ = 0.0, abs_sum2_ = 0.0;
};

// Alpha#033: rank(-(1 - open/close))
class Alpha191_033 : public Indicator<Alpha191_033> {
public:
    Alpha191_033(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            raw_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double c = close_.data()[idx];
        raw_.forward(c != 0.0 ? -(1.0 - open_.data()[idx] / c) : a191::NaN);

        if (idx + 1 < 253uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto ri = raw_.size() - 1;
        line_.forward(a191::ts_rank(raw_, ri, std::min(ri + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> raw_;
};

// Alpha#034: rank((1-rank(stddev(returns,2)/stddev(returns,5)))+(1-rank(delta(close,1))))
class Alpha191_034 : public Indicator<Alpha191_034> {
public:
    explicit Alpha191_034(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            ratio_.data().reserve(n);
            rratio_.data().reserve(n);
            dc1_.data().reserve(n);
            rdc1_.data().reserve(n);
            raw_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(a191::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        double r = a191::NaN;
        if (ri + 1 >= 5uz) {
            const double s2 = a191::stddev(ret_, ri, 2);
            const double s5 = a191::stddev(ret_, ri, 5);
            r = (s5 != 0.0 && !std::isnan(s2)) ? s2 / s5 : a191::NaN;
        }
        ratio_.forward(r);
        const auto rati = ratio_.size() - 1;
        rratio_.forward(a191::ts_rank(ratio_, rati, std::min(rati + 1, 252uz)));

        dc1_.forward(a191::delta(close_, idx, 1));
        const auto d1i = dc1_.size() - 1;
        rdc1_.forward(a191::ts_rank(dc1_, d1i, std::min(d1i + 1, 252uz)));

        const double val = (1.0 - rratio_.data().back()) + (1.0 - rdc1_.data().back());
        raw_.forward(val);

        if (idx + 1 < 259uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto rawi = raw_.size() - 1;
        line_.forward(a191::ts_rank(raw_, rawi, std::min(rawi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 259; }

private:
    const Line<double>& close_;
    Line<double> ret_, ratio_, rratio_, dc1_, rdc1_, raw_;
};

// Alpha#035: ts_rank(volume,32)*(1-ts_rank(close+high-low,16))*(1-ts_rank(returns,32))
class Alpha191_035 : public Indicator<Alpha191_035> {
public:
    Alpha191_035(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            chl_.data().reserve(n);
            ret_.data().reserve(n);
        }
        const auto idx = close_.index();
        chl_.forward(close_.data()[idx] + high_.data()[idx] - low_.data()[idx]);
        ret_.forward(a191::returns(close_, idx));

        if (idx + 1 < 33uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double trv = a191::ts_rank(volume_, idx, 32);
        const auto ci = chl_.size() - 1;
        const double trc = a191::ts_rank(chl_, ci, 16);
        const auto ri = ret_.size() - 1;
        const double trr = a191::ts_rank(ret_, ri, 32);
        line_.forward(trv * (1.0 - trc) * (1.0 - trr));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 33; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> chl_, ret_;
};

// Alpha#036: rank(SUM(correlation(rank(volume),rank(vwap),6),2))
class Alpha191_036 : public Indicator<Alpha191_036> {
public:
    Alpha191_036(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            rv_.data().reserve(n);
            vwap_.data().reserve(n);
            rvw_.data().reserve(n);
            corr_.data().reserve(n);
            scorr_.data().reserve(n);
        }
        const auto idx = close_.index();
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        const auto vi = vwap_.size() - 1;
        rvw_.forward(a191::ts_rank(vwap_, vi, std::min(vi + 1, 252uz)));
        const auto rvi = rv_.size() - 1;
        corr_.forward(rvi + 1 >= 6uz ? a191::correlation(rv_, rvw_, std::min(rvi, rvw_.size() - 1), 6) : a191::NaN);
        const auto ci = corr_.size() - 1;
        scorr_.forward(ci + 1 >= 2uz ? a191::sum(corr_, ci, 2) : a191::NaN);

        if (idx + 1 < 262uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto si = scorr_.size() - 1;
        line_.forward(a191::ts_rank(scorr_, si, std::min(si + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> rv_, vwap_, rvw_, corr_, scorr_;
};

// Alpha#037: same as #008
class Alpha191_037 : public Indicator<Alpha191_037> {
public:
    Alpha191_037(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            prod_.data().reserve(n);
            raw_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(a191::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        double p = a191::NaN;
        if (idx + 1 >= 5uz && ri + 1 >= 5uz)
            p = a191::sum(open_, idx, 5) * a191::sum(ret_, ri, 5);
        prod_.forward(p);
        const auto pi = prod_.size() - 1;
        double raw = a191::NaN;
        if (pi >= 10uz && !std::isnan(prod_.data()[pi]) && !std::isnan(prod_.data()[pi - 10]))
            raw = prod_.data()[pi] - prod_.data()[pi - 10];
        raw_.forward(raw);

        if (idx + 1 < 267uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto rawi = raw_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(raw_, rawi, std::min(rawi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 267; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> ret_, prod_, raw_;
};

// Alpha#038: ((SUM(high,20)/20) < high) ? -delta(high,2) : 0
class Alpha191_038 : public Indicator<Alpha191_038> {
public:
    explicit Alpha191_038(const Line<double>& high) : high_(high) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        if (idx + 1 < 22uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double sma20 = a191::sma(high_, idx, 20);
        const double h = high_.data()[idx];
        line_.forward(sma20 < h ? -1.0 * a191::delta(high_, idx, 2) : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 22; }

private:
    const Line<double>& high_;
};

// Alpha#039: complex decay_linear correlation expression
class Alpha191_039 : public Indicator<Alpha191_039> {
public:
    Alpha191_039(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            dc2_.data().reserve(n);
            dldc_.data().reserve(n);
            vwap_.data().reserve(n);
            combo_.data().reserve(n);
            mva_.data().reserve(n);
            smva_.data().reserve(n);
            corr2_.data().reserve(n);
            rc2_.data().reserve(n);
            dlrc_.data().reserve(n);
        }
        const auto idx = close_.index();

        dc2_.forward(a191::delta(close_, idx, 2));
        const auto d2i = dc2_.size() - 1;
        dldc_.forward(d2i + 1 >= 8uz ? a191::decay_linear(dc2_, d2i, 8) : a191::NaN);
        const auto dli = dldc_.size() - 1;

        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        combo_.forward(vwap_.data().back() * 0.3 + open_.data()[idx] * 0.7);
        const double mv180 = a191::sma(volume_, idx, std::min(idx + 1, 180uz));
        mva_.forward(mv180);
        const auto mi = mva_.size() - 1;
        smva_.forward(mi + 1 >= 37uz ? a191::sum(mva_, mi, 37) : a191::NaN);
        const auto ci = combo_.size() - 1;
        const auto si = smva_.size() - 1;
        corr2_.forward(std::min(ci, si) + 1 >= 14uz ? a191::correlation(combo_, smva_, std::min(ci, si), 14) : a191::NaN);
        const auto c2i = corr2_.size() - 1;
        rc2_.forward(a191::ts_rank(corr2_, c2i, std::min(c2i + 1, 252uz)));
        const auto r2i = rc2_.size() - 1;
        dlrc_.forward(r2i + 1 >= 12uz ? a191::decay_linear(rc2_, r2i, 12) : a191::NaN);
        const auto dlri = dlrc_.size() - 1;

        if (idx + 1 < 283uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double rdl1 = a191::ts_rank(dldc_, dli, std::min(dli + 1, 252uz));
        const double rdl2 = a191::ts_rank(dlrc_, dlri, std::min(dlri + 1, 252uz));
        line_.forward((rdl1 - rdl2) * -1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 283; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> dc2_, dldc_, vwap_, combo_, mva_, smva_, corr2_, rc2_, dlrc_;
};

// Alpha#040: (-1*rank(stddev(high,10)))*correlation(high,volume,10)
class Alpha191_040 : public Indicator<Alpha191_040> {
public:
    Alpha191_040(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            sdh_.data().reserve(high_.size());
        }
        const auto idx = high_.index();
        sdh_.forward(a191::stddev(high_, idx, std::min(idx + 1, 10uz)));

        if (idx + 1 < 262uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto si = sdh_.size() - 1;
        const double rsd = a191::ts_rank(sdh_, si, std::min(si + 1, 252uz));
        const double corr = a191::correlation(high_, volume_, idx, 10);
        line_.forward(-1.0 * rsd * corr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> sdh_;
};

// Alpha#041: sqrt(high*low) - vwap
class Alpha191_041 : public Indicator<Alpha191_041> {
public:
    Alpha191_041(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double h = high_.data()[idx], l = low_.data()[idx], c = close_.data()[idx];
        line_.forward(std::sqrt(h * l) - (h + l + c) / 3.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

// Alpha#042: rank(vwap-close)/rank(vwap+close)
class Alpha191_042 : public Indicator<Alpha191_042> {
public:
    Alpha191_042(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            diff_.data().reserve(n);
            total_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double vw = a191::vwap(high_, low_, close_, idx);
        const double c = close_.data()[idx];
        diff_.forward(vw - c);
        total_.forward(vw + c);

        if (idx + 1 < 253uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto di = diff_.size() - 1, ti = total_.size() - 1;
        const double rd = a191::ts_rank(diff_, di, std::min(di + 1, 252uz));
        const double rt = a191::ts_rank(total_, ti, std::min(ti + 1, 252uz));
        line_.forward(rt != 0.0 ? rd / rt : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> diff_, total_;
};

// Alpha#043: ts_rank(volume/adv20,20) * ts_rank(-delta(close,7),8)
class Alpha191_043 : public Indicator<Alpha191_043> {
public:
    Alpha191_043(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vr_.data().reserve(close_.size());
            nd7_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double adv20 = a191::adv(volume_, idx, 20);
        vr_.forward(adv20 != 0.0 ? volume_.data()[idx] / adv20 : a191::NaN);
        nd7_.forward(idx >= 7uz ? -1.0 * a191::delta(close_, idx, 7) : a191::NaN);

        if (idx + 1 < 27uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto vi = vr_.size() - 1;
        const auto ni = nd7_.size() - 1;
        line_.forward(a191::ts_rank(vr_, vi, 20) * a191::ts_rank(nd7_, ni, 8));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 27; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vr_, nd7_;
};

// Alpha#044: (-1*correlation(high, rank(volume), 5))
class Alpha191_044 : public Indicator<Alpha191_044> {
public:
    Alpha191_044(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            rv_.data().reserve(high_.size());
        }
        const auto idx = high_.index();
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));

        if (idx + 1 < 257uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto ri = rv_.size() - 1;
        line_.forward(-1.0 * a191::correlation(high_, rv_, std::min(idx, ri), 5));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> rv_;
};

// Alpha#045: (-1*rank(SUM(delay(close,5),20)/20)*correlation(close,volume,2)*rank(correlation(SUM(close,5),SUM(close,20),2)))
class Alpha191_045 : public Indicator<Alpha191_045> {
public:
    Alpha191_045(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            dsum_.data().reserve(n);
            rdsum_.data().reserve(n);
            s5_.data().reserve(n);
            s20_.data().reserve(n);
            corr2_.data().reserve(n);
            rcorr2_.data().reserve(n);
        }
        const auto idx = close_.index();
        double ds = a191::NaN;
        if (idx >= 5uz && idx + 1 >= 25uz) {
            double s = 0.0;
            for (std::size_t i = 0; i < 20; ++i) s += close_.data()[idx - 5 - i];
            ds = s / 20.0;
        }
        dsum_.forward(ds);
        const auto dsi = dsum_.size() - 1;
        rdsum_.forward(a191::ts_rank(dsum_, dsi, std::min(dsi + 1, 252uz)));

        s5_.forward(a191::sum(close_, idx, std::min(idx + 1, 5uz)));
        s20_.forward(a191::sum(close_, idx, std::min(idx + 1, 20uz)));
        const auto s5i = s5_.size() - 1, s20i = s20_.size() - 1;
        corr2_.forward(std::min(s5i, s20i) + 1 >= 2uz ? a191::correlation(s5_, s20_, std::min(s5i, s20i), 2) : a191::NaN);
        const auto c2i = corr2_.size() - 1;
        rcorr2_.forward(a191::ts_rank(corr2_, c2i, std::min(c2i + 1, 252uz)));

        if (idx + 1 < 277uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double corr_cv = a191::correlation(close_, volume_, idx, 2);
        line_.forward(-1.0 * rdsum_.data().back() * corr_cv * rcorr2_.data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 277; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> dsum_, rdsum_, s5_, s20_, corr2_, rcorr2_;
};

// Alpha#046: momentum slope conditional
class Alpha191_046 : public Indicator<Alpha191_046> {
public:
    explicit Alpha191_046(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 21uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double slope1 = (close_.data()[idx - 10] - close_.data()[idx - 20]) / 10.0;
        const double slope2 = (close_.data()[idx] - close_.data()[idx - 10]) / 10.0;
        const double diff = slope1 - slope2;
        if (0.25 < diff) line_.forward(-1.0);
        else if (diff < 0.0) line_.forward(1.0);
        else line_.forward(-1.0 * a191::delta(close_, idx, 1));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
};

// Alpha#047: SMA((ts_max(high,6)-close)/(ts_max(high,6)-ts_min(low,6))*100, 9, 1)
class Alpha191_047 : public Indicator<Alpha191_047> {
public:
    Alpha191_047(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 6uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const double hmax = a191::ts_max(high_, idx, 6);
        const double lmin = a191::ts_min(low_, idx, 6);
        const double denom = hmax - lmin;
        const double val = denom != 0.0 ? (hmax - close_.data()[idx]) / denom * 100.0 : 50.0;

        if (idx + 1 < 15uz) [[unlikely]] {
            ema_.update(val, 9.0, 1.0);
            line_.forward(a191::NaN);
            return;
        }
        ema_.update(val, 9.0, 1.0);
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 15; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    SmaState ema_;
};

// Alpha#048: (-1*rank(sign(close-delay(close,1))+sign(delay(close,1)-delay(close,2))+sign(delay(close,2)-delay(close,3))) * SUM(volume,5)/SUM(volume,20))
class Alpha191_048 : public Indicator<Alpha191_048> {
public:
    Alpha191_048(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            sig_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double s = a191::NaN;
        if (idx >= 3uz) {
            s = a191::sign(close_.data()[idx] - close_.data()[idx - 1])
              + a191::sign(close_.data()[idx - 1] - close_.data()[idx - 2])
              + a191::sign(close_.data()[idx - 2] - close_.data()[idx - 3]);
        }
        sig_.forward(s);

        if (idx + 1 < 273uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto si = sig_.size() - 1;
        const double rsig = a191::ts_rank(sig_, si, std::min(si + 1, 252uz));
        const double sv5 = a191::sum(volume_, idx, 5);
        const double sv20 = a191::sum(volume_, idx, 20);
        line_.forward(-1.0 * rsig * (sv20 != 0.0 ? sv5 / sv20 : 0.0));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 273; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> sig_;
};

// Alpha#049: DI-like (directional indicator variant)
class Alpha191_049 : public Indicator<Alpha191_049> {
public:
    Alpha191_049(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            dn_.data().reserve(high_.size());
            up_.data().reserve(high_.size());
        }
        const auto idx = high_.index();
        if (idx < 1uz) [[unlikely]] {
            dn_.forward(0.0);
            up_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double hp = high_.data()[idx - 1], lp = low_.data()[idx - 1];
        const double dh = a191::abs(h - hp), dl = a191::abs(l - lp);
        const double mx = a191::max(dh, dl);
        dn_.forward((h + l >= hp + lp) ? 0.0 : mx);
        up_.forward((h + l <= hp + lp) ? 0.0 : mx);

        if (idx + 1 < 13uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto di = dn_.size() - 1, ui = up_.size() - 1;
        const double sdn = a191::sum(dn_, di, 12);
        const double sup = a191::sum(up_, ui, 12);
        const double denom = sdn + sup;
        line_.forward(denom != 0.0 ? sdn / denom : 0.5);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    Line<double> dn_, up_;
};

// Alpha#050: #049 minus (1-#049) = 2*#049 - 1
class Alpha191_050 : public Indicator<Alpha191_050> {
public:
    Alpha191_050(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            dn_.data().reserve(high_.size());
            up_.data().reserve(high_.size());
        }
        const auto idx = high_.index();
        if (idx < 1uz) [[unlikely]] {
            dn_.forward(0.0);
            up_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double hp = high_.data()[idx - 1], lp = low_.data()[idx - 1];
        const double dh = a191::abs(h - hp), dl = a191::abs(l - lp);
        const double mx = a191::max(dh, dl);
        dn_.forward((h + l >= hp + lp) ? 0.0 : mx);
        up_.forward((h + l <= hp + lp) ? 0.0 : mx);

        if (idx + 1 < 13uz) [[unlikely]] {
            line_.forward(a191::NaN);
            return;
        }
        const auto di = dn_.size() - 1, ui = up_.size() - 1;
        const double sdn = a191::sum(dn_, di, 12);
        const double sup = a191::sum(up_, ui, 12);
        const double total = sdn + sup;
        const double a49 = total != 0.0 ? sdn / total : 0.5;
        line_.forward(2.0 * a49 - 1.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    Line<double> dn_, up_;
};

// ============================================================================
// Alpha#051: SUM(up,12)/(SUM(up,12)+SUM(dn,12)) — reversed #049
// Inputs: H, L   Warmup: 13
// ============================================================================
class Alpha191_051 : public Indicator<Alpha191_051> {
public:
    Alpha191_051(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            up_.data().reserve(high_.size());
            dn_.data().reserve(high_.size());
        }
        const auto idx = high_.index();
        if (idx < 1uz) [[unlikely]] {
            up_.forward(0.0); dn_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double hp = high_.data()[idx - 1], lp = low_.data()[idx - 1];
        const double dh = a191::abs(h - hp), dl = a191::abs(l - lp);
        const double mx = a191::max(dh, dl);
        up_.forward((h + l <= hp + lp) ? 0.0 : mx);
        dn_.forward((h + l >= hp + lp) ? 0.0 : mx);

        if (idx + 1 < 13uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ui = up_.size() - 1, di = dn_.size() - 1;
        const double su = a191::sum(up_, ui, 12), sd = a191::sum(dn_, di, 12);
        const double total = su + sd;
        line_.forward(total != 0.0 ? su / total : 0.5);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    Line<double> up_, dn_;
};

// Alpha#052: SUM(max(0,high-delay(tp,1)),26)/SUM(max(0,delay(tp,1)-low),26)*100
class Alpha191_052 : public Indicator<Alpha191_052> {
public:
    Alpha191_052(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            tp_.data().reserve(n);
            upm_.data().reserve(n);
            dnm_.data().reserve(n);
        }
        const auto idx = close_.index();
        tp_.forward((high_.data()[idx] + low_.data()[idx] + close_.data()[idx]) / 3.0);
        const auto ti = tp_.size() - 1;
        if (ti < 1uz) [[unlikely]] {
            upm_.forward(0.0); dnm_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        upm_.forward(a191::max(0.0, high_.data()[idx] - tp_.data()[ti - 1]));
        dnm_.forward(a191::max(0.0, tp_.data()[ti - 1] - low_.data()[idx]));

        if (idx + 1 < 27uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ui = upm_.size() - 1, di = dnm_.size() - 1;
        const double su = a191::sum(upm_, ui, 26), sd = a191::sum(dnm_, di, 26);
        line_.forward(sd != 0.0 ? su / sd * 100.0 : 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 27; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> tp_, upm_, dnm_;
};

// Alpha#053: count(close>delay(close,1),12)/12*100
class Alpha191_053 : public Indicator<Alpha191_053> {
public:
    explicit Alpha191_053(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            cond_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        cond_.forward(idx >= 1uz && close_.data()[idx] > close_.data()[idx - 1] ? 1.0 : 0.0);

        if (idx + 1 < 13uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(cond_, cond_.size() - 1, 12) / 12.0 * 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& close_;
    Line<double> cond_;
};

// Alpha#054: (-1*rank(stddev(abs(close-open),5)+(close-open)+correlation(close,open,10)))
class Alpha191_054 : public Indicator<Alpha191_054> {
public:
    Alpha191_054(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            absd_.data().reserve(n);
            raw_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double co = close_.data()[idx] - open_.data()[idx];
        absd_.forward(a191::abs(co));
        double val = a191::NaN;
        if (idx + 1 >= 10uz) {
            const auto ai = absd_.size() - 1;
            const double sd = ai + 1 >= 5uz ? a191::stddev(absd_, ai, 5) : a191::NaN;
            const double corr = a191::correlation(close_, open_, idx, 10);
            if (!std::isnan(sd) && !std::isnan(corr)) val = sd + co + corr;
        }
        raw_.forward(val);

        if (idx + 1 < 262uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = raw_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(raw_, ri, std::min(ri + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> absd_, raw_;
};

// Alpha#055: Williams AD accumulation
class Alpha191_055 : public Indicator<Alpha191_055> {
public:
    Alpha191_055(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            wad_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            wad_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double c = close_.data()[idx], cp = close_.data()[idx - 1];
        const double o = open_.data()[idx], op = open_.data()[idx - 1];
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double num = 16.0 * (c - cp + (c - o) / 2.0 + cp - op);
        const double ahc = a191::abs(h - cp), alc = a191::abs(l - cp), ahl = a191::abs(h - l);
        double denom;
        if (ahc > alc && ahc > ahl)
            denom = ahc + alc / 2.0 + a191::abs(cp - op) / 4.0;
        else if (alc > ahl && alc > ahc)
            denom = alc + ahc / 2.0 + a191::abs(cp - op) / 4.0;
        else
            denom = ahl + a191::abs(cp - op) / 4.0;
        const double mx = a191::max(ahc, alc);
        wad_.forward(denom != 0.0 ? num / denom * mx : 0.0);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(wad_, wad_.size() - 1, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> wad_;
};

// Alpha#056: binary rank comparison
class Alpha191_056 : public Indicator<Alpha191_056> {
public:
    Alpha191_056(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            omin_.data().reserve(n);
            romin_.data().reserve(n);
            hlm_.data().reserve(n);
            shlm_.data().reserve(n);
            mv40_.data().reserve(n);
            smv_.data().reserve(n);
            corr_.data().reserve(n);
            rcp_.data().reserve(n);
        }
        const auto idx = close_.index();
        omin_.forward(open_.data()[idx] - a191::ts_min(open_, idx, std::min(idx + 1, 12uz)));
        const auto oi = omin_.size() - 1;
        romin_.forward(a191::ts_rank(omin_, oi, std::min(oi + 1, 252uz)));

        hlm_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);
        const auto hi = hlm_.size() - 1;
        shlm_.forward(hi + 1 >= 19uz ? a191::sum(hlm_, hi, 19) : a191::NaN);
        mv40_.forward(a191::sma(volume_, idx, std::min(idx + 1, 40uz)));
        const auto mi = mv40_.size() - 1;
        smv_.forward(mi + 1 >= 19uz ? a191::sum(mv40_, mi, 19) : a191::NaN);
        const auto si = shlm_.size() - 1, smi = smv_.size() - 1;
        corr_.forward(std::min(si, smi) + 1 >= 13uz
            ? a191::correlation(shlm_, smv_, std::min(si, smi), 13) : a191::NaN);
        const auto ci = corr_.size() - 1;
        const double rc5 = std::isnan(corr_.data()[ci]) ? a191::NaN
            : std::pow(a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz)), 5.0);
        rcp_.forward(std::isnan(rc5) ? a191::NaN
            : a191::ts_rank(rcp_, rcp_.size(), std::min(rcp_.size() + 1, 252uz)));

        if (idx + 1 < 284uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(romin_.data().back() < rc5 ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 284; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> omin_, romin_, hlm_, shlm_, mv40_, smv_, corr_, rcp_;
};

// Alpha#057: SMA((close-ts_min(low,9))/(ts_max(high,9)-ts_min(low,9))*100, 3, 1)
class Alpha191_057 : public Indicator<Alpha191_057> {
public:
    Alpha191_057(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 9uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double hmax = a191::ts_max(high_, idx, 9);
        const double lmin = a191::ts_min(low_, idx, 9);
        const double denom = hmax - lmin;
        const double val = denom != 0.0 ? (close_.data()[idx] - lmin) / denom * 100.0 : 50.0;

        if (idx + 1 < 12uz) [[unlikely]] {
            ema_.update(val, 3.0, 1.0);
            line_.forward(a191::NaN);
            return;
        }
        ema_.update(val, 3.0, 1.0);
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 12; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    SmaState ema_;
};

// Alpha#058: count(close>delay(close,1),20)/20*100
class Alpha191_058 : public Indicator<Alpha191_058> {
public:
    explicit Alpha191_058(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            cond_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        cond_.forward(idx >= 1uz && close_.data()[idx] > close_.data()[idx - 1] ? 1.0 : 0.0);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(cond_, cond_.size() - 1, 20) / 20.0 * 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    Line<double> cond_;
};

// Alpha#059: SUM(inner,20) — same inner as #003
class Alpha191_059 : public Indicator<Alpha191_059> {
public:
    Alpha191_059(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inner_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { inner_.forward(0.0); line_.forward(a191::NaN); return; }
        const double c = close_.data()[idx], cp = close_.data()[idx - 1];
        double val = 0.0;
        if (c != cp) val = c - (c > cp ? a191::min(low_.data()[idx], cp)
                                       : a191::max(high_.data()[idx], cp));
        inner_.forward(val);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(inner_, inner_.size() - 1, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> inner_;
};

// Alpha#060: SUM(CLV*volume, 20) where CLV = ((close-low)-(high-close))/(high-low)
class Alpha191_060 : public Indicator<Alpha191_060> {
public:
    Alpha191_060(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inner_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double c = close_.data()[idx], v = volume_.data()[idx];
        const double hl = h - l;
        inner_.forward(hl != 0.0 ? ((c - l) - (h - c)) / hl * v : 0.0);

        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(inner_, inner_.size() - 1, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> inner_;
};

// Alpha#061: max(rank(decay_linear(delta(vwap,1),12)), rank(decay_linear(rank(corr(low,mean(vol,80),8)),17)))*(-1)
class Alpha191_061 : public Indicator<Alpha191_061> {
public:
    Alpha191_061(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            dvw_.data().reserve(n);
            dlvw_.data().reserve(n);
            mv80_.data().reserve(n);
            corr_.data().reserve(n);
            rcorr_.data().reserve(n);
            dlrc_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        const auto vi = vwap_.size() - 1;
        dvw_.forward(vi >= 1uz ? vwap_.data()[vi] - vwap_.data()[vi - 1] : a191::NaN);
        const auto di = dvw_.size() - 1;
        dlvw_.forward(di + 1 >= 12uz ? a191::decay_linear(dvw_, di, 12) : a191::NaN);

        mv80_.forward(a191::sma(volume_, idx, std::min(idx + 1, 80uz)));
        const auto mi = mv80_.size() - 1;
        corr_.forward(std::min(idx, mi) + 1 >= 8uz
            ? a191::correlation(low_, mv80_, std::min(idx, mi), 8) : a191::NaN);
        const auto ci = corr_.size() - 1;
        rcorr_.forward(a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz)));
        const auto ri = rcorr_.size() - 1;
        dlrc_.forward(ri + 1 >= 17uz ? a191::decay_linear(rcorr_, ri, 17) : a191::NaN);

        if (idx + 1 < 269uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto dli = dlvw_.size() - 1;
        const double r1 = a191::ts_rank(dlvw_, dli, std::min(dli + 1, 252uz));
        const auto dlri = dlrc_.size() - 1;
        const double r2 = a191::ts_rank(dlrc_, dlri, std::min(dlri + 1, 252uz));
        line_.forward(-1.0 * a191::max(r1, r2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 269; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_, dvw_, dlvw_, mv80_, corr_, rcorr_, dlrc_;
};

// Alpha#062: (-1*correlation(high, rank(adv20), 5))
class Alpha191_062 : public Indicator<Alpha191_062> {
public:
    Alpha191_062(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            adv_.data().reserve(high_.size());
            radv_.data().reserve(high_.size());
        }
        const auto idx = high_.index();
        adv_.forward(a191::adv(volume_, idx, 20));
        const auto ai = adv_.size() - 1;
        radv_.forward(a191::ts_rank(adv_, ai, std::min(ai + 1, 252uz)));

        if (idx + 1 < 257uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = radv_.size() - 1;
        line_.forward(-1.0 * a191::correlation(high_, radv_, std::min(idx, ri), 5));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> adv_, radv_;
};

// Alpha#063: RSI(6)
class Alpha191_063 : public Indicator<Alpha191_063> {
public:
    explicit Alpha191_063(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        double up = 0.0, dn = 0.0;
        if (idx >= 1uz) {
            const double d = close_.data()[idx] - close_.data()[idx - 1];
            up = a191::max(d, 0.0);
            dn = a191::abs(d);
        }
        if (idx + 1 < 7uz) [[unlikely]] {
            if (idx >= 1uz) { ema_up_.update(up, 6.0, 1.0); ema_dn_.update(dn, 6.0, 1.0); }
            line_.forward(a191::NaN);
            return;
        }
        ema_up_.update(up, 6.0, 1.0);
        ema_dn_.update(dn, 6.0, 1.0);
        line_.forward(ema_dn_.value != 0.0 ? ema_up_.value / ema_dn_.value * 100.0 : 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 7; }

private:
    const Line<double>& close_;
    SmaState ema_up_, ema_dn_;
};

// Alpha#064: complex decay_linear argmax correlation
class Alpha191_064 : public Indicator<Alpha191_064> {
public:
    Alpha191_064(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            rvw_.data().reserve(n);
            rv_.data().reserve(n);
            corr1_.data().reserve(n);
            dl1_.data().reserve(n);
            rc_.data().reserve(n);
            mv60_.data().reserve(n);
            rmv_.data().reserve(n);
            corr2_.data().reserve(n);
            am2_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        const auto vi = vwap_.size() - 1;
        rvw_.forward(a191::ts_rank(vwap_, vi, std::min(vi + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto rvi = rvw_.size() - 1, rvi2 = rv_.size() - 1;
        corr1_.forward(std::min(rvi, rvi2) + 1 >= 4uz
            ? a191::correlation(rvw_, rv_, std::min(rvi, rvi2), 4) : a191::NaN);
        const auto c1i = corr1_.size() - 1;
        dl1_.forward(c1i + 1 >= 4uz ? a191::decay_linear(corr1_, c1i, 4) : a191::NaN);

        rc_.forward(a191::ts_rank(close_, idx, std::min(idx + 1, 252uz)));
        mv60_.forward(a191::sma(volume_, idx, std::min(idx + 1, 60uz)));
        const auto mi = mv60_.size() - 1;
        rmv_.forward(a191::ts_rank(mv60_, mi, std::min(mi + 1, 252uz)));
        const auto rci = rc_.size() - 1, rmi = rmv_.size() - 1;
        corr2_.forward(std::min(rci, rmi) + 1 >= 4uz
            ? a191::correlation(rc_, rmv_, std::min(rci, rmi), 4) : a191::NaN);
        const auto c2i = corr2_.size() - 1;
        am2_.forward(c2i + 1 >= 13uz ? a191::ts_argmax(corr2_, c2i, 13) : a191::NaN);
        const auto ami = am2_.size() - 1;
        dl2_.forward(ami + 1 >= 14uz ? a191::decay_linear(am2_, ami, 14) : a191::NaN);

        if (idx + 1 < 283uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto dli1 = dl1_.size() - 1, dli2 = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, dli1, std::min(dli1 + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, dli2, std::min(dli2 + 1, 252uz));
        line_.forward(-1.0 * a191::max(r1, r2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 283; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_, rvw_, rv_, corr1_, dl1_, rc_, mv60_, rmv_, corr2_, am2_, dl2_;
};

// Alpha#065: mean(close,6)/close
class Alpha191_065 : public Indicator<Alpha191_065> {
public:
    explicit Alpha191_065(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 6uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double c = close_.data()[idx];
        line_.forward(c != 0.0 ? a191::sma(close_, idx, 6) / c : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& close_;
};

// Alpha#066: (close-mean(close,6))/mean(close,6)*100
class Alpha191_066 : public Indicator<Alpha191_066> {
public:
    explicit Alpha191_066(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 6uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double m = a191::sma(close_, idx, 6);
        line_.forward(m != 0.0 ? (close_.data()[idx] - m) / m * 100.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& close_;
};

// Alpha#067: RSI(24)
class Alpha191_067 : public Indicator<Alpha191_067> {
public:
    explicit Alpha191_067(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        double up = 0.0, dn = 0.0;
        if (idx >= 1uz) {
            const double d = close_.data()[idx] - close_.data()[idx - 1];
            up = a191::max(d, 0.0);
            dn = a191::abs(d);
        }
        if (idx + 1 < 25uz) [[unlikely]] {
            if (idx >= 1uz) { ema_up_.update(up, 24.0, 1.0); ema_dn_.update(dn, 24.0, 1.0); }
            line_.forward(a191::NaN);
            return;
        }
        ema_up_.update(up, 24.0, 1.0);
        ema_dn_.update(dn, 24.0, 1.0);
        line_.forward(ema_dn_.value != 0.0 ? ema_up_.value / ema_dn_.value * 100.0 : 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 25; }

private:
    const Line<double>& close_;
    SmaState ema_up_, ema_dn_;
};

// Alpha#068: SMA(((H+L)/2-(delay(H,1)+delay(L,1))/2)*(H-L)/volume, 15, 1)
class Alpha191_068 : public Indicator<Alpha191_068> {
public:
    Alpha191_068(const Line<double>& high, const Line<double>& low,
                 const Line<double>& volume)
        : high_(high), low_(low), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        double val = 0.0;
        if (idx >= 1uz) {
            const double midp = (high_.data()[idx - 1] + low_.data()[idx - 1]) / 2.0;
            const double mid = (high_.data()[idx] + low_.data()[idx]) / 2.0;
            const double hl = high_.data()[idx] - low_.data()[idx];
            const double v = volume_.data()[idx];
            val = v != 0.0 ? (mid - midp) * hl / v : 0.0;
        }
        if (idx + 1 < 16uz) [[unlikely]] {
            if (idx >= 1uz) ema_.update(val, 15.0, 1.0);
            line_.forward(a191::NaN);
            return;
        }
        ema_.update(val, 15.0, 1.0);
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 16; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& volume_;
    SmaState ema_;
};

// Alpha#069: DTM/DBM directional
class Alpha191_069 : public Indicator<Alpha191_069> {
public:
    Alpha191_069(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low)
        : open_(open), high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(open_.size());
            dtm_.data().reserve(open_.size());
            dbm_.data().reserve(open_.size());
        }
        const auto idx = open_.index();
        if (idx < 1uz) [[unlikely]] {
            dtm_.forward(0.0); dbm_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double o = open_.data()[idx], op = open_.data()[idx - 1];
        dtm_.forward(o <= op ? 0.0 : a191::max(high_.data()[idx] - o, o - op));
        dbm_.forward(o >= op ? 0.0 : a191::max(o - low_.data()[idx], o - op));

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto di = dtm_.size() - 1, bi = dbm_.size() - 1;
        const double sd = a191::sum(dtm_, di, 20), sb = a191::sum(dbm_, bi, 20);
        if (sd > sb) line_.forward((sd - sb) / sd);
        else if (sd == sb) line_.forward(0.0);
        else line_.forward((sd - sb) / sb);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    Line<double> dtm_, dbm_;
};

// Alpha#070: stddev(amount, 6) where amount = close*volume
class Alpha191_070 : public Indicator<Alpha191_070> {
public:
    Alpha191_070(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            amt_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        amt_.forward(close_.data()[idx] * volume_.data()[idx]);
        if (idx + 1 < 6uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::stddev(amt_, amt_.size() - 1, 6));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> amt_;
};

// Alpha#071: (close-mean(close,24))/mean(close,24)*100
class Alpha191_071 : public Indicator<Alpha191_071> {
public:
    explicit Alpha191_071(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 24uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double m = a191::sma(close_, idx, 24);
        line_.forward(m != 0.0 ? (close_.data()[idx] - m) / m * 100.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 24; }

private:
    const Line<double>& close_;
};

// Alpha#072: SMA((ts_max(H,6)-close)/(ts_max(H,6)-ts_min(L,6))*100, 15, 1)
class Alpha191_072 : public Indicator<Alpha191_072> {
public:
    Alpha191_072(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 6uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double hmax = a191::ts_max(high_, idx, 6);
        const double lmin = a191::ts_min(low_, idx, 6);
        const double denom = hmax - lmin;
        const double val = denom != 0.0 ? (hmax - close_.data()[idx]) / denom * 100.0 : 50.0;
        if (idx + 1 < 21uz) [[unlikely]] {
            ema_.update(val, 15.0, 1.0);
            line_.forward(a191::NaN);
            return;
        }
        ema_.update(val, 15.0, 1.0);
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    SmaState ema_;
};

// Alpha#073: complex decay_linear chain
class Alpha191_073 : public Indicator<Alpha191_073> {
public:
    Alpha191_073(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            corr1_.data().reserve(n);
            dl1_.data().reserve(n);
            dl1b_.data().reserve(n);
            tr1_.data().reserve(n);
            vwap_.data().reserve(n);
            mv30_.data().reserve(n);
            corr2_.data().reserve(n);
            dl2_.data().reserve(n);
            rdl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        corr1_.forward(idx + 1 >= 10uz ? a191::correlation(close_, volume_, idx, 10) : a191::NaN);
        const auto c1i = corr1_.size() - 1;
        dl1_.forward(c1i + 1 >= 16uz ? a191::decay_linear(corr1_, c1i, 16) : a191::NaN);
        const auto d1i = dl1_.size() - 1;
        dl1b_.forward(d1i + 1 >= 4uz ? a191::decay_linear(dl1_, d1i, 4) : a191::NaN);
        const auto d1bi = dl1b_.size() - 1;
        tr1_.forward(d1bi + 1 >= 5uz ? a191::ts_rank(dl1b_, d1bi, 5) : a191::NaN);

        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        mv30_.forward(a191::sma(volume_, idx, std::min(idx + 1, 30uz)));
        const auto vi = vwap_.size() - 1, mi = mv30_.size() - 1;
        corr2_.forward(std::min(vi, mi) + 1 >= 4uz
            ? a191::correlation(vwap_, mv30_, std::min(vi, mi), 4) : a191::NaN);
        const auto c2i = corr2_.size() - 1;
        dl2_.forward(c2i + 1 >= 3uz ? a191::decay_linear(corr2_, c2i, 3) : a191::NaN);
        const auto d2i = dl2_.size() - 1;
        rdl2_.forward(a191::ts_rank(dl2_, d2i, std::min(d2i + 1, 252uz)));

        if (idx + 1 < 287uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(-1.0 * (tr1_.data().back() - rdl2_.data().back()));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 287; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> corr1_, dl1_, dl1b_, tr1_, vwap_, mv30_, corr2_, dl2_, rdl2_;
};

// Alpha#074: rank(corr(SUM(low*0.35+vwap*0.65,20),SUM(mean(vol,40),20),7)) + rank(corr(rank(vwap),rank(volume),6))
class Alpha191_074 : public Indicator<Alpha191_074> {
public:
    Alpha191_074(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            combo_.data().reserve(n);
            scombo_.data().reserve(n);
            mv40_.data().reserve(n);
            smv_.data().reserve(n);
            corr1_.data().reserve(n);
            rcorr1_.data().reserve(n);
            rvw_.data().reserve(n);
            rv_.data().reserve(n);
            corr2_.data().reserve(n);
            rcorr2_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        combo_.forward(low_.data()[idx] * 0.35 + vwap_.data().back() * 0.65);
        const auto ci = combo_.size() - 1;
        scombo_.forward(ci + 1 >= 20uz ? a191::sum(combo_, ci, 20) : a191::NaN);
        mv40_.forward(a191::sma(volume_, idx, std::min(idx + 1, 40uz)));
        const auto mi = mv40_.size() - 1;
        smv_.forward(mi + 1 >= 20uz ? a191::sum(mv40_, mi, 20) : a191::NaN);
        const auto sci = scombo_.size() - 1, smi = smv_.size() - 1;
        corr1_.forward(std::min(sci, smi) + 1 >= 7uz
            ? a191::correlation(scombo_, smv_, std::min(sci, smi), 7) : a191::NaN);
        const auto cr1i = corr1_.size() - 1;
        rcorr1_.forward(a191::ts_rank(corr1_, cr1i, std::min(cr1i + 1, 252uz)));

        const auto vi = vwap_.size() - 1;
        rvw_.forward(a191::ts_rank(vwap_, vi, std::min(vi + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto rvi = rvw_.size() - 1, rvi2 = rv_.size() - 1;
        corr2_.forward(std::min(rvi, rvi2) + 1 >= 6uz
            ? a191::correlation(rvw_, rv_, std::min(rvi, rvi2), 6) : a191::NaN);
        const auto cr2i = corr2_.size() - 1;
        rcorr2_.forward(a191::ts_rank(corr2_, cr2i, std::min(cr2i + 1, 252uz)));

        if (idx + 1 < 279uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(rcorr1_.data().back() + rcorr2_.data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 279; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_, combo_, scombo_, mv40_, smv_, corr1_, rcorr1_;
    Line<double> rvw_, rv_, corr2_, rcorr2_;
};

// Alpha#075: count(close>open, 50)/50
class Alpha191_075 : public Indicator<Alpha191_075> {
public:
    Alpha191_075(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            cond_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        cond_.forward(close_.data()[idx] > open_.data()[idx] ? 1.0 : 0.0);
        if (idx + 1 < 51uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(cond_, cond_.size() - 1, 50) / 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 51; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> cond_;
};

// Alpha#076: stddev(abs(returns)/volume, 20) / mean(abs(returns)/volume, 20)
class Alpha191_076 : public Indicator<Alpha191_076> {
public:
    Alpha191_076(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            rv_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double r = a191::returns(close_, idx);
        const double v = volume_.data()[idx];
        rv_.forward(!std::isnan(r) && v != 0.0 ? a191::abs(r) / v : a191::NaN);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = rv_.size() - 1;
        const double sd = a191::stddev(rv_, ri, 20);
        const double m = a191::sma(rv_, ri, 20);
        line_.forward(m != 0.0 ? sd / m : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> rv_;
};

// Alpha#077: min(rank(decay_linear((H+L)/2-vwap, 20)), rank(decay_linear(corr((H+L)/2,mean(vol,40),3),6)))
class Alpha191_077 : public Indicator<Alpha191_077> {
public:
    Alpha191_077(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            diff_.data().reserve(n);
            dl1_.data().reserve(n);
            hlm_.data().reserve(n);
            mv40_.data().reserve(n);
            corr_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double hlm = (high_.data()[idx] + low_.data()[idx]) / 2.0;
        const double vw = a191::vwap(high_, low_, close_, idx);
        diff_.forward(hlm - vw);
        const auto di = diff_.size() - 1;
        dl1_.forward(di + 1 >= 20uz ? a191::decay_linear(diff_, di, 20) : a191::NaN);

        hlm_.forward(hlm);
        mv40_.forward(a191::sma(volume_, idx, std::min(idx + 1, 40uz)));
        const auto hi = hlm_.size() - 1, mi = mv40_.size() - 1;
        corr_.forward(std::min(hi, mi) + 1 >= 3uz
            ? a191::correlation(hlm_, mv40_, std::min(hi, mi), 3) : a191::NaN);
        const auto ci = corr_.size() - 1;
        dl2_.forward(ci + 1 >= 6uz ? a191::decay_linear(corr_, ci, 6) : a191::NaN);

        if (idx + 1 < 295uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto dli1 = dl1_.size() - 1, dli2 = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, dli1, std::min(dli1 + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, dli2, std::min(dli2 + 1, 252uz));
        line_.forward(a191::min(r1, r2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 295; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> diff_, dl1_, hlm_, mv40_, corr_, dl2_;
};

// Alpha#078: CCI(12)
class Alpha191_078 : public Indicator<Alpha191_078> {
public:
    Alpha191_078(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            tp_.data().reserve(close_.size());
            dev_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        tp_.forward((high_.data()[idx] + low_.data()[idx] + close_.data()[idx]) / 3.0);
        const auto ti = tp_.size() - 1;
        if (ti + 1 < 12uz) [[unlikely]] {
            dev_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double mtp = a191::sma(tp_, ti, 12);
        double mad = 0.0;
        for (std::size_t i = 0; i < 12uz; ++i)
            mad += a191::abs(tp_.data()[ti - 11 + i] - mtp);
        mad /= 12.0;
        dev_.forward(mad);

        if (idx + 1 < 24uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double md = a191::sma(dev_, dev_.size() - 1, 12);
        line_.forward(md != 0.0 ? (tp_.data()[ti] - mtp) / (0.015 * md) : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 24; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> tp_, dev_;
};

// Alpha#079: RSI(12)
class Alpha191_079 : public Indicator<Alpha191_079> {
public:
    explicit Alpha191_079(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        double up = 0.0, dn = 0.0;
        if (idx >= 1uz) {
            const double d = close_.data()[idx] - close_.data()[idx - 1];
            up = a191::max(d, 0.0); dn = a191::abs(d);
        }
        if (idx + 1 < 13uz) [[unlikely]] {
            if (idx >= 1uz) { ema_up_.update(up, 12.0, 1.0); ema_dn_.update(dn, 12.0, 1.0); }
            line_.forward(a191::NaN); return;
        }
        ema_up_.update(up, 12.0, 1.0); ema_dn_.update(dn, 12.0, 1.0);
        line_.forward(ema_dn_.value != 0.0 ? ema_up_.value / ema_dn_.value * 100.0 : 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& close_;
    SmaState ema_up_, ema_dn_;
};

// Alpha#080: (volume-delay(volume,5))/delay(volume,5)*100
class Alpha191_080 : public Indicator<Alpha191_080> {
public:
    explicit Alpha191_080(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(volume_.size()); }
        const auto idx = volume_.index();
        if (idx < 5uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double vp = volume_.data()[idx - 5];
        line_.forward(vp != 0.0 ? (volume_.data()[idx] - vp) / vp * 100.0 : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#081: SMA(volume,21,2) — recursive EMA of volume
// Inputs: V   Warmup: 21
// ============================================================================
class Alpha191_081 : public Indicator<Alpha191_081> {
public:
    explicit Alpha191_081(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(volume_.size()); }
        const auto idx = volume_.index();
        ema_.update(volume_.data()[idx], 21.0, 2.0);
        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& volume_;
    SmaState ema_;
};

// ============================================================================
// Alpha#082: RSI(6) rescaled: SMA(max(close-delay(close,1),0),6,1)/SMA(abs(close-delay(close,1)),6,1)*100
// Inputs: C   Warmup: 7
// ============================================================================
class Alpha191_082 : public Indicator<Alpha191_082> {
public:
    explicit Alpha191_082(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        double up = 0.0, total = 0.0;
        if (idx >= 1uz) {
            const double d = close_.data()[idx] - close_.data()[idx - 1];
            up = a191::max(d, 0.0);
            total = a191::abs(d);
        }
        if (idx + 1 < 7uz) [[unlikely]] {
            if (idx >= 1uz) { ema_up_.update(up, 6.0, 1.0); ema_abs_.update(total, 6.0, 1.0); }
            line_.forward(a191::NaN);
            return;
        }
        ema_up_.update(up, 6.0, 1.0);
        ema_abs_.update(total, 6.0, 1.0);
        line_.forward(ema_abs_.value != 0.0 ? ema_up_.value / ema_abs_.value * 100.0 : 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 7; }

private:
    const Line<double>& close_;
    SmaState ema_up_, ema_abs_;
};

// ============================================================================
// Alpha#083: (-1*rank(covariance(rank(high), rank(volume), 5)))
// Same as #016. Inputs: H, V   Warmup: 257
// ============================================================================
class Alpha191_083 : public Indicator<Alpha191_083> {
public:
    Alpha191_083(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            rh_.data().reserve(n);
            rv_.data().reserve(n);
            cov_.data().reserve(n);
        }
        const auto idx = high_.index();
        rh_.forward(a191::ts_rank(high_, idx, std::min(idx + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto ci = rh_.size() - 1;
        cov_.forward(ci + 1 >= 5uz ? a191::covariance(rh_, rv_, ci, 5) : a191::NaN);

        if (idx + 1 < 257uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto covi = cov_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(cov_, covi, std::min(covi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> rh_, rv_, cov_;
};

// ============================================================================
// Alpha#084: SUM(IF(close>delay(close,1),volume,IF(close<delay(close,1),-volume,0)),20)
// Inputs: C, V   Warmup: 21
// ============================================================================
class Alpha191_084 : public Indicator<Alpha191_084> {
public:
    Alpha191_084(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            sv_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double val = 0.0;
        if (idx >= 1uz) {
            const double c = close_.data()[idx], cp = close_.data()[idx - 1];
            if (c > cp) val = volume_.data()[idx];
            else if (c < cp) val = -volume_.data()[idx];
        }
        sv_.forward(val);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(sv_, sv_.size() - 1, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> sv_;
};

// ============================================================================
// Alpha#085: ts_rank(volume/adv20,20)*ts_rank(-delta(close,7),8)
// Same as #043. Inputs: C, V   Warmup: 27
// ============================================================================
class Alpha191_085 : public Indicator<Alpha191_085> {
public:
    Alpha191_085(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            vr_.data().reserve(close_.size());
            nd7_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double adv20 = a191::adv(volume_, idx, 20);
        vr_.forward(adv20 != 0.0 ? volume_.data()[idx] / adv20 : a191::NaN);
        nd7_.forward(idx >= 7uz ? -1.0 * a191::delta(close_, idx, 7) : a191::NaN);

        if (idx + 1 < 27uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto vi = vr_.size() - 1;
        const auto ni = nd7_.size() - 1;
        line_.forward(a191::ts_rank(vr_, vi, 20) * a191::ts_rank(nd7_, ni, 8));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 27; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vr_, nd7_;
};

// ============================================================================
// Alpha#086: conditional on delay(close,20) momentum slope
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha191_086 : public Indicator<Alpha191_086> {
public:
    explicit Alpha191_086(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double sma20 = a191::sma(close_, idx, 20);
        const double c = close_.data()[idx];
        if (sma20 < c)
            line_.forward(-1.0 * a191::delta(close_, idx, 5));
        else
            line_.forward(a191::delta(close_, idx, 5));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#087: rank(decay_linear(delta(vwap,4),7))+ts_rank(decay_linear(((lo*0.9+hi*0.1-vwap)/(open-0.5*(hi+lo))),11),7)
// Inputs: O, H, L, C   Warmup: 270
// ============================================================================
class Alpha191_087 : public Indicator<Alpha191_087> {
public:
    Alpha191_087(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            dvw_.data().reserve(n);
            dl1_.data().reserve(n);
            inner_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        const auto vi = vwap_.size() - 1;
        dvw_.forward(vi >= 4uz ? vwap_.data()[vi] - vwap_.data()[vi - 4] : a191::NaN);
        const auto di = dvw_.size() - 1;
        dl1_.forward(di + 1 >= 7uz ? a191::decay_linear(dvw_, di, 7) : a191::NaN);

        const double vw = vwap_.data().back();
        const double o = open_.data()[idx], h = high_.data()[idx], l = low_.data()[idx];
        const double denom = o - 0.5 * (h + l);
        inner_.forward(denom != 0.0 ? (l * 0.9 + h * 0.1 - vw) / denom : a191::NaN);
        const auto ii = inner_.size() - 1;
        dl2_.forward(ii + 1 >= 11uz ? a191::decay_linear(inner_, ii, 11) : a191::NaN);

        if (idx + 1 < 270uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto dli1 = dl1_.size() - 1, dli2 = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, dli1, std::min(dli1 + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, dli2, 7);
        line_.forward(r1 + r2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 270; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> vwap_, dvw_, dl1_, inner_, dl2_;
};

// ============================================================================
// Alpha#088: (close-delay(close,20))/delay(close,20)*100
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha191_088 : public Indicator<Alpha191_088> {
public:
    explicit Alpha191_088(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double cp = close_.data()[idx - 20];
        line_.forward(cp != 0.0 ? (close_.data()[idx] - cp) / cp * 100.0 : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#089: 2*(SMA(close,13,2)-SMA(close,27,2)-SMA(SMA(close,13,2)-SMA(close,27,2),10,2))
// MACD-like expression. Inputs: C   Warmup: 50
// ============================================================================
class Alpha191_089 : public Indicator<Alpha191_089> {
public:
    explicit Alpha191_089(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double c = close_.data()[idx];
        ema13_.update(c, 13.0, 2.0);
        ema27_.update(c, 27.0, 2.0);
        const double diff = ema13_.value - ema27_.value;
        ema_diff_.update(diff, 10.0, 2.0);

        if (idx + 1 < 50uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(2.0 * (diff - ema_diff_.value));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 50; }

private:
    const Line<double>& close_;
    SmaState ema13_, ema27_, ema_diff_;
};

// ============================================================================
// Alpha#090: rank(corr(rank(vwap), rank(volume), 5)) * (-1)
// Inputs: H, L, C, V   Warmup: 257
// ============================================================================
class Alpha191_090 : public Indicator<Alpha191_090> {
public:
    Alpha191_090(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            rvw_.data().reserve(n);
            rv_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        const auto vi = vwap_.size() - 1;
        rvw_.forward(a191::ts_rank(vwap_, vi, std::min(vi + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto rvi = rvw_.size() - 1, rvi2 = rv_.size() - 1;
        corr_.forward(std::min(rvi, rvi2) + 1 >= 5uz
            ? a191::correlation(rvw_, rv_, std::min(rvi, rvi2), 5) : a191::NaN);

        if (idx + 1 < 257uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ci = corr_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_, rvw_, rv_, corr_;
};

// ============================================================================
// Alpha#091: (rank(close-max(close,5))*rank(corr(mean(volume,40),low,5)))*(-1)
// Inputs: L, C, V   Warmup: 297
// ============================================================================
class Alpha191_091 : public Indicator<Alpha191_091> {
public:
    Alpha191_091(const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            cdiff_.data().reserve(n);
            mv40_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();
        cdiff_.forward(close_.data()[idx] - a191::ts_max(close_, idx, std::min(idx + 1, 5uz)));
        mv40_.forward(a191::sma(volume_, idx, std::min(idx + 1, 40uz)));
        const auto mi = mv40_.size() - 1;
        corr_.forward(std::min(idx, mi) + 1 >= 5uz
            ? a191::correlation(mv40_, low_, std::min(mi, idx), 5) : a191::NaN);

        if (idx + 1 < 297uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto cdi = cdiff_.size() - 1, ci = corr_.size() - 1;
        const double r1 = a191::ts_rank(cdiff_, cdi, std::min(cdi + 1, 252uz));
        const double r2 = a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz));
        line_.forward(-1.0 * r1 * r2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 297; }

private:
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> cdiff_, mv40_, corr_;
};

// ============================================================================
// Alpha#092: max(rank(decay_linear(delta(close*0.35+vwap*0.65,2),3)),
//                ts_rank(decay_linear(abs(corr(mean(vol,180),close,13)),5),15))*(-1)
// Inputs: H, L, C, V   Warmup: 283
// ============================================================================
class Alpha191_092 : public Indicator<Alpha191_092> {
public:
    Alpha191_092(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            combo_.data().reserve(n);
            dcombo_.data().reserve(n);
            dl1_.data().reserve(n);
            mv180_.data().reserve(n);
            corr_.data().reserve(n);
            acorr_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        combo_.forward(close_.data()[idx] * 0.35 + vwap_.data().back() * 0.65);
        const auto ci = combo_.size() - 1;
        dcombo_.forward(ci >= 2uz ? combo_.data()[ci] - combo_.data()[ci - 2] : a191::NaN);
        const auto dci = dcombo_.size() - 1;
        dl1_.forward(dci + 1 >= 3uz ? a191::decay_linear(dcombo_, dci, 3) : a191::NaN);

        mv180_.forward(a191::sma(volume_, idx, std::min(idx + 1, 180uz)));
        const auto mi = mv180_.size() - 1;
        corr_.forward(std::min(mi, idx) + 1 >= 13uz
            ? a191::correlation(mv180_, close_, std::min(mi, idx), 13) : a191::NaN);
        const auto cri = corr_.size() - 1;
        acorr_.forward(std::isnan(corr_.data()[cri]) ? a191::NaN : a191::abs(corr_.data()[cri]));
        const auto ai = acorr_.size() - 1;
        dl2_.forward(ai + 1 >= 5uz ? a191::decay_linear(acorr_, ai, 5) : a191::NaN);

        if (idx + 1 < 283uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto dli1 = dl1_.size() - 1, dli2 = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, dli1, std::min(dli1 + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, dli2, 15);
        line_.forward(-1.0 * a191::max(r1, r2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 283; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_, combo_, dcombo_, dl1_, mv180_, corr_, acorr_, dl2_;
};

// ============================================================================
// Alpha#093: SUM(IF(open>=delay(open,1),0,max(open-low,open-delay(open,1))),20)
// Inputs: O, L   Warmup: 21
// ============================================================================
class Alpha191_093 : public Indicator<Alpha191_093> {
public:
    Alpha191_093(const Line<double>& open, const Line<double>& low)
        : open_(open), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(open_.size());
            inner_.data().reserve(open_.size());
        }
        const auto idx = open_.index();
        double val = 0.0;
        if (idx >= 1uz) {
            const double o = open_.data()[idx], op = open_.data()[idx - 1];
            if (o < op) val = a191::max(o - low_.data()[idx], o - op);
        }
        inner_.forward(val);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(inner_, inner_.size() - 1, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& open_;
    const Line<double>& low_;
    Line<double> inner_;
};

// ============================================================================
// Alpha#094: SUM(IF(close>delay(close,1),volume,IF(close<delay(close,1),-volume,0)),30)
// Inputs: C, V   Warmup: 31
// ============================================================================
class Alpha191_094 : public Indicator<Alpha191_094> {
public:
    Alpha191_094(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            sv_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double val = 0.0;
        if (idx >= 1uz) {
            const double c = close_.data()[idx], cp = close_.data()[idx - 1];
            if (c > cp) val = volume_.data()[idx];
            else if (c < cp) val = -volume_.data()[idx];
        }
        sv_.forward(val);

        if (idx + 1 < 31uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::sum(sv_, sv_.size() - 1, 30));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 31; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> sv_;
};

// ============================================================================
// Alpha#095: stddev(amount,20) where amount = close * volume
// Inputs: C, V   Warmup: 20
// ============================================================================
class Alpha191_095 : public Indicator<Alpha191_095> {
public:
    Alpha191_095(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            amt_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        amt_.forward(close_.data()[idx] * volume_.data()[idx]);
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::stddev(amt_, amt_.size() - 1, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> amt_;
};

// ============================================================================
// Alpha#096: SMA(SMA((close-ts_min(low,9))/(ts_max(high,9)-ts_min(low,9))*100, 3, 1), 3, 1)
// Inputs: H, L, C   Warmup: 15
// ============================================================================
class Alpha191_096 : public Indicator<Alpha191_096> {
public:
    Alpha191_096(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 9uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double hmax = a191::ts_max(high_, idx, 9);
        const double lmin = a191::ts_min(low_, idx, 9);
        const double denom = hmax - lmin;
        const double val = denom != 0.0 ? (close_.data()[idx] - lmin) / denom * 100.0 : 50.0;
        ema1_.update(val, 3.0, 1.0);
        ema2_.update(ema1_.value, 3.0, 1.0);

        if (idx + 1 < 15uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema2_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 15; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    SmaState ema1_, ema2_;
};

// ============================================================================
// Alpha#097: stddev(volume, 10)
// Inputs: V   Warmup: 10
// ============================================================================
class Alpha191_097 : public Indicator<Alpha191_097> {
public:
    explicit Alpha191_097(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(volume_.size()); }
        const auto idx = volume_.index();
        if (idx + 1 < 10uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::stddev(volume_, idx, 10));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#098: decay_linear(correlation(vwap,SUM(mean(vol,5),26),5),7)
// Inputs: H, L, C, V   Warmup: 38
// ============================================================================
class Alpha191_098 : public Indicator<Alpha191_098> {
public:
    Alpha191_098(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            mv5_.data().reserve(n);
            smv_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        mv5_.forward(a191::sma(volume_, idx, std::min(idx + 1, 5uz)));
        const auto mi = mv5_.size() - 1;
        smv_.forward(mi + 1 >= 26uz ? a191::sum(mv5_, mi, 26) : a191::NaN);
        const auto vi = vwap_.size() - 1, si = smv_.size() - 1;
        corr_.forward(std::min(vi, si) + 1 >= 5uz
            ? a191::correlation(vwap_, smv_, std::min(vi, si), 5) : a191::NaN);

        const auto ci = corr_.size() - 1;
        if (idx + 1 < 38uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ci + 1 >= 7uz ? a191::decay_linear(corr_, ci, 7) : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 38; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_, mv5_, smv_, corr_;
};

// ============================================================================
// Alpha#099: (-1*rank(covariance(rank(close), rank(volume), 5)))
// Same as #013. Inputs: C, V   Warmup: 257
// ============================================================================
class Alpha191_099 : public Indicator<Alpha191_099> {
public:
    Alpha191_099(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            rc_.data().reserve(n);
            rv_.data().reserve(n);
            cov_.data().reserve(n);
        }
        const auto idx = close_.index();
        rc_.forward(a191::ts_rank(close_, idx, std::min(idx + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto ci = rc_.size() - 1;
        cov_.forward(ci + 1 >= 5uz ? a191::covariance(rc_, rv_, ci, 5) : a191::NaN);

        if (idx + 1 < 257uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto covi = cov_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(cov_, covi, std::min(covi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 257; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> rc_, rv_, cov_;
};

// ============================================================================
// Alpha#100: stddev(volume, 20)
// Inputs: V   Warmup: 20
// ============================================================================
class Alpha191_100 : public Indicator<Alpha191_100> {
public:
    explicit Alpha191_100(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(volume_.size()); }
        const auto idx = volume_.index();
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::stddev(volume_, idx, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#101: (close-open)/((high-low)+0.001)
// Inputs: O, H, L, C   Warmup: 1
// ============================================================================
class Alpha191_101 : public Indicator<Alpha191_101> {
public:
    Alpha191_101(const Line<double>& open, const Line<double>& high,
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
// Alpha#102: SMA(max(volume-delay(volume,1),0),6,1)/SMA(abs(volume-delay(volume,1)),6,1)*100
// Inputs: V   Warmup: 7
// ============================================================================
class Alpha191_102 : public Indicator<Alpha191_102> {
public:
    explicit Alpha191_102(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(volume_.size()); }
        const auto idx = volume_.index();
        double up = 0.0, total = 0.0;
        if (idx >= 1uz) {
            const double d = volume_.data()[idx] - volume_.data()[idx - 1];
            up = a191::max(d, 0.0);
            total = a191::abs(d);
        }
        if (idx + 1 < 7uz) [[unlikely]] {
            if (idx >= 1uz) { ema_up_.update(up, 6.0, 1.0); ema_abs_.update(total, 6.0, 1.0); }
            line_.forward(a191::NaN);
            return;
        }
        ema_up_.update(up, 6.0, 1.0);
        ema_abs_.update(total, 6.0, 1.0);
        line_.forward(ema_abs_.value != 0.0 ? ema_up_.value / ema_abs_.value * 100.0 : 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 7; }

private:
    const Line<double>& volume_;
    SmaState ema_up_, ema_abs_;
};

// ============================================================================
// Alpha#103: lowday(low, 20) — bars since lowest low in 20 bars
// Inputs: L   Warmup: 20
// ============================================================================
class Alpha191_103 : public Indicator<Alpha191_103> {
public:
    explicit Alpha191_103(const Line<double>& low) : low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(low_.size()); }
        const auto idx = low_.index();
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::lowday(low_, idx, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& low_;
};

// ============================================================================
// Alpha#104: -1*delta(corr(high,volume,5),5)*rank(stddev(close,20))
// Inputs: H, C, V   Warmup: 262
// ============================================================================
class Alpha191_104 : public Indicator<Alpha191_104> {
public:
    Alpha191_104(const Line<double>& high, const Line<double>& close,
                 const Line<double>& volume)
        : high_(high), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            corr_.data().reserve(n);
            sd_.data().reserve(n);
        }
        const auto idx = close_.index();
        corr_.forward(idx + 1 >= 5uz ? a191::correlation(high_, volume_, idx, 5) : a191::NaN);
        sd_.forward(idx + 1 >= 20uz ? a191::stddev(close_, idx, 20) : a191::NaN);

        if (idx + 1 < 262uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ci = corr_.size() - 1;
        const double dcorr = ci >= 5uz && !std::isnan(corr_.data()[ci]) && !std::isnan(corr_.data()[ci - 5])
            ? corr_.data()[ci] - corr_.data()[ci - 5] : a191::NaN;
        const auto si = sd_.size() - 1;
        const double rsd = a191::ts_rank(sd_, si, std::min(si + 1, 252uz));
        line_.forward(std::isnan(dcorr) ? a191::NaN : -1.0 * dcorr * rsd);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& high_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> corr_, sd_;
};

// ============================================================================
// Alpha#105: -1*corr(rank(open), rank(volume), 10)
// Inputs: O, V   Warmup: 262
// ============================================================================
class Alpha191_105 : public Indicator<Alpha191_105> {
public:
    Alpha191_105(const Line<double>& open, const Line<double>& volume)
        : open_(open), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = open_.size();
            reserve_output(n);
            ro_.data().reserve(n);
            rv_.data().reserve(n);
        }
        const auto idx = open_.index();
        ro_.forward(a191::ts_rank(open_, idx, std::min(idx + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));

        if (idx + 1 < 262uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto roi = ro_.size() - 1, rvi = rv_.size() - 1;
        line_.forward(-1.0 * a191::correlation(ro_, rv_, std::min(roi, rvi), 10));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 262; }

private:
    const Line<double>& open_;
    const Line<double>& volume_;
    Line<double> ro_, rv_;
};

// ============================================================================
// Alpha#106: close - delay(close, 20)
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha191_106 : public Indicator<Alpha191_106> {
public:
    explicit Alpha191_106(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(close_.data()[idx] - close_.data()[idx - 20]);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#107: (-1*rank(open-delay(high,1)))*rank(open-delay(close,1))*rank(open-delay(low,1))
// Same as #020. Inputs: O, H, L, C   Warmup: 253
// ============================================================================
class Alpha191_107 : public Indicator<Alpha191_107> {
public:
    Alpha191_107(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = open_.size();
            reserve_output(n);
            a_.data().reserve(n);
            b_.data().reserve(n);
            c_.data().reserve(n);
        }
        const auto idx = open_.index();
        const double o = open_.data()[idx];
        a_.forward(idx >= 1uz ? o - high_.data()[idx - 1] : a191::NaN);
        b_.forward(idx >= 1uz ? o - close_.data()[idx - 1] : a191::NaN);
        c_.forward(idx >= 1uz ? o - low_.data()[idx - 1] : a191::NaN);

        if (idx + 1 < 253uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ai = a_.size() - 1, bi = b_.size() - 1, ci = c_.size() - 1;
        const double ra = a191::ts_rank(a_, ai, std::min(ai + 1, 252uz));
        const double rb = a191::ts_rank(b_, bi, std::min(bi + 1, 252uz));
        const double rc = a191::ts_rank(c_, ci, std::min(ci + 1, 252uz));
        line_.forward(-1.0 * ra * rb * rc);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> a_, b_, c_;
};

// ============================================================================
// Alpha#108: rank(high-min(high,2))^rank(corr(vwap,mean(volume,120),6))*(-1)
// Inputs: H, L, C, V   Warmup: 278
// ============================================================================
class Alpha191_108 : public Indicator<Alpha191_108> {
public:
    Alpha191_108(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            hdiff_.data().reserve(n);
            vwap_.data().reserve(n);
            mv120_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();
        hdiff_.forward(high_.data()[idx] - a191::ts_min(high_, idx, std::min(idx + 1, 2uz)));
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        mv120_.forward(a191::sma(volume_, idx, std::min(idx + 1, 120uz)));
        const auto vi = vwap_.size() - 1, mi = mv120_.size() - 1;
        corr_.forward(std::min(vi, mi) + 1 >= 6uz
            ? a191::correlation(vwap_, mv120_, std::min(vi, mi), 6) : a191::NaN);

        if (idx + 1 < 278uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto hi = hdiff_.size() - 1, ci = corr_.size() - 1;
        const double rh = a191::ts_rank(hdiff_, hi, std::min(hi + 1, 252uz));
        const double rc = a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz));
        line_.forward(-1.0 * std::pow(rh, rc));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 278; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> hdiff_, vwap_, mv120_, corr_;
};

// ============================================================================
// Alpha#109: SMA(high-low, 10, 2) / SMA(SMA(high-low, 10, 2), 10, 2)
// Inputs: H, L   Warmup: 20
// ============================================================================
class Alpha191_109 : public Indicator<Alpha191_109> {
public:
    Alpha191_109(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        const double hl = high_.data()[idx] - low_.data()[idx];
        ema1_.update(hl, 10.0, 2.0);
        ema2_.update(ema1_.value, 10.0, 2.0);

        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema2_.value != 0.0 ? ema1_.value / ema2_.value : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    SmaState ema1_, ema2_;
};

// ============================================================================
// Alpha#110: SUM(max(0,high-delay(close,1)),20)/SUM(max(0,delay(close,1)-low),20)*100
// Inputs: H, L, C   Warmup: 21
// ============================================================================
class Alpha191_110 : public Indicator<Alpha191_110> {
public:
    Alpha191_110(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            upm_.data().reserve(close_.size());
            dnm_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            upm_.forward(0.0); dnm_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double cp = close_.data()[idx - 1];
        upm_.forward(a191::max(0.0, high_.data()[idx] - cp));
        dnm_.forward(a191::max(0.0, cp - low_.data()[idx]));

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ui = upm_.size() - 1, di = dnm_.size() - 1;
        const double su = a191::sum(upm_, ui, 20), sd = a191::sum(dnm_, di, 20);
        line_.forward(sd != 0.0 ? su / sd * 100.0 : 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> upm_, dnm_;
};

// ============================================================================
// Alpha#111: SMA(vol*((close-low)-(high-close))/(high-low), 11, 2)-SMA(vol*((close-low)-(high-close))/(high-low), 4, 2)
// Inputs: H, L, C, V   Warmup: 12
// ============================================================================
class Alpha191_111 : public Indicator<Alpha191_111> {
public:
    Alpha191_111(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double c = close_.data()[idx], v = volume_.data()[idx];
        const double hl = h - l;
        const double clv = hl != 0.0 ? ((c - l) - (h - c)) / hl * v : 0.0;
        ema11_.update(clv, 11.0, 2.0);
        ema4_.update(clv, 4.0, 2.0);

        if (idx + 1 < 12uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema11_.value - ema4_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 12; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    SmaState ema11_, ema4_;
};

// ============================================================================
// Alpha#112: (SUM(IF(close-delay(close,1)>0,close-delay(close,1),0),12)
//            - SUM(IF(close-delay(close,1)<0,abs(close-delay(close,1)),0),12))
//            / (SUM(IF(close-delay(close,1)>0,close-delay(close,1),0),12)
//            + SUM(IF(close-delay(close,1)<0,abs(close-delay(close,1)),0),12)) * 100
// Inputs: C   Warmup: 13
// ============================================================================
class Alpha191_112 : public Indicator<Alpha191_112> {
public:
    explicit Alpha191_112(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            up_.data().reserve(close_.size());
            dn_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            up_.forward(0.0); dn_.forward(0.0);
            line_.forward(a191::NaN);
            return;
        }
        const double d = close_.data()[idx] - close_.data()[idx - 1];
        up_.forward(d > 0.0 ? d : 0.0);
        dn_.forward(d < 0.0 ? a191::abs(d) : 0.0);

        if (idx + 1 < 13uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ui = up_.size() - 1, di = dn_.size() - 1;
        const double su = a191::sum(up_, ui, 12), sd = a191::sum(dn_, di, 12);
        const double total = su + sd;
        line_.forward(total != 0.0 ? (su - sd) / total * 100.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& close_;
    Line<double> up_, dn_;
};

// ============================================================================
// Alpha#113: -rank(SUM(delay(close,5),20)/20) * corr(close,volume,2) * rank(corr(SUM(close,5),SUM(close,20),2))
// Same as #045. Inputs: C, V   Warmup: 277
// ============================================================================
class Alpha191_113 : public Indicator<Alpha191_113> {
public:
    Alpha191_113(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            dsum_.data().reserve(n);
            rdsum_.data().reserve(n);
            s5_.data().reserve(n);
            s20_.data().reserve(n);
            corr2_.data().reserve(n);
            rcorr2_.data().reserve(n);
        }
        const auto idx = close_.index();
        double ds = a191::NaN;
        if (idx >= 5uz && idx + 1 >= 25uz) {
            double s = 0.0;
            for (std::size_t i = 0; i < 20; ++i) s += close_.data()[idx - 5 - i];
            ds = s / 20.0;
        }
        dsum_.forward(ds);
        const auto dsi = dsum_.size() - 1;
        rdsum_.forward(a191::ts_rank(dsum_, dsi, std::min(dsi + 1, 252uz)));

        s5_.forward(a191::sum(close_, idx, std::min(idx + 1, 5uz)));
        s20_.forward(a191::sum(close_, idx, std::min(idx + 1, 20uz)));
        const auto s5i = s5_.size() - 1, s20i = s20_.size() - 1;
        corr2_.forward(std::min(s5i, s20i) + 1 >= 2uz ? a191::correlation(s5_, s20_, std::min(s5i, s20i), 2) : a191::NaN);
        const auto c2i = corr2_.size() - 1;
        rcorr2_.forward(a191::ts_rank(corr2_, c2i, std::min(c2i + 1, 252uz)));

        if (idx + 1 < 277uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double corr_cv = a191::correlation(close_, volume_, idx, 2);
        line_.forward(-1.0 * rdsum_.data().back() * corr_cv * rcorr2_.data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 277; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> dsum_, rdsum_, s5_, s20_, corr2_, rcorr2_;
};

// ============================================================================
// Alpha#114: rank(delay(((high-low)/(SUM(close,5)/5)),2))*rank(rank(volume))
//            / (((high-low)/(SUM(close,5)/5))/(vwap-close))
// Inputs: H, L, C, V   Warmup: 259
// ============================================================================
class Alpha191_114 : public Indicator<Alpha191_114> {
public:
    Alpha191_114(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ratio_.data().reserve(n);
            rv_.data().reserve(n);
            rrv_.data().reserve(n);
        }
        const auto idx = close_.index();
        double r = a191::NaN;
        if (idx + 1 >= 5uz) {
            const double s5m = a191::sma(close_, idx, 5);
            r = s5m != 0.0 ? (high_.data()[idx] - low_.data()[idx]) / s5m : a191::NaN;
        }
        ratio_.forward(r);
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto rvi = rv_.size() - 1;
        rrv_.forward(a191::ts_rank(rv_, rvi, std::min(rvi + 1, 252uz)));

        if (idx + 1 < 259uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = ratio_.size() - 1;
        const double rdr = a191::ts_rank(ratio_, ri - 2, std::min(ri - 1, 252uz));
        const double vw = a191::vwap(high_, low_, close_, idx);
        const double vc = vw - close_.data()[idx];
        double cur_ratio = std::isnan(ratio_.data()[ri]) ? a191::NaN : ratio_.data()[ri];
        double denom = vc != 0.0 && !std::isnan(cur_ratio) ? cur_ratio / vc : a191::NaN;
        if (std::isnan(denom) || denom == 0.0) { line_.forward(a191::NaN); return; }
        line_.forward(rdr * rrv_.data().back() / denom);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 259; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> ratio_, rv_, rrv_;
};

// ============================================================================
// Alpha#115: rank(corr(high*0.9+close*0.1, mean(volume,30), 10))
//            ^ rank(corr(ts_rank((high+low)/2,4), ts_rank(volume,10), 7))
// Inputs: H, L, C, V   Warmup: 272
// ============================================================================
class Alpha191_115 : public Indicator<Alpha191_115> {
public:
    Alpha191_115(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            hc_.data().reserve(n);
            mv30_.data().reserve(n);
            corr1_.data().reserve(n);
            hlm_.data().reserve(n);
            trh_.data().reserve(n);
            trv_.data().reserve(n);
            corr2_.data().reserve(n);
        }
        const auto idx = close_.index();
        hc_.forward(high_.data()[idx] * 0.9 + close_.data()[idx] * 0.1);
        mv30_.forward(a191::sma(volume_, idx, std::min(idx + 1, 30uz)));
        const auto hci = hc_.size() - 1, mi = mv30_.size() - 1;
        corr1_.forward(std::min(hci, mi) + 1 >= 10uz
            ? a191::correlation(hc_, mv30_, std::min(hci, mi), 10) : a191::NaN);

        hlm_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);
        const auto hi = hlm_.size() - 1;
        trh_.forward(a191::ts_rank(hlm_, hi, std::min(hi + 1, 4uz)));
        trv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 10uz)));
        const auto tri = trh_.size() - 1, tvi = trv_.size() - 1;
        corr2_.forward(std::min(tri, tvi) + 1 >= 7uz
            ? a191::correlation(trh_, trv_, std::min(tri, tvi), 7) : a191::NaN);

        if (idx + 1 < 272uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto c1i = corr1_.size() - 1, c2i = corr2_.size() - 1;
        const double r1 = a191::ts_rank(corr1_, c1i, std::min(c1i + 1, 252uz));
        const double r2 = a191::ts_rank(corr2_, c2i, std::min(c2i + 1, 252uz));
        line_.forward(std::pow(r1, r2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 272; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> hc_, mv30_, corr1_, hlm_, trh_, trv_, corr2_;
};

// ============================================================================
// Alpha#116: regbeta(close, sequence, 20)
// Uses sequence 1..20 as x-axis for rolling OLS beta (slope).
// Inputs: C   Warmup: 20
// ============================================================================
class Alpha191_116 : public Indicator<Alpha191_116> {
public:
    explicit Alpha191_116(const Line<double>& close) : close_(close) {
        for (std::size_t i = 0; i < 20; ++i) seq_line_.forward(static_cast<double>(i + 1));
    }

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(a191::regbeta(close_, seq_line_, idx, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& close_;
    Line<double> seq_line_;
};

// ============================================================================
// Alpha#117: ts_rank(volume,32)*(1-ts_rank(close+high-low,16))*(1-ts_rank(returns,32))
// Same as #035. Inputs: H, L, C, V   Warmup: 33
// ============================================================================
class Alpha191_117 : public Indicator<Alpha191_117> {
public:
    Alpha191_117(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            chl_.data().reserve(n);
            ret_.data().reserve(n);
        }
        const auto idx = close_.index();
        chl_.forward(close_.data()[idx] + high_.data()[idx] - low_.data()[idx]);
        ret_.forward(a191::returns(close_, idx));

        if (idx + 1 < 33uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double trv = a191::ts_rank(volume_, idx, 32);
        const auto ci = chl_.size() - 1;
        const double trc = a191::ts_rank(chl_, ci, 16);
        const auto ri = ret_.size() - 1;
        const double trr = a191::ts_rank(ret_, ri, 32);
        line_.forward(trv * (1.0 - trc) * (1.0 - trr));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 33; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> chl_, ret_;
};

// ============================================================================
// Alpha#118: SUM(high-open, 20) / SUM(open-low, 20) * 100
// Inputs: O, H, L   Warmup: 20
// ============================================================================
class Alpha191_118 : public Indicator<Alpha191_118> {
public:
    Alpha191_118(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low)
        : open_(open), high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(open_.size());
            ho_.data().reserve(open_.size());
            ol_.data().reserve(open_.size());
        }
        const auto idx = open_.index();
        ho_.forward(high_.data()[idx] - open_.data()[idx]);
        ol_.forward(open_.data()[idx] - low_.data()[idx]);

        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto hi = ho_.size() - 1, li = ol_.size() - 1;
        const double sh = a191::sum(ho_, hi, 20), sl = a191::sum(ol_, li, 20);
        line_.forward(sl != 0.0 ? sh / sl * 100.0 : 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    Line<double> ho_, ol_;
};

// ============================================================================
// Alpha#119: rank(decay_linear(corr(vwap,SUM(mean(vol,5),26),5),7))
//            - rank(decay_linear(ts_rank(min(corr(rank(open),rank(mean(vol,15)),21),9),7),8))
// Inputs: O, H, L, C, V   Warmup: 295
// ============================================================================
class Alpha191_119 : public Indicator<Alpha191_119> {
public:
    Alpha191_119(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_.data().reserve(n);
            mv5_.data().reserve(n);
            smv_.data().reserve(n);
            corr1_.data().reserve(n);
            dl1_.data().reserve(n);
            ro_.data().reserve(n);
            mv15_.data().reserve(n);
            rmv15_.data().reserve(n);
            corr2_.data().reserve(n);
            mc_.data().reserve(n);
            tr_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_.forward(a191::vwap(high_, low_, close_, idx));
        mv5_.forward(a191::sma(volume_, idx, std::min(idx + 1, 5uz)));
        const auto mi5 = mv5_.size() - 1;
        smv_.forward(mi5 + 1 >= 26uz ? a191::sum(mv5_, mi5, 26) : a191::NaN);
        const auto vi = vwap_.size() - 1, si = smv_.size() - 1;
        corr1_.forward(std::min(vi, si) + 1 >= 5uz
            ? a191::correlation(vwap_, smv_, std::min(vi, si), 5) : a191::NaN);
        const auto c1i = corr1_.size() - 1;
        dl1_.forward(c1i + 1 >= 7uz ? a191::decay_linear(corr1_, c1i, 7) : a191::NaN);

        ro_.forward(a191::ts_rank(open_, idx, std::min(idx + 1, 252uz)));
        mv15_.forward(a191::sma(volume_, idx, std::min(idx + 1, 15uz)));
        const auto m15i = mv15_.size() - 1;
        rmv15_.forward(a191::ts_rank(mv15_, m15i, std::min(m15i + 1, 252uz)));
        const auto roi = ro_.size() - 1, rmi = rmv15_.size() - 1;
        corr2_.forward(std::min(roi, rmi) + 1 >= 21uz
            ? a191::correlation(ro_, rmv15_, std::min(roi, rmi), 21) : a191::NaN);
        const auto c2i = corr2_.size() - 1;
        mc_.forward(c2i + 1 >= 9uz ? a191::ts_min(corr2_, c2i, 9) : a191::NaN);
        const auto mci = mc_.size() - 1;
        tr_.forward(mci + 1 >= 7uz ? a191::ts_rank(mc_, mci, 7) : a191::NaN);
        const auto tri = tr_.size() - 1;
        dl2_.forward(tri + 1 >= 8uz ? a191::decay_linear(tr_, tri, 8) : a191::NaN);

        if (idx + 1 < 295uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto dli1 = dl1_.size() - 1, dli2 = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, dli1, std::min(dli1 + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, dli2, std::min(dli2 + 1, 252uz));
        line_.forward(r1 - r2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 295; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_, mv5_, smv_, corr1_, dl1_;
    Line<double> ro_, mv15_, rmv15_, corr2_, mc_, tr_, dl2_;
};

// ============================================================================
// Alpha#120: rank(vwap-close) / rank(vwap+close)
// Same as #042. Inputs: H, L, C   Warmup: 253
// ============================================================================
class Alpha191_120 : public Indicator<Alpha191_120> {
public:
    Alpha191_120(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            diff_.data().reserve(n);
            total_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double vw = a191::vwap(high_, low_, close_, idx);
        const double c = close_.data()[idx];
        diff_.forward(vw - c);
        total_.forward(vw + c);

        if (idx + 1 < 253uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto di = diff_.size() - 1, ti = total_.size() - 1;
        const double rd = a191::ts_rank(diff_, di, std::min(di + 1, 252uz));
        const double rt = a191::ts_rank(total_, ti, std::min(ti + 1, 252uz));
        line_.forward(rt != 0.0 ? rd / rt : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> diff_, total_;
};

// ============================================================================
// Alpha#121: rank(vwap-close) / rank(vwap+close)  [duplicate of #042/#120]
// Simplified: same formula, retained for catalog completeness.
// Inputs: H, L, C   Warmup: 253
// ============================================================================
class Alpha191_121 : public Indicator<Alpha191_121> {
public:
    Alpha191_121(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            diff_.data().reserve(n);
            total_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double vw = a191::vwap(high_, low_, close_, idx);
        const double c = close_.data()[idx];
        diff_.forward(vw - c);
        total_.forward(vw + c);

        if (idx + 1 < 253uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto di = diff_.size() - 1, ti = total_.size() - 1;
        const double rd = a191::ts_rank(diff_, di, std::min(di + 1, 252uz));
        const double rt = a191::ts_rank(total_, ti, std::min(ti + 1, 252uz));
        line_.forward(rt != 0.0 ? rd / rt : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> diff_, total_;
};

// ============================================================================
// Alpha#122: (SMA(SMA(SMA(log(close),13,2),13,2),13,2)
//            - delay(SMA(SMA(SMA(log(close),13,2),13,2),13,2),1))
//            / delay(SMA(SMA(SMA(log(close),13,2),13,2),13,2),1)
// Triple-smoothed log close momentum.
// Inputs: C   Warmup: 40
// ============================================================================
class Alpha191_122 : public Indicator<Alpha191_122> {
public:
    explicit Alpha191_122(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double lc = a191::log(close_.data()[idx]);
        ema1_.update(lc, 13.0, 2.0);
        ema2_.update(ema1_.value, 13.0, 2.0);
        ema3_.update(ema2_.value, 13.0, 2.0);

        if (idx + 1 < 40uz) [[unlikely]] {
            prev_ema3_ = ema3_.value;
            line_.forward(a191::NaN);
            return;
        }
        line_.forward(prev_ema3_ != 0.0 ? (ema3_.value - prev_ema3_) / prev_ema3_ : 0.0);
        prev_ema3_ = ema3_.value;
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 40; }

private:
    const Line<double>& close_;
    SmaState ema1_, ema2_, ema3_;
    double prev_ema3_ = a191::NaN;
};

// ============================================================================
// Alpha#123: rank(corr(SUM((high+low)/2, 20), SUM(mean(volume,60), 20), 9))
// Inputs: H, L, V   Warmup: 292
// ============================================================================
class Alpha191_123 : public Indicator<Alpha191_123> {
public:
    Alpha191_123(const Line<double>& high, const Line<double>& low,
                 const Line<double>& volume)
        : high_(high), low_(low), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            mid_.data().reserve(n);
            smid_.data().reserve(n);
            mv60_.data().reserve(n);
            smv60_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = high_.index();
        mid_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);
        const auto mi = mid_.size() - 1;
        smid_.forward(mi + 1 >= 20uz ? a191::sum(mid_, mi, 20) : a191::NaN);

        mv60_.forward(a191::sma(volume_, idx, std::min(idx + 1, 60uz)));
        const auto mvi = mv60_.size() - 1;
        smv60_.forward(mvi + 1 >= 20uz ? a191::sum(mv60_, mvi, 20) : a191::NaN);

        const auto si = smid_.size() - 1, svi = smv60_.size() - 1;
        corr_.forward(std::min(si, svi) + 1 >= 9uz
            ? a191::correlation(smid_, smv60_, std::min(si, svi), 9) : a191::NaN);

        if (idx + 1 < 292uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ci = corr_.size() - 1;
        line_.forward(a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 292; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& volume_;
    Line<double> mid_, smid_, mv60_, smv60_, corr_;
};

// ============================================================================
// Alpha#124: (close - vwap) / decay_linear(rank(ts_max(close, 30)), 2)
// Inputs: H, L, C   Warmup: 284
// ============================================================================
class Alpha191_124 : public Indicator<Alpha191_124> {
public:
    Alpha191_124(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            tmax_.data().reserve(n);
            rtmax_.data().reserve(n);
        }
        const auto idx = close_.index();
        tmax_.forward(idx + 1 >= 30uz ? a191::ts_max(close_, idx, 30) : a191::NaN);
        const auto ti = tmax_.size() - 1;
        rtmax_.forward(a191::ts_rank(tmax_, ti, std::min(ti + 1, 252uz)));

        if (idx + 1 < 284uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = rtmax_.size() - 1;
        const double dl = ri + 1 >= 2uz ? a191::decay_linear(rtmax_, ri, 2) : a191::NaN;
        const double vw = a191::vwap(high_, low_, close_, idx);
        line_.forward(dl != 0.0 && !std::isnan(dl)
            ? (close_.data()[idx] - vw) / dl : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 284; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> tmax_, rtmax_;
};

// ============================================================================
// Alpha#125: rank(decay_linear(corr(vwap, mean(volume,80), 17), 20))
//            / rank(decay_linear(delta(close*0.5+vwap*0.5, 3), 16))
// Inputs: H, L, C, V   Warmup: 370
// ============================================================================
class Alpha191_125 : public Indicator<Alpha191_125> {
public:
    Alpha191_125(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_l_.data().reserve(n);
            mv80_.data().reserve(n);
            corr_.data().reserve(n);
            dl1_.data().reserve(n);
            blend_.data().reserve(n);
            dblend_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double vw = a191::vwap(high_, low_, close_, idx);
        vwap_l_.forward(vw);
        mv80_.forward(a191::sma(volume_, idx, std::min(idx + 1, 80uz)));
        const auto vi = vwap_l_.size() - 1, mi = mv80_.size() - 1;
        corr_.forward(std::min(vi, mi) + 1 >= 17uz
            ? a191::correlation(vwap_l_, mv80_, std::min(vi, mi), 17) : a191::NaN);
        const auto ci = corr_.size() - 1;
        dl1_.forward(ci + 1 >= 20uz ? a191::decay_linear(corr_, ci, 20) : a191::NaN);

        blend_.forward(close_.data()[idx] * 0.5 + vw * 0.5);
        const auto bi = blend_.size() - 1;
        dblend_.forward(bi >= 3uz ? blend_.data()[bi] - blend_.data()[bi - 3] : a191::NaN);
        const auto di = dblend_.size() - 1;
        dl2_.forward(di + 1 >= 16uz ? a191::decay_linear(dblend_, di, 16) : a191::NaN);

        if (idx + 1 < 370uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto d1i = dl1_.size() - 1, d2i = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, d1i, std::min(d1i + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, d2i, std::min(d2i + 1, 252uz));
        line_.forward(r2 != 0.0 ? r1 / r2 : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 370; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_l_, mv80_, corr_, dl1_;
    Line<double> blend_, dblend_, dl2_;
};

// ============================================================================
// Alpha#126: (close + high + low) / 3
// Inputs: H, L, C   Warmup: 1
// ============================================================================
class Alpha191_126 : public Indicator<Alpha191_126> {
public:
    Alpha191_126(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        line_.forward((close_.data()[idx] + high_.data()[idx] + low_.data()[idx]) / 3.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

// ============================================================================
// Alpha#127: mean((100*(close-max(close,12))/max(close,12))^2, 12)^(1/2)
// Inputs: C   Warmup: 24
// ============================================================================
class Alpha191_127 : public Indicator<Alpha191_127> {
public:
    explicit Alpha191_127(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            sq_.data().reserve(n);
        }
        const auto idx = close_.index();
        if (idx + 1 < 12uz) [[unlikely]] {
            sq_.forward(a191::NaN);
            line_.forward(a191::NaN);
            return;
        }
        const double mx = a191::ts_max(close_, idx, 12);
        const double pct = mx != 0.0 ? 100.0 * (close_.data()[idx] - mx) / mx : 0.0;
        sq_.forward(pct * pct);

        if (idx + 1 < 24uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto si = sq_.size() - 1;
        line_.forward(std::sqrt(a191::sma(sq_, si, 12)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 24; }

private:
    const Line<double>& close_;
    Line<double> sq_;
};

// ============================================================================
// Alpha#128: 100 - 100/(1 + SUM(IF(high+low>=delay(high,1)+delay(low,1), ...))
// Chaikin-like directional volume.
// Inputs: H, L, C, V   Warmup: 15
// ============================================================================
class Alpha191_128 : public Indicator<Alpha191_128> {
public:
    Alpha191_128(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            up_.data().reserve(close_.size());
            dn_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            up_.forward(0.0); dn_.forward(0.0);
            line_.forward(a191::NaN); return;
        }
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double c = close_.data()[idx], v = volume_.data()[idx];
        const double hp = high_.data()[idx - 1], lp = low_.data()[idx - 1];
        const double hl_avg = (h + l) / 2.0, hlp_avg = (hp + lp) / 2.0;
        const double range = h - l;
        const double clv = range != 0.0 ? ((c - l) - (h - c)) / range * v : 0.0;
        up_.forward(hl_avg >= hlp_avg ? clv : 0.0);
        dn_.forward(hl_avg < hlp_avg ? clv : 0.0);

        if (idx + 1 < 15uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ui = up_.size() - 1, di = dn_.size() - 1;
        const double su = a191::sum(up_, ui, 14), sd = a191::sum(dn_, di, 14);
        const double denom = su + sd;
        line_.forward(denom != 0.0 ? 100.0 - 100.0 / (1.0 + su / (a191::abs(sd) + 1e-10)) : 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 15; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> up_, dn_;
};

// ============================================================================
// Alpha#129: SUM(IF(close-delay(close,1)<0, abs(close-delay(close,1)), 0), 12)
// Inputs: C   Warmup: 13
// ============================================================================
class Alpha191_129 : public Indicator<Alpha191_129> {
public:
    explicit Alpha191_129(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            dn_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { dn_.forward(0.0); line_.forward(a191::NaN); return; }
        const double d = close_.data()[idx] - close_.data()[idx - 1];
        dn_.forward(d < 0.0 ? a191::abs(d) : 0.0);

        if (idx + 1 < 13uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto di = dn_.size() - 1;
        line_.forward(a191::sum(dn_, di, 12));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& close_;
    Line<double> dn_;
};

// ============================================================================
// Alpha#130: rank(decay_linear(corr((high+low)/2, mean(volume,40), 9), 10))
//            / rank(decay_linear(corr(rank(vwap), rank(volume), 7), 3))
// Inputs: H, L, C, V   Warmup: 310
// ============================================================================
class Alpha191_130 : public Indicator<Alpha191_130> {
public:
    Alpha191_130(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            mid_.data().reserve(n);
            mv40_.data().reserve(n);
            corr1_.data().reserve(n);
            dl1_.data().reserve(n);
            rvwap_.data().reserve(n);
            rvol_.data().reserve(n);
            corr2_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        mid_.forward((high_.data()[idx] + low_.data()[idx]) / 2.0);
        mv40_.forward(a191::sma(volume_, idx, std::min(idx + 1, 40uz)));
        const auto mi = mid_.size() - 1, mvi = mv40_.size() - 1;
        corr1_.forward(std::min(mi, mvi) + 1 >= 9uz
            ? a191::correlation(mid_, mv40_, std::min(mi, mvi), 9) : a191::NaN);
        const auto c1i = corr1_.size() - 1;
        dl1_.forward(c1i + 1 >= 10uz ? a191::decay_linear(corr1_, c1i, 10) : a191::NaN);

        const double vw = a191::vwap(high_, low_, close_, idx);
        Line<double> tmp_vwap;
        tmp_vwap.forward(vw);
        rvwap_.forward(a191::ts_rank(high_, idx, std::min(idx + 1, 252uz)));
        rvol_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
        const auto rvi = rvwap_.size() - 1, roi = rvol_.size() - 1;
        corr2_.forward(std::min(rvi, roi) + 1 >= 7uz
            ? a191::correlation(rvwap_, rvol_, std::min(rvi, roi), 7) : a191::NaN);
        const auto c2i = corr2_.size() - 1;
        dl2_.forward(c2i + 1 >= 3uz ? a191::decay_linear(corr2_, c2i, 3) : a191::NaN);

        if (idx + 1 < 310uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto d1i = dl1_.size() - 1, d2i = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, d1i, std::min(d1i + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, d2i, std::min(d2i + 1, 252uz));
        line_.forward(r2 != 0.0 ? r1 / r2 : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 310; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> mid_, mv40_, corr1_, dl1_;
    Line<double> rvwap_, rvol_, corr2_, dl2_;
};

// ============================================================================
// Alpha#131: rank(delta(vwap, 1))^ts_rank(corr(close, mean(volume,50), 18), 18)
// Inputs: H, L, C, V   Warmup: 322
// ============================================================================
class Alpha191_131 : public Indicator<Alpha191_131> {
public:
    Alpha191_131(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_l_.data().reserve(n);
            dvwap_.data().reserve(n);
            mv50_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double vw = a191::vwap(high_, low_, close_, idx);
        vwap_l_.forward(vw);
        const auto vi = vwap_l_.size() - 1;
        dvwap_.forward(vi >= 1uz ? vwap_l_.data()[vi] - vwap_l_.data()[vi - 1] : a191::NaN);

        mv50_.forward(a191::sma(volume_, idx, std::min(idx + 1, 50uz)));
        const auto mvi = mv50_.size() - 1;
        corr_.forward(std::min(idx, mvi) + 1 >= 18uz
            ? a191::correlation(close_, mv50_, std::min(idx, mvi), 18) : a191::NaN);

        if (idx + 1 < 322uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto di = dvwap_.size() - 1, ci = corr_.size() - 1;
        const double rdv = a191::ts_rank(dvwap_, di, std::min(di + 1, 252uz));
        const double trc = a191::ts_rank(corr_, ci, 18);
        line_.forward(a191::signed_power(rdv, trc));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 322; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_l_, dvwap_, mv50_, corr_;
};

// ============================================================================
// Alpha#132: mean(amount, 20)   where amount = close * volume
// Inputs: C, V   Warmup: 20
// ============================================================================
class Alpha191_132 : public Indicator<Alpha191_132> {
public:
    Alpha191_132(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            amt_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        amt_.forward(close_.data()[idx] * volume_.data()[idx]);
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ai = amt_.size() - 1;
        line_.forward(a191::sma(amt_, ai, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> amt_;
};

// ============================================================================
// Alpha#133: (20 - highday(high,20))/20*100 - (20 - lowday(low,20))/20*100
// Inputs: H, L   Warmup: 20
// ============================================================================
class Alpha191_133 : public Indicator<Alpha191_133> {
public:
    Alpha191_133(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double hd = a191::highday(high_, idx, 20);
        const double ld = a191::lowday(low_, idx, 20);
        line_.forward((20.0 - hd) / 20.0 * 100.0 - (20.0 - ld) / 20.0 * 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
};

// ============================================================================
// Alpha#134: (close - delay(close, 12)) / delay(close, 12) * volume
// Inputs: C, V   Warmup: 13
// ============================================================================
class Alpha191_134 : public Indicator<Alpha191_134> {
public:
    Alpha191_134(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 12uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double cp = close_.data()[idx - 12];
        line_.forward(cp != 0.0 ? (close_.data()[idx] - cp) / cp * volume_.data()[idx] : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#135: SMA(delay(close/delay(close,20), 1), 20, 1)
// Inputs: C   Warmup: 42
// ============================================================================
class Alpha191_135 : public Indicator<Alpha191_135> {
public:
    explicit Alpha191_135(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        double ratio = a191::NaN;
        if (idx >= 21uz) {
            const double cp20 = close_.data()[idx - 1 - 20];
            const double cp1 = close_.data()[idx - 1];
            ratio = cp20 != 0.0 ? cp1 / cp20 : a191::NaN;
        }
        if (!std::isnan(ratio)) ema_.update(ratio, 20.0, 1.0);
        if (idx + 1 < 42uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 42; }

private:
    const Line<double>& close_;
    SmaState ema_;
};

// ============================================================================
// Alpha#136: (-1 * rank(delta(returns, 3))) * corr(open, volume, 10)
// Inputs: O, C, V   Warmup: 266
// ============================================================================
class Alpha191_136 : public Indicator<Alpha191_136> {
public:
    Alpha191_136(const Line<double>& open, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            dret_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(idx >= 1uz && close_.data()[idx - 1] != 0.0
            ? (close_.data()[idx] - close_.data()[idx - 1]) / close_.data()[idx - 1]
            : a191::NaN);
        const auto ri = ret_.size() - 1;
        dret_.forward(ri >= 3uz && !std::isnan(ret_.data()[ri]) && !std::isnan(ret_.data()[ri - 3])
            ? ret_.data()[ri] - ret_.data()[ri - 3] : a191::NaN);

        if (idx + 1 < 266uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto di = dret_.size() - 1;
        const double rdr = a191::ts_rank(dret_, di, std::min(di + 1, 252uz));
        const double c = a191::correlation(open_, volume_, idx, 10);
        line_.forward(-1.0 * rdr * c);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 266; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> ret_, dret_;
};

// ============================================================================
// Alpha#137: 16*(close-delay(close,1)+(close-open)/2+delay(close,1)-delay(open,1))
//            / ((abs(high-delay(close,1))>abs(low-delay(close,1)) &
//                abs(high-delay(close,1))>abs(high-low))*
//               (abs(high-delay(close,1))+abs(low-delay(close,1))/2+abs(delay(close,1)-delay(open,1))/4)
//              + ...)  -- Adaptive Price Zone / True Range variant
// Simplified 3-branch conditional Williams AD variant.
// Inputs: O, H, L, C   Warmup: 2
// ============================================================================
class Alpha191_137 : public Indicator<Alpha191_137> {
public:
    Alpha191_137(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double c = close_.data()[idx], o = open_.data()[idx];
        const double h = high_.data()[idx], l = low_.data()[idx];
        const double cp = close_.data()[idx - 1], op = open_.data()[idx - 1];
        const double ahc = a191::abs(h - cp), alc = a191::abs(l - cp);
        const double ahl = a191::abs(h - l), aco = a191::abs(cp - op);
        double denom = 0.0;
        if (ahc > alc && ahc > ahl)
            denom = ahc + alc / 2.0 + aco / 4.0;
        else if (alc > ahc && alc > ahl)
            denom = alc + ahc / 2.0 + aco / 4.0;
        else
            denom = ahl + aco / 4.0;
        const double num = (c - cp) + (c - o) / 2.0 + (cp - op);
        line_.forward(denom != 0.0 ? 16.0 * num / denom : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

// ============================================================================
// Alpha#138: rank(decay_linear(corr(low,mean(volume,20),7),8))
//            - rank(decay_linear(delta(((low*0.7)+(vwap*0.3)),3),10))
// Inputs: H, L, C, V   Warmup: 300
// ============================================================================
class Alpha191_138 : public Indicator<Alpha191_138> {
public:
    Alpha191_138(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            mv20_.data().reserve(n);
            corr_.data().reserve(n);
            dl1_.data().reserve(n);
            blend_.data().reserve(n);
            dbl_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        mv20_.forward(a191::sma(volume_, idx, std::min(idx + 1, 20uz)));
        const auto mvi = mv20_.size() - 1;
        corr_.forward(std::min(idx, mvi) + 1 >= 7uz
            ? a191::correlation(low_, mv20_, std::min(idx, mvi), 7) : a191::NaN);
        const auto ci = corr_.size() - 1;
        dl1_.forward(ci + 1 >= 8uz ? a191::decay_linear(corr_, ci, 8) : a191::NaN);

        const double vw = a191::vwap(high_, low_, close_, idx);
        blend_.forward(low_.data()[idx] * 0.7 + vw * 0.3);
        const auto bi = blend_.size() - 1;
        dbl_.forward(bi >= 3uz ? blend_.data()[bi] - blend_.data()[bi - 3] : a191::NaN);
        const auto di = dbl_.size() - 1;
        dl2_.forward(di + 1 >= 10uz ? a191::decay_linear(dbl_, di, 10) : a191::NaN);

        if (idx + 1 < 300uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto d1i = dl1_.size() - 1, d2i = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, d1i, std::min(d1i + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, d2i, std::min(d2i + 1, 252uz));
        line_.forward(r1 - r2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 300; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> mv20_, corr_, dl1_;
    Line<double> blend_, dbl_, dl2_;
};

// ============================================================================
// Alpha#139: (-1 * corr(open, volume, 10))
// Inputs: O, V   Warmup: 10
// ============================================================================
class Alpha191_139 : public Indicator<Alpha191_139> {
public:
    Alpha191_139(const Line<double>& open, const Line<double>& volume)
        : open_(open), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(open_.size()); }
        const auto idx = open_.index();
        if (idx + 1 < 10uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(-1.0 * a191::correlation(open_, volume_, idx, 10));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 10; }

private:
    const Line<double>& open_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#140: min(rank(decay_linear(rank(open) + rank(low) - rank(high) - rank(close), 8)),
//                ts_rank(decay_linear(corr(ts_rank(close,8), ts_rank(mean(vol,60),20), 8), 7), 3))
// Inputs: O, H, L, C, V   Warmup: 350
// ============================================================================
class Alpha191_140 : public Indicator<Alpha191_140> {
public:
    Alpha191_140(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            rsum_.data().reserve(n);
            dl1_.data().reserve(n);
            rc8_.data().reserve(n);
            mv60_.data().reserve(n);
            rmv_.data().reserve(n);
            corr_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double ro = a191::ts_rank(open_, idx, std::min(idx + 1, 252uz));
        const double rl = a191::ts_rank(low_, idx, std::min(idx + 1, 252uz));
        const double rh = a191::ts_rank(high_, idx, std::min(idx + 1, 252uz));
        const double rc = a191::ts_rank(close_, idx, std::min(idx + 1, 252uz));
        rsum_.forward(ro + rl - rh - rc);
        const auto rsi = rsum_.size() - 1;
        dl1_.forward(rsi + 1 >= 8uz ? a191::decay_linear(rsum_, rsi, 8) : a191::NaN);

        rc8_.forward(a191::ts_rank(close_, idx, std::min(idx + 1, 8uz)));
        mv60_.forward(a191::sma(volume_, idx, std::min(idx + 1, 60uz)));
        const auto mvi = mv60_.size() - 1;
        rmv_.forward(a191::ts_rank(mv60_, mvi, std::min(mvi + 1, 20uz)));
        const auto r8i = rc8_.size() - 1, rmi = rmv_.size() - 1;
        corr_.forward(std::min(r8i, rmi) + 1 >= 8uz
            ? a191::correlation(rc8_, rmv_, std::min(r8i, rmi), 8) : a191::NaN);
        const auto ci = corr_.size() - 1;
        dl2_.forward(ci + 1 >= 7uz ? a191::decay_linear(corr_, ci, 7) : a191::NaN);

        if (idx + 1 < 350uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto d1i = dl1_.size() - 1, d2i = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, d1i, std::min(d1i + 1, 252uz));
        const double r2 = d2i + 1 >= 3uz ? a191::ts_rank(dl2_, d2i, 3) : a191::NaN;
        line_.forward(a191::min(r1, std::isnan(r2) ? r1 : r2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 350; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> rsum_, dl1_;
    Line<double> rc8_, mv60_, rmv_, corr_, dl2_;
};

// ============================================================================
// Alpha#141: rank(corr(rank(high), rank(mean(volume,15)), 9)) * (-1)
// Inputs: H, V   Warmup: 276
// ============================================================================
class Alpha191_141 : public Indicator<Alpha191_141> {
public:
    Alpha191_141(const Line<double>& high, const Line<double>& volume)
        : high_(high), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = high_.size();
            reserve_output(n);
            rh_.data().reserve(n);
            mv15_.data().reserve(n);
            rmv_.data().reserve(n);
            corr_.data().reserve(n);
        }
        const auto idx = high_.index();
        rh_.forward(a191::ts_rank(high_, idx, std::min(idx + 1, 252uz)));
        mv15_.forward(a191::sma(volume_, idx, std::min(idx + 1, 15uz)));
        const auto mvi = mv15_.size() - 1;
        rmv_.forward(a191::ts_rank(mv15_, mvi, std::min(mvi + 1, 252uz)));
        const auto rhi = rh_.size() - 1, rmi = rmv_.size() - 1;
        corr_.forward(std::min(rhi, rmi) + 1 >= 9uz
            ? a191::correlation(rh_, rmv_, std::min(rhi, rmi), 9) : a191::NaN);

        if (idx + 1 < 276uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ci = corr_.size() - 1;
        line_.forward(-1.0 * a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 276; }

private:
    const Line<double>& high_;
    const Line<double>& volume_;
    Line<double> rh_, mv15_, rmv_, corr_;
};

// ============================================================================
// Alpha#142: -1*rank(ts_rank(close,10))*rank(delta(delta(close,1),1))*rank(ts_rank(volume/mean(volume,20),5))
// Inputs: C, V   Warmup: 275
// ============================================================================
class Alpha191_142 : public Indicator<Alpha191_142> {
public:
    Alpha191_142(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vratio_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double mv20 = a191::sma(volume_, idx, std::min(idx + 1, 20uz));
        vratio_.forward(mv20 != 0.0 ? volume_.data()[idx] / mv20 : a191::NaN);

        if (idx + 1 < 275uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double rtc = a191::ts_rank(close_, idx, 10);
        const double d1 = idx >= 1uz ? close_.data()[idx] - close_.data()[idx - 1] : 0.0;
        const double d1p = idx >= 2uz ? close_.data()[idx - 1] - close_.data()[idx - 2] : 0.0;
        const double dd = d1 - d1p;
        const auto vi = vratio_.size() - 1;
        const double rtv = a191::ts_rank(vratio_, vi, std::min(vi + 1, 5uz));
        line_.forward(-1.0 * rtc * dd * rtv);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 275; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vratio_;
};

// ============================================================================
// Alpha#143: (close - delay(close, 1)) / delay(close, 1)  [simple returns]
// Inputs: C   Warmup: 2
// ============================================================================
class Alpha191_143 : public Indicator<Alpha191_143> {
public:
    explicit Alpha191_143(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double cp = close_.data()[idx - 1];
        line_.forward(cp != 0.0 ? (close_.data()[idx] - cp) / cp : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#144: SUMIF(abs(close/delay(close,1)-1)/amount, 20, close<delay(close,1))
//            / COUNT(close<delay(close,1), 20)
// Inputs: C, V   Warmup: 21
// ============================================================================
class Alpha191_144 : public Indicator<Alpha191_144> {
public:
    Alpha191_144(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ratio_.data().reserve(close_.size());
            cond_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            ratio_.forward(0.0); cond_.forward(0.0);
            line_.forward(a191::NaN); return;
        }
        const double cp = close_.data()[idx - 1];
        const double amt = close_.data()[idx] * volume_.data()[idx];
        const double ret = cp != 0.0 ? a191::abs(close_.data()[idx] / cp - 1.0) : 0.0;
        const double r = amt != 0.0 ? ret / amt : 0.0;
        const bool down = close_.data()[idx] < cp;
        ratio_.forward(down ? r : 0.0);
        cond_.forward(down ? 1.0 : 0.0);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = ratio_.size() - 1, ci = cond_.size() - 1;
        const double sr = a191::sum(ratio_, ri, 20);
        const double cnt = a191::sum(cond_, ci, 20);
        line_.forward(cnt != 0.0 ? sr / cnt : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> ratio_, cond_;
};

// ============================================================================
// Alpha#145: (mean(volume,9) - mean(volume,26)) / mean(volume,12) * 100
// Inputs: V   Warmup: 26
// ============================================================================
class Alpha191_145 : public Indicator<Alpha191_145> {
public:
    explicit Alpha191_145(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(volume_.size()); }
        const auto idx = volume_.index();
        if (idx + 1 < 26uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double m9 = a191::sma(volume_, idx, 9);
        const double m26 = a191::sma(volume_, idx, 26);
        const double m12 = a191::sma(volume_, idx, 12);
        line_.forward(m12 != 0.0 ? (m9 - m26) / m12 * 100.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 26; }

private:
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#146: mean((close-delay(close,1))/delay(close,1) - SMA((close-delay(close,1))/delay(close,1), 61, 2), 20)^2
// Inputs: C   Warmup: 82
// ============================================================================
class Alpha191_146 : public Indicator<Alpha191_146> {
public:
    explicit Alpha191_146(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ret_.data().reserve(close_.size());
            sq_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            ret_.forward(0.0); sq_.forward(0.0);
            line_.forward(a191::NaN); return;
        }
        const double cp = close_.data()[idx - 1];
        const double r = cp != 0.0 ? (close_.data()[idx] - cp) / cp : 0.0;
        ret_.forward(r);
        ema61_.update(r, 61.0, 2.0);
        const double diff = r - ema61_.value;
        sq_.forward(diff * diff);

        if (idx + 1 < 82uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto si = sq_.size() - 1;
        line_.forward(a191::sma(sq_, si, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 82; }

private:
    const Line<double>& close_;
    Line<double> ret_, sq_;
    SmaState ema61_;
};

// ============================================================================
// Alpha#147: regbeta(mean(close, 12), sequence, 12)
// Inputs: C   Warmup: 24
// ============================================================================
class Alpha191_147 : public Indicator<Alpha191_147> {
public:
    explicit Alpha191_147(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            mc_.data().reserve(close_.size());
            seq_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        mc_.forward(idx + 1 >= 12uz ? a191::sma(close_, idx, 12) : a191::NaN);
        seq_.forward(static_cast<double>(idx + 1));

        if (idx + 1 < 24uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto mi = mc_.size() - 1, si = seq_.size() - 1;
        line_.forward(a191::regbeta(mc_, seq_, std::min(mi, si), 12));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 24; }

private:
    const Line<double>& close_;
    Line<double> mc_, seq_;
};

// ============================================================================
// Alpha#148: rank(corr(open, SUM(mean(volume,60), 9), 6))
//            - rank(open - ts_min(open, 14))
// Inputs: O, V   Warmup: 330
// ============================================================================
class Alpha191_148 : public Indicator<Alpha191_148> {
public:
    Alpha191_148(const Line<double>& open, const Line<double>& volume)
        : open_(open), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = open_.size();
            reserve_output(n);
            mv60_.data().reserve(n);
            smv_.data().reserve(n);
            corr_.data().reserve(n);
            omin_.data().reserve(n);
        }
        const auto idx = open_.index();
        mv60_.forward(a191::sma(volume_, idx, std::min(idx + 1, 60uz)));
        const auto mvi = mv60_.size() - 1;
        smv_.forward(mvi + 1 >= 9uz ? a191::sum(mv60_, mvi, 9) : a191::NaN);
        const auto si = smv_.size() - 1;
        corr_.forward(std::min(idx, si) + 1 >= 6uz
            ? a191::correlation(open_, smv_, std::min(idx, si), 6) : a191::NaN);

        omin_.forward(idx + 1 >= 14uz
            ? open_.data()[idx] - a191::ts_min(open_, idx, 14) : a191::NaN);

        if (idx + 1 < 330uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ci = corr_.size() - 1, oi = omin_.size() - 1;
        const double rc = a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz));
        const double ro = a191::ts_rank(omin_, oi, std::min(oi + 1, 252uz));
        line_.forward(rc - ro);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 330; }

private:
    const Line<double>& open_;
    const Line<double>& volume_;
    Line<double> mv60_, smv_, corr_, omin_;
};

// ============================================================================
// Alpha#149: regbeta(filter(close/delay(close,1)-1, benchmark_close/delay(benchmark_close,1)-1 < 0), ...)
// Single-symbol: use close itself as benchmark proxy → degenerate.
// Simplified: regbeta(returns, sequence, 12) when returns < 0
// Inputs: C   Warmup: 253
// ============================================================================
class Alpha191_149 : public Indicator<Alpha191_149> {
public:
    explicit Alpha191_149(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ret_.data().reserve(close_.size());
            seq_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double r = a191::NaN;
        if (idx >= 1uz) {
            const double cp = close_.data()[idx - 1];
            r = cp != 0.0 ? (close_.data()[idx] - cp) / cp : a191::NaN;
        }
        ret_.forward(!std::isnan(r) && r < 0.0 ? r : a191::NaN);
        seq_.forward(static_cast<double>(idx + 1));

        if (idx + 1 < 253uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = ret_.size() - 1, si = seq_.size() - 1;
        line_.forward(a191::regbeta(ret_, seq_, std::min(ri, si), 252));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& close_;
    Line<double> ret_, seq_;
};

// ============================================================================
// Alpha#150: (close + high + low) / 3 * volume
// Inputs: H, L, C, V   Warmup: 1
// ============================================================================
class Alpha191_150 : public Indicator<Alpha191_150> {
public:
    Alpha191_150(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        line_.forward((close_.data()[idx] + high_.data()[idx] + low_.data()[idx]) / 3.0
                      * volume_.data()[idx]);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#151: SMA(close - delay(close, 20), 20, 1)
// Inputs: C   Warmup: 41
// ============================================================================
class Alpha191_151 : public Indicator<Alpha191_151> {
public:
    explicit Alpha191_151(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        double mom = a191::NaN;
        if (idx >= 20uz) mom = close_.data()[idx] - close_.data()[idx - 20];
        if (!std::isnan(mom)) ema_.update(mom, 20.0, 1.0);
        if (idx + 1 < 41uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 41; }

private:
    const Line<double>& close_;
    SmaState ema_;
};

// ============================================================================
// Alpha#152: SMA(mean(delay(SMA(delay(close/delay(close,9),1),9,1),1),12) - mean(delay(SMA(delay(close/delay(close,9),1),9,1),1),26), 9, 1)
// Simplified: SMA of MACD(12,26) of a smoothed momentum.
// Inputs: C   Warmup: 60
// ============================================================================
class Alpha191_152 : public Indicator<Alpha191_152> {
public:
    explicit Alpha191_152(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            mom9_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double ratio = a191::NaN;
        if (idx >= 9uz) {
            const double cp9 = close_.data()[idx - 9];
            ratio = cp9 != 0.0 ? close_.data()[idx] / cp9 : a191::NaN;
        }
        if (!std::isnan(ratio)) ema9_.update(ratio, 9.0, 1.0);
        mom9_.forward(ema9_.initialized ? ema9_.value : a191::NaN);

        const auto mi = mom9_.size() - 1;
        const double m12 = mi + 1 >= 12uz ? a191::sma(mom9_, mi, 12) : a191::NaN;
        const double m26 = mi + 1 >= 26uz ? a191::sma(mom9_, mi, 26) : a191::NaN;
        const double diff = (!std::isnan(m12) && !std::isnan(m26)) ? m12 - m26 : a191::NaN;
        if (!std::isnan(diff)) sig_.update(diff, 9.0, 1.0);

        if (idx + 1 < 60uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(sig_.initialized ? sig_.value : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 60; }

private:
    const Line<double>& close_;
    SmaState ema9_, sig_;
    Line<double> mom9_;
};

// ============================================================================
// Alpha#153: (mean(close,3) + mean(close,6) + mean(close,12) + mean(close,24)) / 4
// Inputs: C   Warmup: 24
// ============================================================================
class Alpha191_153 : public Indicator<Alpha191_153> {
public:
    explicit Alpha191_153(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 24uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double m3 = a191::sma(close_, idx, 3);
        const double m6 = a191::sma(close_, idx, 6);
        const double m12 = a191::sma(close_, idx, 12);
        const double m24 = a191::sma(close_, idx, 24);
        line_.forward((m3 + m6 + m12 + m24) / 4.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 24; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#154: (vwap - min(vwap, 16)) < corr(vwap, mean(volume,180), 18) ? 1 : 0
// Inputs: H, L, C, V   Warmup: 435
// ============================================================================
class Alpha191_154 : public Indicator<Alpha191_154> {
public:
    Alpha191_154(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_l_.data().reserve(n);
            mv180_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_l_.forward(a191::vwap(high_, low_, close_, idx));
        mv180_.forward(a191::sma(volume_, idx, std::min(idx + 1, 180uz)));

        if (idx + 1 < 435uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto vi = vwap_l_.size() - 1, mvi = mv180_.size() - 1;
        const double vmin = a191::ts_min(vwap_l_, vi, 16);
        const double diff = vwap_l_.data()[vi] - vmin;
        const double c = a191::correlation(vwap_l_, mv180_, std::min(vi, mvi), 18);
        line_.forward(diff < c ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 435; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_l_, mv180_;
};

// ============================================================================
// Alpha#155: SMA(volume, 13, 2) - SMA(volume, 27, 2) - SMA(SMA(volume,13,2)-SMA(volume,27,2), 10, 2)
// Inputs: V   Warmup: 40
// ============================================================================
class Alpha191_155 : public Indicator<Alpha191_155> {
public:
    explicit Alpha191_155(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(volume_.size()); }
        const auto idx = volume_.index();
        const double v = volume_.data()[idx];
        e13_.update(v, 13.0, 2.0);
        e27_.update(v, 27.0, 2.0);
        const double diff = e13_.value - e27_.value;
        ediff_.update(diff, 10.0, 2.0);

        if (idx + 1 < 40uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(diff - ediff_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 40; }

private:
    const Line<double>& volume_;
    SmaState e13_, e27_, ediff_;
};

// ============================================================================
// Alpha#156: max(rank(decay_linear(delta(vwap,5),3)), rank(decay_linear(
//            (delta(open*0.15+low*0.85,-1) / (open*0.15+low*0.85)) * (-1*ts_rank(..)), ..)))
// Simplified: rank-vs-rank comparison.
// Inputs: O, H, L, C, V   Warmup: 280
// ============================================================================
class Alpha191_156 : public Indicator<Alpha191_156> {
public:
    Alpha191_156(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close,
                 const Line<double>& volume)
        : open_(open), high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_l_.data().reserve(n);
            dvw_.data().reserve(n);
            dl1_.data().reserve(n);
            blend_.data().reserve(n);
            ratio_.data().reserve(n);
            dl2_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double vw = a191::vwap(high_, low_, close_, idx);
        vwap_l_.forward(vw);
        const auto vi = vwap_l_.size() - 1;
        dvw_.forward(vi >= 5uz ? vwap_l_.data()[vi] - vwap_l_.data()[vi - 5] : a191::NaN);
        const auto di = dvw_.size() - 1;
        dl1_.forward(di + 1 >= 3uz ? a191::decay_linear(dvw_, di, 3) : a191::NaN);

        const double bl = open_.data()[idx] * 0.15 + low_.data()[idx] * 0.85;
        blend_.forward(bl);
        const auto bi = blend_.size() - 1;
        const double dbl = bi >= 1uz ? blend_.data()[bi] - blend_.data()[bi - 1] : a191::NaN;
        const double rat = bl != 0.0 && !std::isnan(dbl) ? -1.0 * dbl / bl : a191::NaN;
        ratio_.forward(rat);
        const auto ri = ratio_.size() - 1;
        dl2_.forward(ri + 1 >= 3uz ? a191::decay_linear(ratio_, ri, 3) : a191::NaN);

        if (idx + 1 < 280uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto d1i = dl1_.size() - 1, d2i = dl2_.size() - 1;
        const double r1 = a191::ts_rank(dl1_, d1i, std::min(d1i + 1, 252uz));
        const double r2 = a191::ts_rank(dl2_, d2i, std::min(d2i + 1, 252uz));
        line_.forward(a191::max(r1, r2));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 280; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_l_, dvw_, dl1_;
    Line<double> blend_, ratio_, dl2_;
};

// ============================================================================
// Alpha#157: min(product(rank(rank(log(SUM(ts_min(rank(-1*rank(delta(close-1,5))),2),1)))), 1), 5)
//            + ts_rank(delay(-1*returns,6), 5)
// Simplified: -ts_rank(delta(close,5), 2) + ts_rank(delay(-returns,6), 5)
// Inputs: C   Warmup: 20
// ============================================================================
class Alpha191_157 : public Indicator<Alpha191_157> {
public:
    explicit Alpha191_157(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ret_.data().reserve(close_.size());
            nret6_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double r = a191::NaN;
        if (idx >= 1uz) {
            const double cp = close_.data()[idx - 1];
            r = cp != 0.0 ? (close_.data()[idx] - cp) / cp : a191::NaN;
        }
        ret_.forward(r);
        const auto ri = ret_.size() - 1;
        nret6_.forward(ri >= 6uz && !std::isnan(ret_.data()[ri - 6]) ? -ret_.data()[ri - 6] : a191::NaN);

        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double rd5 = a191::ts_rank(close_, idx, std::min(idx + 1, 5uz));
        const auto ni = nret6_.size() - 1;
        const double trn = a191::ts_rank(nret6_, ni, std::min(ni + 1, 5uz));
        line_.forward(-rd5 + trn);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& close_;
    Line<double> ret_, nret6_;
};

// ============================================================================
// Alpha#158: (high - SMA(close, 15, 2)) - (low - SMA(close, 15, 2))
//          = high - low  (simplifies to range)
// Inputs: H, L, C   Warmup: 15
// ============================================================================
class Alpha191_158 : public Indicator<Alpha191_158> {
public:
    Alpha191_158(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 15uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double sma15 = [&]{ SmaState s; for (std::size_t i = idx - 14; i <= idx; ++i) s.update(close_.data()[i], 15.0, 2.0); return s.value; }();
        line_.forward((high_.data()[idx] - sma15) - (low_.data()[idx] - sma15));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 15; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

// ============================================================================
// Alpha#159: ((close - SUM(min(low, delay(close,1)), 6)) / SUM(max(high, delay(close,1)) - min(low, delay(close,1)), 6) * 12 * 24
//            + (close - SUM(min(low, delay(close,1)), 12)) / SUM(max(high, delay(close,1)) - min(low, delay(close,1)), 12) * 6 * 24
//            + (close - SUM(min(low, delay(close,1)), 24)) / SUM(max(high, delay(close,1)) - min(low, delay(close,1)), 24)) * 100 / (6*12+6*24+12*24)
// Williams %R multi-period fusion.
// Inputs: H, L, C   Warmup: 25
// ============================================================================
class Alpha191_159 : public Indicator<Alpha191_159> {
public:
    Alpha191_159(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            lmin_.data().reserve(close_.size());
            rng_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            lmin_.forward(low_.data()[idx]);
            rng_.forward(high_.data()[idx] - low_.data()[idx]);
            line_.forward(a191::NaN); return;
        }
        const double cp = close_.data()[idx - 1];
        lmin_.forward(a191::min(low_.data()[idx], cp));
        rng_.forward(a191::max(high_.data()[idx], cp) - a191::min(low_.data()[idx], cp));

        if (idx + 1 < 25uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto li = lmin_.size() - 1, ri = rng_.size() - 1;
        auto wr = [&](std::size_t p) -> double {
            const double sl = a191::sum(lmin_, li, p);
            const double sr = a191::sum(rng_, ri, p);
            return sr != 0.0 ? (close_.data()[idx] - sl) / sr : 0.0;
        };
        const double w6 = wr(6), w12 = wr(12), w24 = wr(24);
        line_.forward((w6 * 12.0 * 24.0 + w12 * 6.0 * 24.0 + w24 * 6.0 * 12.0) * 100.0
                      / (6.0 * 12.0 + 6.0 * 24.0 + 12.0 * 24.0));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 25; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> lmin_, rng_;
};

// ============================================================================
// Alpha#160: SMA(IF(close<=delay(close,1), stddev(close,20), 0), 20, 1)
// Inputs: C   Warmup: 40
// ============================================================================
class Alpha191_160 : public Indicator<Alpha191_160> {
public:
    explicit Alpha191_160(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        double val = 0.0;
        if (idx >= 20uz && idx >= 1uz && close_.data()[idx] <= close_.data()[idx - 1])
            val = a191::stddev(close_, idx, 20);
        ema_.update(val, 20.0, 1.0);
        if (idx + 1 < 40uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 40; }

private:
    const Line<double>& close_;
    SmaState ema_;
};

// ============================================================================
// Alpha#161: mean(max(max(high-low, abs(delay(close,1)-high)), abs(delay(close,1)-low)), 12)
// Average True Range (12).
// Inputs: H, L, C   Warmup: 13
// ============================================================================
class Alpha191_161 : public Indicator<Alpha191_161> {
public:
    Alpha191_161(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            tr_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            tr_.forward(high_.data()[0] - low_.data()[0]);
            line_.forward(a191::NaN); return;
        }
        const double cp = close_.data()[idx - 1];
        const double hl = high_.data()[idx] - low_.data()[idx];
        const double hc = a191::abs(cp - high_.data()[idx]);
        const double lc = a191::abs(cp - low_.data()[idx]);
        tr_.forward(a191::max(a191::max(hl, hc), lc));

        if (idx + 1 < 13uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ti = tr_.size() - 1;
        line_.forward(a191::sma(tr_, ti, 12));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> tr_;
};

// ============================================================================
// Alpha#162: SMA(max(close-delay(close,1),0), 12, 1) / SMA(abs(close-delay(close,1)), 12, 1) * 100
// RSI(12) variant with SMA smoothing.
// Inputs: C   Warmup: 13
// ============================================================================
class Alpha191_162 : public Indicator<Alpha191_162> {
public:
    explicit Alpha191_162(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double d = close_.data()[idx] - close_.data()[idx - 1];
        ema_up_.update(a191::max(d, 0.0), 12.0, 1.0);
        ema_abs_.update(a191::abs(d), 12.0, 1.0);
        if (idx + 1 < 13uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema_abs_.value != 0.0 ? ema_up_.value / ema_abs_.value * 100.0 : 50.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& close_;
    SmaState ema_up_, ema_abs_;
};

// ============================================================================
// Alpha#163: rank(-1*returns * mean(volume,20) * vwap * (high-close))
// Inputs: H, L, C, V   Warmup: 253
// ============================================================================
class Alpha191_163 : public Indicator<Alpha191_163> {
public:
    Alpha191_163(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            val_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double r = 0.0;
        if (idx >= 1uz) {
            const double cp = close_.data()[idx - 1];
            r = cp != 0.0 ? (close_.data()[idx] - cp) / cp : 0.0;
        }
        const double vw = a191::vwap(high_, low_, close_, idx);
        const double mv = a191::sma(volume_, idx, std::min(idx + 1, 20uz));
        val_.forward(-1.0 * r * mv * vw * (high_.data()[idx] - close_.data()[idx]));

        if (idx + 1 < 253uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto vi = val_.size() - 1;
        line_.forward(a191::ts_rank(val_, vi, std::min(vi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> val_;
};

// ============================================================================
// Alpha#164: SMA((IF(close>delay(close,1), 1/(close-delay(close,1)), 1)
//            - min(IF(close>delay(close,1), 1/(close-delay(close,1)), 1), 12))
//            / (high - low) * 100, 13, 2)
// Inputs: H, L, C   Warmup: 27
// ============================================================================
class Alpha191_164 : public Indicator<Alpha191_164> {
public:
    Alpha191_164(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inv_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            inv_.forward(1.0); line_.forward(a191::NaN); return;
        }
        const double c = close_.data()[idx], cp = close_.data()[idx - 1];
        const double d = c - cp;
        const double invd = (c > cp && d != 0.0) ? 1.0 / d : 1.0;
        inv_.forward(invd);

        const auto ii = inv_.size() - 1;
        if (idx + 1 < 14uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double mi = a191::ts_min(inv_, ii, 12);
        const double hl = high_.data()[idx] - low_.data()[idx];
        const double raw = hl != 0.0 ? (invd - mi) / hl * 100.0 : 0.0;
        ema_.update(raw, 13.0, 2.0);
        if (idx + 1 < 27uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 27; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> inv_;
    SmaState ema_;
};

// ============================================================================
// Alpha#165: max(SUM(close - mean(close, 48)), -SUM(close - mean(close, 48))) / stddev(close, 48)
// Inputs: C   Warmup: 48
// ============================================================================
class Alpha191_165 : public Indicator<Alpha191_165> {
public:
    explicit Alpha191_165(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 48uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double m48 = a191::sma(close_, idx, 48);
        double s = 0.0;
        for (std::size_t i = 0; i < 48uz; ++i)
            s += close_.data()[idx - 47 + i] - m48;
        const double sd = a191::stddev(close_, idx, 48);
        line_.forward(sd != 0.0 ? a191::max(s, -s) / sd : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 48; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#166: -20*(20-1)^1.5 * SUM(close/delay(close,1)-1-mean(close/delay(close,1)-1,20), 20)
//            / ((20-1)*(20-2)*(SUM((close/delay(close,1)-1)^2, 20))^1.5)
// Rolling skewness of returns.
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha191_166 : public Indicator<Alpha191_166> {
public:
    explicit Alpha191_166(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ret_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { ret_.forward(0.0); line_.forward(a191::NaN); return; }
        const double cp = close_.data()[idx - 1];
        ret_.forward(cp != 0.0 ? close_.data()[idx] / cp - 1.0 : 0.0);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = ret_.size() - 1;
        const double m = a191::sma(ret_, ri, 20);
        double s3 = 0.0, s2 = 0.0;
        for (std::size_t i = 0; i < 20uz; ++i) {
            const double d = ret_.data()[ri - 19 + i] - m;
            s3 += d * d * d;
            s2 += d * d;
        }
        const double n = 20.0;
        const double denom = (n - 1.0) * (n - 2.0) * std::pow(s2, 1.5);
        line_.forward(denom != 0.0 ? -n * std::pow(n - 1.0, 1.5) * s3 / denom : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    Line<double> ret_;
};

// ============================================================================
// Alpha#167: SUM(IF(close-delay(close,1)>0, close-delay(close,1), 0), 12)
// Inputs: C   Warmup: 13
// ============================================================================
class Alpha191_167 : public Indicator<Alpha191_167> {
public:
    explicit Alpha191_167(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            up_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { up_.forward(0.0); line_.forward(a191::NaN); return; }
        const double d = close_.data()[idx] - close_.data()[idx - 1];
        up_.forward(d > 0.0 ? d : 0.0);
        if (idx + 1 < 13uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ui = up_.size() - 1;
        line_.forward(a191::sum(up_, ui, 12));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 13; }

private:
    const Line<double>& close_;
    Line<double> up_;
};

// ============================================================================
// Alpha#168: -volume / mean(volume, 20)
// Inputs: V   Warmup: 20
// ============================================================================
class Alpha191_168 : public Indicator<Alpha191_168> {
public:
    explicit Alpha191_168(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(volume_.size()); }
        const auto idx = volume_.index();
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double mv = a191::sma(volume_, idx, 20);
        line_.forward(mv != 0.0 ? -volume_.data()[idx] / mv : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#169: SMA(mean(delay(SMA(close-delay(close,1),9,1),1), 12)
//            - mean(delay(SMA(close-delay(close,1),9,1),1), 26), 10, 1)
// MACD of smoothed momentum.
// Inputs: C   Warmup: 50
// ============================================================================
class Alpha191_169 : public Indicator<Alpha191_169> {
public:
    explicit Alpha191_169(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            sm_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        double mom = 0.0;
        if (idx >= 1uz) mom = close_.data()[idx] - close_.data()[idx - 1];
        ema9_.update(mom, 9.0, 1.0);
        sm_.forward(ema9_.value);

        const auto si = sm_.size() - 1;
        const double m12 = si + 1 >= 12uz ? a191::sma(sm_, si, 12) : a191::NaN;
        const double m26 = si + 1 >= 26uz ? a191::sma(sm_, si, 26) : a191::NaN;
        const double diff = (!std::isnan(m12) && !std::isnan(m26)) ? m12 - m26 : a191::NaN;
        if (!std::isnan(diff)) sig_.update(diff, 10.0, 1.0);
        if (idx + 1 < 50uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(sig_.initialized ? sig_.value : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 50; }

private:
    const Line<double>& close_;
    SmaState ema9_, sig_;
    Line<double> sm_;
};

// ============================================================================
// Alpha#170: rank(1/close) * volume / mean(volume,20)
//            * (high * rank(high-close) / mean(high,5) - vwap)
// Inputs: H, L, C, V   Warmup: 253
// ============================================================================
class Alpha191_170 : public Indicator<Alpha191_170> {
public:
    Alpha191_170(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            inv_.data().reserve(close_.size());
            hmc_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double c = close_.data()[idx], h = high_.data()[idx];
        inv_.forward(c != 0.0 ? 1.0 / c : a191::NaN);
        hmc_.forward(h - c);

        if (idx + 1 < 253uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ii = inv_.size() - 1, hi = hmc_.size() - 1;
        const double rinv = a191::ts_rank(inv_, ii, std::min(ii + 1, 252uz));
        const double mv20 = a191::sma(volume_, idx, 20);
        const double vr = mv20 != 0.0 ? volume_.data()[idx] / mv20 : 0.0;
        const double rhmc = a191::ts_rank(hmc_, hi, std::min(hi + 1, 252uz));
        const double mh5 = a191::sma(high_, idx, 5);
        const double vw = a191::vwap(high_, low_, close_, idx);
        const double term = mh5 != 0.0 ? h * rhmc / mh5 - vw : -vw;
        line_.forward(rinv * vr * term);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> inv_, hmc_;
};

// ============================================================================
// Alpha#171: (-1*(low-close)*(open^5))/((close-high)*(close^5))
// Inputs: O, H, L, C   Warmup: 1
// ============================================================================
class Alpha191_171 : public Indicator<Alpha191_171> {
public:
    Alpha191_171(const Line<double>& open, const Line<double>& high,
                 const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double o = open_.data()[idx], h = high_.data()[idx];
        const double l = low_.data()[idx], c = close_.data()[idx];
        const double denom = (c - h) * std::pow(c, 5.0);
        line_.forward(denom != 0.0 ? -1.0 * (l - c) * std::pow(o, 5.0) / denom : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

// ============================================================================
// Alpha#172: mean(abs(SUM(IF(LD>0 & LD>HD, LD, 0), 14)*100/14
//            - SUM(IF(HD>0 & HD>LD, HD, 0), 14)*100/14)
//            / (SUM(IF(LD>0 & LD>HD, LD, 0), 14)*100/14
//            + SUM(IF(HD>0 & HD>LD, HD, 0), 14)*100/14) * 100, 6)
// ADX(14,6).
// Inputs: H, L, C   Warmup: 21
// ============================================================================
class Alpha191_172 : public Indicator<Alpha191_172> {
public:
    Alpha191_172(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            pdm_.data().reserve(close_.size());
            ndm_.data().reserve(close_.size());
            dx_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            pdm_.forward(0.0); ndm_.forward(0.0); dx_.forward(0.0);
            line_.forward(a191::NaN); return;
        }
        const double hd = high_.data()[idx] - high_.data()[idx - 1];
        const double ld = low_.data()[idx - 1] - low_.data()[idx];
        pdm_.forward(hd > 0.0 && hd > ld ? hd : 0.0);
        ndm_.forward(ld > 0.0 && ld > hd ? ld : 0.0);

        if (idx + 1 < 15uz) [[unlikely]] { dx_.forward(0.0); line_.forward(a191::NaN); return; }
        const auto pi = pdm_.size() - 1, ni = ndm_.size() - 1;
        const double sp = a191::sum(pdm_, pi, 14);
        const double sn = a191::sum(ndm_, ni, 14);
        const double pdi = sp * 100.0 / 14.0;
        const double ndi = sn * 100.0 / 14.0;
        const double dxval = (pdi + ndi) != 0.0 ? a191::abs(pdi - ndi) / (pdi + ndi) * 100.0 : 0.0;
        dx_.forward(dxval);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto di = dx_.size() - 1;
        line_.forward(a191::sma(dx_, di, 6));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> pdm_, ndm_, dx_;
};

// ============================================================================
// Alpha#173: 3*SMA(close, 13, 2) - 2*SMA(SMA(close, 13, 2), 13, 2) + SMA(SMA(SMA(log(close), 13, 2), 13, 2), 13, 2)
// TRIX-like triple smoothed indicator.
// Inputs: C   Warmup: 40
// ============================================================================
class Alpha191_173 : public Indicator<Alpha191_173> {
public:
    explicit Alpha191_173(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double c = close_.data()[idx], lc = a191::log(c);
        e1_.update(c, 13.0, 2.0);
        e2_.update(e1_.value, 13.0, 2.0);
        el1_.update(lc, 13.0, 2.0);
        el2_.update(el1_.value, 13.0, 2.0);
        el3_.update(el2_.value, 13.0, 2.0);
        if (idx + 1 < 40uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(3.0 * e1_.value - 2.0 * e2_.value + el3_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 40; }

private:
    const Line<double>& close_;
    SmaState e1_, e2_, el1_, el2_, el3_;
};

// ============================================================================
// Alpha#174: SMA(IF(close>delay(close,1), stddev(close,20), 0), 20, 1)
// Same as #160.
// Inputs: C   Warmup: 40
// ============================================================================
class Alpha191_174 : public Indicator<Alpha191_174> {
public:
    explicit Alpha191_174(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        double val = 0.0;
        if (idx >= 20uz && idx >= 1uz && close_.data()[idx] > close_.data()[idx - 1])
            val = a191::stddev(close_, idx, 20);
        ema_.update(val, 20.0, 1.0);
        if (idx + 1 < 40uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema_.value);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 40; }

private:
    const Line<double>& close_;
    SmaState ema_;
};

// ============================================================================
// Alpha#175: mean(max(max(high-low, abs(delay(close,1)-high)), abs(delay(close,1)-low)), 6)
// ATR(6).
// Inputs: H, L, C   Warmup: 7
// ============================================================================
class Alpha191_175 : public Indicator<Alpha191_175> {
public:
    Alpha191_175(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            tr_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            tr_.forward(high_.data()[0] - low_.data()[0]);
            line_.forward(a191::NaN); return;
        }
        const double cp = close_.data()[idx - 1];
        tr_.forward(a191::max(a191::max(high_.data()[idx] - low_.data()[idx],
                                        a191::abs(cp - high_.data()[idx])),
                              a191::abs(cp - low_.data()[idx])));
        if (idx + 1 < 7uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ti = tr_.size() - 1;
        line_.forward(a191::sma(tr_, ti, 6));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 7; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> tr_;
};

// ============================================================================
// Alpha#176: corr(rank((close - ts_min(low,12)) / (ts_max(high,12) - ts_min(low,12))),
//                 rank(volume), 6)
// Inputs: H, L, C, V   Warmup: 270
// ============================================================================
class Alpha191_176 : public Indicator<Alpha191_176> {
public:
    Alpha191_176(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            wpr_.data().reserve(n);
            rwpr_.data().reserve(n);
            rv_.data().reserve(n);
        }
        const auto idx = close_.index();
        if (idx + 1 < 12uz) [[unlikely]] {
            wpr_.forward(a191::NaN); rwpr_.forward(a191::NaN);
            rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));
            line_.forward(a191::NaN); return;
        }
        const double mn = a191::ts_min(low_, idx, 12);
        const double mx = a191::ts_max(high_, idx, 12);
        const double rng = mx - mn;
        wpr_.forward(rng != 0.0 ? (close_.data()[idx] - mn) / rng : 0.5);
        const auto wi = wpr_.size() - 1;
        rwpr_.forward(a191::ts_rank(wpr_, wi, std::min(wi + 1, 252uz)));
        rv_.forward(a191::ts_rank(volume_, idx, std::min(idx + 1, 252uz)));

        if (idx + 1 < 270uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = rwpr_.size() - 1, rvi = rv_.size() - 1;
        line_.forward(a191::correlation(rwpr_, rv_, std::min(ri, rvi), 6));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 270; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> wpr_, rwpr_, rv_;
};

// ============================================================================
// Alpha#177: (20 - highday(high, 20)) / 20 * 100
// Inputs: H   Warmup: 20
// ============================================================================
class Alpha191_177 : public Indicator<Alpha191_177> {
public:
    explicit Alpha191_177(const Line<double>& high) : high_(high) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward((20.0 - a191::highday(high_, idx, 20)) / 20.0 * 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& high_;
};

// ============================================================================
// Alpha#178: (close - delay(close, 1)) / delay(close, 1) * volume
// Inputs: C, V   Warmup: 2
// ============================================================================
class Alpha191_178 : public Indicator<Alpha191_178> {
public:
    Alpha191_178(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double cp = close_.data()[idx - 1];
        line_.forward(cp != 0.0 ? (close_.data()[idx] - cp) / cp * volume_.data()[idx] : a191::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

// ============================================================================
// Alpha#179: rank(corr(vwap, volume, 4)) * rank(corr(rank(low), rank(mean(volume,50)), 12))
// Inputs: H, L, C, V   Warmup: 315
// ============================================================================
class Alpha191_179 : public Indicator<Alpha191_179> {
public:
    Alpha191_179(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            vwap_l_.data().reserve(n);
            corr1_.data().reserve(n);
            rl_.data().reserve(n);
            mv50_.data().reserve(n);
            rmv_.data().reserve(n);
            corr2_.data().reserve(n);
        }
        const auto idx = close_.index();
        vwap_l_.forward(a191::vwap(high_, low_, close_, idx));
        const auto vi = vwap_l_.size() - 1;
        corr1_.forward(std::min(vi, idx) + 1 >= 4uz
            ? a191::correlation(vwap_l_, volume_, std::min(vi, idx), 4) : a191::NaN);

        rl_.forward(a191::ts_rank(low_, idx, std::min(idx + 1, 252uz)));
        mv50_.forward(a191::sma(volume_, idx, std::min(idx + 1, 50uz)));
        const auto mvi = mv50_.size() - 1;
        rmv_.forward(a191::ts_rank(mv50_, mvi, std::min(mvi + 1, 252uz)));
        const auto rli = rl_.size() - 1, rmi = rmv_.size() - 1;
        corr2_.forward(std::min(rli, rmi) + 1 >= 12uz
            ? a191::correlation(rl_, rmv_, std::min(rli, rmi), 12) : a191::NaN);

        if (idx + 1 < 315uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto c1i = corr1_.size() - 1, c2i = corr2_.size() - 1;
        const double r1 = a191::ts_rank(corr1_, c1i, std::min(c1i + 1, 252uz));
        const double r2 = a191::ts_rank(corr2_, c2i, std::min(c2i + 1, 252uz));
        line_.forward(r1 * r2);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 315; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> vwap_l_, corr1_;
    Line<double> rl_, mv50_, rmv_, corr2_;
};

// ============================================================================
// Alpha#180: IF(mean(volume,20) < volume,
//              -1*ts_rank(abs(delta(close,7)), 60)*sign(delta(close,7)),
//              -1*volume)
// Inputs: C, V   Warmup: 68
// ============================================================================
class Alpha191_180 : public Indicator<Alpha191_180> {
public:
    Alpha191_180(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            adc7_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        adc7_.forward(idx >= 7uz ? a191::abs(close_.data()[idx] - close_.data()[idx - 7]) : a191::NaN);

        if (idx + 1 < 68uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double mv20 = a191::sma(volume_, idx, 20);
        if (mv20 < volume_.data()[idx]) {
            const auto ai = adc7_.size() - 1;
            const double tr = a191::ts_rank(adc7_, ai, std::min(ai + 1, 60uz));
            const double d7 = close_.data()[idx] - close_.data()[idx - 7];
            line_.forward(-1.0 * tr * a191::sign(d7));
        } else {
            line_.forward(-1.0 * volume_.data()[idx]);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 68; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> adc7_;
};

// ============================================================================
// Alpha#181: SUM(close/delay(close,1)-1 - mean(close/delay(close,1)-1, 20), 20)
//            * (20-1)^(-1.5) * SUM((close/delay(close,1)-1)^2, 20)^(-1)
// Kurtosis-related measure of returns.
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha191_181 : public Indicator<Alpha191_181> {
public:
    explicit Alpha191_181(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ret_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] { ret_.forward(0.0); line_.forward(a191::NaN); return; }
        const double cp = close_.data()[idx - 1];
        ret_.forward(cp != 0.0 ? close_.data()[idx] / cp - 1.0 : 0.0);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = ret_.size() - 1;
        const double m = a191::sma(ret_, ri, 20);
        double s = 0.0, s2 = 0.0;
        for (std::size_t i = 0; i < 20uz; ++i) {
            const double r = ret_.data()[ri - 19 + i];
            s += r - m;
            s2 += r * r;
        }
        const double n1_inv = std::pow(19.0, -1.5);
        line_.forward(s2 != 0.0 ? s * n1_inv / s2 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    Line<double> ret_;
};

// ============================================================================
// Alpha#182: COUNT(close>open & benchmark_close>benchmark_open, 20) / 20
// Single-symbol: benchmark = self → COUNT(close>open, 20) / 20.
// Inputs: O, C   Warmup: 20
// ============================================================================
class Alpha191_182 : public Indicator<Alpha191_182> {
public:
    Alpha191_182(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            cond_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        cond_.forward(close_.data()[idx] > open_.data()[idx] ? 1.0 : 0.0);
        if (idx + 1 < 20uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ci = cond_.size() - 1;
        line_.forward(a191::sum(cond_, ci, 20) / 20.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 20; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> cond_;
};

// ============================================================================
// Alpha#183: max(SUM(close-mean(close,24)), -SUM(close-mean(close,24))) / stddev(close, 24)
// Same structure as #165 with period 24.
// Inputs: C   Warmup: 24
// ============================================================================
class Alpha191_183 : public Indicator<Alpha191_183> {
public:
    explicit Alpha191_183(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < 24uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const double m24 = a191::sma(close_, idx, 24);
        double s = 0.0;
        for (std::size_t i = 0; i < 24uz; ++i)
            s += close_.data()[idx - 23 + i] - m24;
        const double sd = a191::stddev(close_, idx, 24);
        line_.forward(sd != 0.0 ? a191::max(s, -s) / sd : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 24; }

private:
    const Line<double>& close_;
};

// ============================================================================
// Alpha#184: rank(corr(delay(open-close,1), close, 200)) + rank(open-close)
// Inputs: O, C   Warmup: 454
// ============================================================================
class Alpha191_184 : public Indicator<Alpha191_184> {
public:
    Alpha191_184(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            doc_.data().reserve(n);
            doc1_.data().reserve(n);
            corr_.data().reserve(n);
            oc_.data().reserve(n);
        }
        const auto idx = close_.index();
        doc_.forward(open_.data()[idx] - close_.data()[idx]);
        const auto di = doc_.size() - 1;
        doc1_.forward(di >= 1uz ? doc_.data()[di - 1] : a191::NaN);
        const auto d1i = doc1_.size() - 1;
        corr_.forward(std::min(d1i, idx) + 1 >= 200uz
            ? a191::correlation(doc1_, close_, std::min(d1i, idx), 200) : a191::NaN);
        oc_.forward(open_.data()[idx] - close_.data()[idx]);

        if (idx + 1 < 454uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ci = corr_.size() - 1, oi = oc_.size() - 1;
        const double rc = a191::ts_rank(corr_, ci, std::min(ci + 1, 252uz));
        const double ro = a191::ts_rank(oc_, oi, std::min(oi + 1, 252uz));
        line_.forward(rc + ro);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 454; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> doc_, doc1_, corr_, oc_;
};

// ============================================================================
// Alpha#185: rank(-1*(1-(open/close))^2)
// Inputs: O, C   Warmup: 253
// ============================================================================
class Alpha191_185 : public Indicator<Alpha191_185> {
public:
    Alpha191_185(const Line<double>& open, const Line<double>& close)
        : open_(open), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            val_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double c = close_.data()[idx];
        const double ratio = c != 0.0 ? open_.data()[idx] / c : 1.0;
        val_.forward(-1.0 * (1.0 - ratio) * (1.0 - ratio));
        if (idx + 1 < 253uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto vi = val_.size() - 1;
        line_.forward(a191::ts_rank(val_, vi, std::min(vi + 1, 252uz)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    Line<double> val_;
};

// ============================================================================
// Alpha#186: (mean(abs(SUM(LD>0&LD>HD?LD:0,14)*100/14
//            -SUM(HD>0&HD>LD?HD:0,14)*100/14)
//            /(SUM(LD>0&LD>HD?LD:0,14)*100/14
//            +SUM(HD>0&HD>LD?HD:0,14)*100/14)*100, 6)
//            + delay(mean(...),6)) / 2
// Smoothed ADX (same as #172 with delay-averaging).
// Inputs: H, L, C   Warmup: 28
// ============================================================================
class Alpha191_186 : public Indicator<Alpha191_186> {
public:
    Alpha191_186(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close)
        : high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            pdm_.data().reserve(close_.size());
            ndm_.data().reserve(close_.size());
            dx_.data().reserve(close_.size());
            adx_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            pdm_.forward(0.0); ndm_.forward(0.0); dx_.forward(0.0); adx_.forward(0.0);
            line_.forward(a191::NaN); return;
        }
        const double hd = high_.data()[idx] - high_.data()[idx - 1];
        const double ld = low_.data()[idx - 1] - low_.data()[idx];
        pdm_.forward(hd > 0.0 && hd > ld ? hd : 0.0);
        ndm_.forward(ld > 0.0 && ld > hd ? ld : 0.0);

        if (idx + 1 < 15uz) [[unlikely]] { dx_.forward(0.0); adx_.forward(0.0); line_.forward(a191::NaN); return; }
        const auto pi = pdm_.size() - 1, ni = ndm_.size() - 1;
        const double pdi = a191::sum(pdm_, pi, 14) * 100.0 / 14.0;
        const double ndi = a191::sum(ndm_, ni, 14) * 100.0 / 14.0;
        const double dxv = (pdi + ndi) != 0.0 ? a191::abs(pdi - ndi) / (pdi + ndi) * 100.0 : 0.0;
        dx_.forward(dxv);
        const auto di = dx_.size() - 1;
        adx_.forward(di + 1 >= 6uz ? a191::sma(dx_, di, 6) : a191::NaN);

        if (idx + 1 < 28uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ai = adx_.size() - 1;
        const double cur = adx_.data()[ai];
        const double prev = ai >= 6uz ? adx_.data()[ai - 6] : cur;
        line_.forward((cur + prev) / 2.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 28; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> pdm_, ndm_, dx_, adx_;
};

// ============================================================================
// Alpha#187: SUM(IF(open<=delay(open,1), 0, max(high-open, open-delay(open,1))), 20)
// Inputs: O, H   Warmup: 21
// ============================================================================
class Alpha191_187 : public Indicator<Alpha191_187> {
public:
    Alpha191_187(const Line<double>& open, const Line<double>& high)
        : open_(open), high_(high) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(open_.size());
            val_.data().reserve(open_.size());
        }
        const auto idx = open_.index();
        if (idx < 1uz) [[unlikely]] { val_.forward(0.0); line_.forward(a191::NaN); return; }
        const double o = open_.data()[idx], op = open_.data()[idx - 1];
        val_.forward(o <= op ? 0.0 : a191::max(high_.data()[idx] - o, o - op));
        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto vi = val_.size() - 1;
        line_.forward(a191::sum(val_, vi, 20));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    Line<double> val_;
};

// ============================================================================
// Alpha#188: (high - low - SMA(high - low, 11, 2)) / SMA(high - low, 11, 2)
// Inputs: H, L   Warmup: 11
// ============================================================================
class Alpha191_188 : public Indicator<Alpha191_188> {
public:
    Alpha191_188(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        const double hl = high_.data()[idx] - low_.data()[idx];
        ema_.update(hl, 11.0, 2.0);
        if (idx + 1 < 11uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        line_.forward(ema_.value != 0.0 ? (hl - ema_.value) / ema_.value : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 11; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    SmaState ema_;
};

// ============================================================================
// Alpha#189: mean(abs(close - mean(close, 6)), 6)
// Mean Absolute Deviation(6).
// Inputs: C   Warmup: 12
// ============================================================================
class Alpha191_189 : public Indicator<Alpha191_189> {
public:
    explicit Alpha191_189(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ad_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx + 1 < 6uz) [[unlikely]] {
            ad_.forward(0.0); line_.forward(a191::NaN); return;
        }
        const double m6 = a191::sma(close_, idx, 6);
        ad_.forward(a191::abs(close_.data()[idx] - m6));
        if (idx + 1 < 12uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ai = ad_.size() - 1;
        line_.forward(a191::sma(ad_, ai, 6));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 12; }

private:
    const Line<double>& close_;
    Line<double> ad_;
};

// ============================================================================
// Alpha#190: log((COUNT(close/delay(close,1)>((close/delay(close,1))^(1/252)),20)
//              *SUMIF(((close/delay(close,1))-1)^2,20,close/delay(close,1)<1)
//              /(COUNT(close/delay(close,1)<1,20)
//              *SUMIF((close/delay(close,1)-1)^2,20,close/delay(close,1)>1)))
// Gain/loss ratio log.
// Inputs: C   Warmup: 21
// ============================================================================
class Alpha191_190 : public Indicator<Alpha191_190> {
public:
    explicit Alpha191_190(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            ratio_.data().reserve(close_.size());
            rsq_.data().reserve(close_.size());
            up_.data().reserve(close_.size());
            dn_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        if (idx < 1uz) [[unlikely]] {
            ratio_.forward(1.0); rsq_.forward(0.0); up_.forward(0.0); dn_.forward(0.0);
            line_.forward(a191::NaN); return;
        }
        const double cp = close_.data()[idx - 1];
        const double r = cp != 0.0 ? close_.data()[idx] / cp : 1.0;
        ratio_.forward(r);
        const double sq = (r - 1.0) * (r - 1.0);
        rsq_.forward(sq);
        up_.forward(r > 1.0 ? 1.0 : 0.0);
        dn_.forward(r < 1.0 ? 1.0 : 0.0);

        if (idx + 1 < 21uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto ri = ratio_.size() - 1;
        const auto si = rsq_.size() - 1;
        const double thresh = std::pow(ratio_.data()[ri], 1.0 / 252.0);
        double cnt_up = 0.0, cnt_dn = 0.0, sum_loss_sq = 0.0, sum_gain_sq = 0.0;
        for (std::size_t i = 0; i < 20uz; ++i) {
            const double rv = ratio_.data()[ri - 19 + i];
            const double sq_v = rsq_.data()[si - 19 + i];
            if (rv > thresh) cnt_up += 1.0;
            if (rv < 1.0) { cnt_dn += 1.0; sum_loss_sq += sq_v; }
            if (rv > 1.0) sum_gain_sq += sq_v;
        }
        const double denom = cnt_dn * sum_gain_sq;
        const double num = cnt_up * sum_loss_sq;
        line_.forward(denom > 0.0 && num > 0.0 ? a191::log(num / denom) : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 21; }

private:
    const Line<double>& close_;
    Line<double> ratio_, rsq_, up_, dn_;
};

// ============================================================================
// Alpha#191: corr(mean(volume,20), low, 5) + (high+low)/2 - close
// Inputs: H, L, C, V   Warmup: 25
// ============================================================================
class Alpha191_191 : public Indicator<Alpha191_191> {
public:
    Alpha191_191(const Line<double>& high, const Line<double>& low,
                 const Line<double>& close, const Line<double>& volume)
        : high_(high), low_(low), close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            mv20_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        mv20_.forward(a191::sma(volume_, idx, std::min(idx + 1, 20uz)));
        if (idx + 1 < 25uz) [[unlikely]] { line_.forward(a191::NaN); return; }
        const auto mi = mv20_.size() - 1;
        const double c = a191::correlation(mv20_, low_, std::min(mi, idx), 5);
        line_.forward(c + (high_.data()[idx] + low_.data()[idx]) / 2.0 - close_.data()[idx]);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 25; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    const Line<double>& volume_;
    Line<double> mv20_;
};

} // namespace stratforge
