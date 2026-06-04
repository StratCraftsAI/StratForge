#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Schaff Trend Cycle: MACD passed through double Stochastic smoothing.
class STC : public Indicator<STC> {
public:
    explicit STC(const Line<double>& source,
                 std::size_t period = 10uz,
                 std::size_t fast = 23uz,
                 std::size_t slow = 50uz,
                 double factor = 0.5)
        : source_(source)
        , period_(period == 0 ? 1 : period)
        , fast_(fast == 0 ? 1 : fast)
        , slow_(slow == 0 ? 1 : slow)
        , factor_(factor)
        , fast_mult_(2.0 / (static_cast<double>(fast_) + 1.0))
        , slow_mult_(2.0 / (static_cast<double>(slow_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(source_.size());
            macd_.data().reserve(source_.size());
        }
        const auto idx = source_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();

        if (idx + 1 < fast_) {
            macd_.forward(nan);
            line_.forward(nan);
            return;
        }

        if (!fast_init_) {
            double s = 0.0;
            for (std::size_t i = 0; i < fast_; ++i) s += source_.data()[idx - i];
            fast_ema_ = s / static_cast<double>(fast_);
            fast_init_ = true;
        } else {
            fast_ema_ = (source_.data()[idx] - fast_ema_) * fast_mult_ + fast_ema_;
        }

        if (idx + 1 < slow_) {
            macd_.forward(nan);
            line_.forward(nan);
            return;
        }

        if (!slow_init_) {
            double s = 0.0;
            for (std::size_t i = 0; i < slow_; ++i) s += source_.data()[idx - i];
            slow_ema_ = s / static_cast<double>(slow_);
            slow_init_ = true;
        } else {
            slow_ema_ = (source_.data()[idx] - slow_ema_) * slow_mult_ + slow_ema_;
        }

        const double m = fast_ema_ - slow_ema_;
        macd_.forward(m);

        if (macd_.size() < period_) {
            line_.forward(nan);
            return;
        }

        double hh = m, ll = m;
        for (std::size_t i = 1; i < period_; ++i) {
            const double v = macd_.data()[macd_.size() - 1 - i];
            if (std::isnan(v)) { line_.forward(nan); return; }
            hh = std::max(hh, v);
            ll = std::min(ll, v);
        }

        const double stoch1 = (hh == ll) ? 50.0 : 100.0 * (m - ll) / (hh - ll);
        pf_ = pf_ + factor_ * (stoch1 - pf_);

        pf_hist_.push_back(pf_);
        if (pf_hist_.size() > period_) pf_hist_.erase(pf_hist_.begin());

        if (pf_hist_.size() < period_) {
            line_.forward(nan);
            return;
        }

        double pfh = pf_hist_[0], pfl = pf_hist_[0];
        for (std::size_t i = 1; i < pf_hist_.size(); ++i) {
            pfh = std::max(pfh, pf_hist_[i]);
            pfl = std::min(pfl, pf_hist_[i]);
        }
        const double stoch2 = (pfh == pfl) ? 50.0 : 100.0 * (pf_ - pfl) / (pfh - pfl);
        pff_ = pff_ + factor_ * (stoch2 - pff_);

        line_.forward(std::clamp(pff_, 0.0, 100.0));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return slow_ + period_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
    std::size_t fast_;
    std::size_t slow_;
    double factor_;
    double fast_mult_;
    double slow_mult_;
    double fast_ema_ = 0.0;
    double slow_ema_ = 0.0;
    double pf_ = 50.0;
    double pff_ = 50.0;
    bool fast_init_ = false;
    bool slow_init_ = false;
    Line<double> macd_;
    std::vector<double> pf_hist_;
};

using SchaffTrendCycle = STC;

} // namespace stratforge
