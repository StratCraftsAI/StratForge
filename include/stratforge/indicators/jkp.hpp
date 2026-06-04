#pragma once

// Jensen, Kelly & Pedersen (2023) academic factor library.
// 25 factors across momentum, reversal, low-risk, and seasonality.
//
// Single-symbol approximations:
//   market_return → SMA(return, 20) smoothed proxy (no benchmark index)
//   beta/coskew   → computed vs smoothed market proxy
//   residual      → OLS residual vs market proxy
//   seasonality   → bar-index-based proxies (no calendar dates)
//
// All factors are cross-sectional in the original paper.
// In single-symbol mode, ranking is implicit (output is raw signal).

#include <stratforge/indicators/jkp_ops.hpp>
#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

namespace jkpns = jkp;

// ============================================================================
// JKP_Mom12M: 12-Month Momentum = return(252) - return(21)
// Jegadeesh & Titman (1993)
// Inputs: C   Warmup: 253
// ============================================================================
class JKP_Mom12M : public Indicator<JKP_Mom12M> {
public:
    explicit JKP_Mom12M(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 252uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double r252 = jkpns::ret(close_, idx, 252);
        const double r21 = jkpns::ret(close_, idx, 21);
        line_.forward(std::isnan(r252) || std::isnan(r21) ? jkpns::NaN : r252 - r21);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_Mom6M: 6-Month Momentum = return(126) - return(21)
// Jegadeesh & Titman (1993)
// Inputs: C   Warmup: 127
// ============================================================================
class JKP_Mom6M : public Indicator<JKP_Mom6M> {
public:
    explicit JKP_Mom6M(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 126uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double r126 = jkpns::ret(close_, idx, 126);
        const double r21 = jkpns::ret(close_, idx, 21);
        line_.forward(std::isnan(r126) || std::isnan(r21) ? jkpns::NaN : r126 - r21);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 127; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_Mom1M: 1-Month Momentum = return(21)
// Jegadeesh & Titman (1993)
// Inputs: C   Warmup: 22
// ============================================================================
class JKP_Mom1M : public Indicator<JKP_Mom1M> {
public:
    explicit JKP_Mom1M(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 21uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::ret(close_, idx, 21));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 22; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_Mom36M: 36-Month Momentum = return(756) - return(252)
// De Bondt & Thaler (1985)
// Inputs: C   Warmup: 757
// ============================================================================
class JKP_Mom36M : public Indicator<JKP_Mom36M> {
public:
    explicit JKP_Mom36M(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 756uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double r756 = jkpns::ret(close_, idx, 756);
        const double r252 = jkpns::ret(close_, idx, 252);
        line_.forward(std::isnan(r756) || std::isnan(r252) ? jkpns::NaN : r756 - r252);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 757; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_MomRevLT: Long-Term Reversal = return(756)
// De Bondt & Thaler (1985)
// Inputs: C   Warmup: 757
// ============================================================================
class JKP_MomRevLT : public Indicator<JKP_MomRevLT> {
public:
    explicit JKP_MomRevLT(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 756uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::ret(close_, idx, 756));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 757; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_MomInt: Intermediate Horizon Momentum = return(168) - return(21)
// Novy-Marx (2012)
// Inputs: C   Warmup: 169
// ============================================================================
class JKP_MomInt : public Indicator<JKP_MomInt> {
public:
    explicit JKP_MomInt(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 168uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double r168 = jkpns::ret(close_, idx, 168);
        const double r21 = jkpns::ret(close_, idx, 21);
        line_.forward(std::isnan(r168) || std::isnan(r21) ? jkpns::NaN : r168 - r21);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 169; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_52WHigh: 52-Week High = close / ts_max(high, 252)
// George & Hwang (2004)
// Inputs: H, C   Warmup: 252
// ============================================================================
class JKP_52WHigh : public Indicator<JKP_52WHigh> {
public:
    JKP_52WHigh(const Line<double>& high, const Line<double>& close)
        : high_(high), close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx + 1 < 252uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double mx = jkpns::ts_max(high_, idx, 252);
        line_.forward(mx > 0.0 ? close_.data()[idx] / mx : jkpns::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 252; }

private:
    const Line<double>& high_;
    const Line<double>& close_;
};

// ============================================================================
// JKP_MomOff: Momentum Change (Acceleration) = return(126) - return(126, lag=126)
// = return(close,126) - (close[-126]-close[-252])/close[-252]
// Gettleman & Marks (2006)
// Inputs: C   Warmup: 253
// ============================================================================
class JKP_MomOff : public Indicator<JKP_MomOff> {
public:
    explicit JKP_MomOff(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 252uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double r_now = jkpns::ret(close_, idx, 126);
        const double prev_close = close_.data()[idx - 126];
        const double prev_prev = close_.data()[idx - 252];
        const double r_lag = (prev_prev != 0.0) ? (prev_close - prev_prev) / prev_prev : jkpns::NaN;
        line_.forward(std::isnan(r_now) || std::isnan(r_lag) ? jkpns::NaN : r_now - r_lag);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_VolTrend: Volume Trend = sma(volume,21) / sma(volume,126)
// Haugen & Baker (1996)
// Inputs: V   Warmup: 126
// ============================================================================
class JKP_VolTrend : public Indicator<JKP_VolTrend> {
public:
    explicit JKP_VolTrend(const Line<double>& volume) : volume_(volume) {}

    void next_impl() {
        const auto idx = volume_.index();
        if (idx + 1 < 126uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double short_ma = jkpns::sma(volume_, idx, 21);
        const double long_ma = jkpns::sma(volume_, idx, 126);
        line_.forward(long_ma > 0.0 ? short_ma / long_ma : jkpns::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 126; }

private:
    const Line<double>& volume_;
};

// ============================================================================
// JKP_PriceDelay: Price Delay = 1 - R²(ret~ret[-1]) / R²(ret~ret[-1..5])
// Approximation: R²(1-lag AR) vs R²(5-lag AR). In single-symbol mode
// we regress returns on 1-bar-lagged returns for both, using period=1 vs 5.
// Hou & Moskowitz (2005)
// Inputs: C   Warmup: 258 (252 returns + 5 lags + 1)
// ============================================================================
class JKP_PriceDelay : public Indicator<JKP_PriceDelay> {
public:
    explicit JKP_PriceDelay(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            ret_lag1_.data().reserve(n);
            ret_lag5_.data().reserve(n);
        }
        const auto idx = close_.index();

        const double r = jkpns::returns(close_, idx);
        ret_.forward(r);
        const auto ri = ret_.size() - 1;
        ret_lag1_.forward(ri >= 1 ? ret_.data()[ri - 1] : jkpns::NaN);
        ret_lag5_.forward(ri >= 5 ? ret_.data()[ri - 5] : jkpns::NaN);

        if (idx + 1 < 258uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }

        const double r2_1 = jkpns::r_squared(ret_, ret_lag1_, ri, 252);
        const double r2_5 = jkpns::r_squared(ret_, ret_lag5_, ri, 252);
        if (std::isnan(r2_1) || std::isnan(r2_5) || r2_5 == 0.0) {
            line_.forward(jkpns::NaN);
            return;
        }
        line_.forward(1.0 - r2_1 / r2_5);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 258; }

private:
    const Line<double>& close_;
    Line<double> ret_, ret_lag1_, ret_lag5_;
};

// ============================================================================
// JKP_Rev1M: Short-Term Reversal = -return(21)
// Jegadeesh (1990)
// Inputs: C   Warmup: 22
// ============================================================================
class JKP_Rev1M : public Indicator<JKP_Rev1M> {
public:
    explicit JKP_Rev1M(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 21uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double r = jkpns::ret(close_, idx, 21);
        line_.forward(std::isnan(r) ? jkpns::NaN : -r);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 22; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_Rev1W: Weekly Reversal = -return(5)
// Lehmann (1990)
// Inputs: C   Warmup: 6
// ============================================================================
class JKP_Rev1W : public Indicator<JKP_Rev1W> {
public:
    explicit JKP_Rev1W(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 5uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double r = jkpns::ret(close_, idx, 5);
        line_.forward(std::isnan(r) ? jkpns::NaN : -r);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 6; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_RevIdio: Idiosyncratic Reversal = -residual_return(close, 21, market)
// In single-symbol mode: market = SMA(return, 20) as smoothed proxy.
// Residual = return - beta * market_proxy.
// Da et al. (2014)
// Inputs: C   Warmup: 43 (21 returns + 21 for SMA + 1)
// ============================================================================
class JKP_RevIdio : public Indicator<JKP_RevIdio> {
public:
    explicit JKP_RevIdio(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            mkt_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        mkt_.forward(jkpns::sma(ret_, ri, std::min(ri + 1, 20uz)));

        if (idx + 1 < 43uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }

        const double b = jkpns::beta(ret_, mkt_, ri, 21);
        if (std::isnan(b)) { line_.forward(jkpns::NaN); return; }
        const double residual = ret_.data()[ri] - b * mkt_.data()[ri];
        line_.forward(-residual);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 43; }

private:
    const Line<double>& close_;
    Line<double> ret_, mkt_;
};

// ============================================================================
// JKP_Beta60M: Market Beta (60-Month) = beta(return, market_return, 1260)
// Single-symbol: market = SMA(return, 20)
// Fama & MacBeth (1973)
// Inputs: C   Warmup: 1282
// ============================================================================
class JKP_Beta60M : public Indicator<JKP_Beta60M> {
public:
    explicit JKP_Beta60M(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            mkt_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        mkt_.forward(jkpns::sma(ret_, ri, std::min(ri + 1, 20uz)));

        if (idx + 1 < 1282uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::beta(ret_, mkt_, ri, 1260));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1282; }

private:
    const Line<double>& close_;
    Line<double> ret_, mkt_;
};

// ============================================================================
// JKP_BetaDM: Dimson Beta (Lagged) = sum of betas at lags 0..5
// beta_dimson = beta(ret, mkt, 252) + sum(beta(ret, mkt_lag_k, 252) for k=1..5)
// Single-symbol: market = SMA(return, 20)
// Dimson (1979)
// Inputs: C   Warmup: 279 (1 ret + 20 sma + 5 lags + 252 window + 1)
// ============================================================================
class JKP_BetaDM : public Indicator<JKP_BetaDM> {
public:
    explicit JKP_BetaDM(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            mkt_.data().reserve(n);
            for (auto& lg : mkt_lag_)
                lg.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        mkt_.forward(jkpns::sma(ret_, ri, std::min(ri + 1, 20uz)));
        const auto mi = mkt_.size() - 1;
        for (std::size_t k = 0; k < 5; ++k)
            mkt_lag_[k].forward(mi >= (k + 1) ? mkt_.data()[mi - k - 1] : jkpns::NaN);

        if (idx + 1 < 279uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }

        double b_sum = jkpns::beta(ret_, mkt_, ri, 252);
        if (std::isnan(b_sum)) { line_.forward(jkpns::NaN); return; }
        for (std::size_t k = 0; k < 5; ++k) {
            const auto li = mkt_lag_[k].size() - 1;
            const double bk = jkpns::beta(ret_, mkt_lag_[k], std::min(ri, li), 252);
            if (std::isnan(bk)) { line_.forward(jkpns::NaN); return; }
            b_sum += bk;
        }
        line_.forward(b_sum);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 279; }

private:
    const Line<double>& close_;
    Line<double> ret_, mkt_;
    Line<double> mkt_lag_[5];
};

// ============================================================================
// JKP_BetaDown: Downside Beta = beta(return, market, 252, cond=market<0)
// Single-symbol: market = SMA(return, 20)
// Ang et al. (2006)
// Inputs: C   Warmup: 274 (1 ret + 20 sma + 252 window + 1)
// ============================================================================
class JKP_BetaDown : public Indicator<JKP_BetaDown> {
public:
    explicit JKP_BetaDown(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            mkt_.data().reserve(n);
            cond_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        const double m = jkpns::sma(ret_, ri, std::min(ri + 1, 20uz));
        mkt_.forward(m);
        cond_.forward(m < 0.0 ? 1.0 : 0.0);

        if (idx + 1 < 274uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::beta_cond(ret_, mkt_, cond_, ri, 252));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 274; }

private:
    const Line<double>& close_;
    Line<double> ret_, mkt_, cond_;
};

// ============================================================================
// JKP_IVolCAPM: Idiosyncratic Volatility (CAPM)
// = std(residual(return, market_return), 252)
// Single-symbol: market = SMA(return, 20)
// Ang et al. (2006)
// Inputs: C   Warmup: 274
// ============================================================================
class JKP_IVolCAPM : public Indicator<JKP_IVolCAPM> {
public:
    explicit JKP_IVolCAPM(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            mkt_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        mkt_.forward(jkpns::sma(ret_, ri, std::min(ri + 1, 20uz)));

        if (idx + 1 < 274uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::residual_std(ret_, mkt_, ri, 252));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 274; }

private:
    const Line<double>& close_;
    Line<double> ret_, mkt_;
};

// ============================================================================
// JKP_TVol: Total Volatility = std(return, 252)
// Ang et al. (2006)
// Inputs: C   Warmup: 254 (1 ret + 252 window + 1)
// ============================================================================
class JKP_TVol : public Indicator<JKP_TVol> {
public:
    explicit JKP_TVol(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;

        if (ri + 1 < 252uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::stddev(ret_, ri, 252));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& close_;
    Line<double> ret_;
};

// ============================================================================
// JKP_RMax5: Maximum Daily Return (5-Day) = ts_max(return, 5)
// Bali et al. (2011)
// Inputs: C   Warmup: 7
// ============================================================================
class JKP_RMax5 : public Indicator<JKP_RMax5> {
public:
    explicit JKP_RMax5(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;

        if (ri + 1 < 5uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::ts_max(ret_, ri, 5));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 7; }

private:
    const Line<double>& close_;
    Line<double> ret_;
};

// ============================================================================
// JKP_RMax21: Maximum Daily Return (21-Day) = ts_max(return, 21)
// Bali et al. (2011)
// Inputs: C   Warmup: 23
// ============================================================================
class JKP_RMax21 : public Indicator<JKP_RMax21> {
public:
    explicit JKP_RMax21(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;

        if (ri + 1 < 21uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::ts_max(ret_, ri, 21));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 23; }

private:
    const Line<double>& close_;
    Line<double> ret_;
};

// ============================================================================
// JKP_Skew: Return Skewness = skewness(return, 252)
// Bali et al. (2016)
// Inputs: C   Warmup: 254
// ============================================================================
class JKP_Skew : public Indicator<JKP_Skew> {
public:
    explicit JKP_Skew(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;

        if (ri + 1 < 252uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::skewness(ret_, ri, 252));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 253; }

private:
    const Line<double>& close_;
    Line<double> ret_;
};

// ============================================================================
// JKP_Coskew: Coskewness = coskew(return, market_return, 252)
// Single-symbol: market = SMA(return, 20)
// Harvey & Siddique (2000)
// Inputs: C   Warmup: 274
// ============================================================================
class JKP_Coskew : public Indicator<JKP_Coskew> {
public:
    explicit JKP_Coskew(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
            mkt_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;
        mkt_.forward(jkpns::sma(ret_, ri, std::min(ri + 1, 20uz)));

        if (idx + 1 < 274uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::coskewness(ret_, mkt_, ri, 252));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 274; }

private:
    const Line<double>& close_;
    Line<double> ret_, mkt_;
};

// ============================================================================
// JKP_RVol21: Realized Volatility (21-Day) = std(return, 21)
// Ang et al. (2006)
// Inputs: C   Warmup: 23
// ============================================================================
class JKP_RVol21 : public Indicator<JKP_RVol21> {
public:
    explicit JKP_RVol21(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;

        if (ri + 1 < 21uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        line_.forward(jkpns::stddev(ret_, ri, 21));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 23; }

private:
    const Line<double>& close_;
    Line<double> ret_;
};

// ============================================================================
// JKP_SeasYr: Return Seasonality (Year-on-Year)
// = mean of returns at same annual lag over trailing history
// Approximation: average of lag-252 and lag-504 returns (2-year lookback)
// since bar-based data has no calendar month information.
// Heston & Sadka (2008)
// Inputs: C   Warmup: 505
// ============================================================================
class JKP_SeasYr : public Indicator<JKP_SeasYr> {
public:
    explicit JKP_SeasYr(const Line<double>& close) : close_(close) {}

    void next_impl() {
        const auto idx = close_.index();
        if (idx < 504uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }
        const double r1 = jkpns::ret(close_, idx - 252 + 21, 21);
        const double r2 = jkpns::ret(close_, idx - 504 + 21, 21);
        if (std::isnan(r1) || std::isnan(r2)) {
            line_.forward(jkpns::NaN);
            return;
        }
        line_.forward((r1 + r2) / 2.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 505; }

private:
    const Line<double>& close_;
};

// ============================================================================
// JKP_SeasTOM: Turn-of-Month Effect
// = mean of returns over bars [-1..+3] relative to 21-bar month boundary
// Approximation: average return over a 5-bar window at multiples of 21 bars.
// Without calendar data, we use bar-index modular arithmetic as proxy.
// Ariel (1990)
// Inputs: C   Warmup: 23
// ============================================================================
class JKP_SeasTOM : public Indicator<JKP_SeasTOM> {
public:
    explicit JKP_SeasTOM(const Line<double>& close) : close_(close) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            ret_.data().reserve(n);
        }
        const auto idx = close_.index();
        ret_.forward(jkpns::returns(close_, idx));
        const auto ri = ret_.size() - 1;

        if (ri + 1 < 23uz) [[unlikely]] { line_.forward(jkpns::NaN); return; }

        double s = 0.0;
        std::size_t cnt = 0;
        for (std::size_t offset : {0uz, 21uz}) {
            if (ri >= offset + 3) {
                for (std::size_t k = 0; k < 4; ++k) {
                    const double r = ret_.data()[ri - offset - k];
                    if (!std::isnan(r)) { s += r; ++cnt; }
                }
            }
        }
        line_.forward(cnt > 0 ? s / static_cast<double>(cnt) : jkpns::NaN);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 23; }

private:
    const Line<double>& close_;
    Line<double> ret_;
};

} // namespace stratforge
