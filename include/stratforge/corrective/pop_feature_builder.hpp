// SPDX-License-Identifier: MIT
//
//  P1: Self-contained PoP feature vector builder.
//
// Owns its own indicator instances (RSI, ATR, BB%B, MACD, SMA, StdDev)
// bound to raw DataFeed lines. Independent of Strategy indicators.
// Shared by backtest and live modes (D4).

#pragma once

#include <stratforge/broker/broker.hpp>
#include <stratforge/corrective/corrective_contracts.hpp>
#include <stratforge/data/data_feed.hpp>
#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/bollingerpct.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/stddev.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

namespace stratforge::corrective {

class PopFeatureBuilder {
public:
    void init(const std::vector<DataFeed*>& feeds) {
        feed_indicators_.clear();
        feed_indicators_.reserve(feeds.size());
        for (auto* feed : feeds) {
            feed_indicators_.push_back(std::make_unique<FeedIndicators>(
                feed->close(), feed->high(), feed->low(), feed->volume()));
        }
        initialized_ = true;
        bars_since_last_trade_ = 0;
        equity_peak_ = 0.0;
    }

    void advance() {
        for (auto& fi : feed_indicators_) {
            fi->advance();
        }
        ++bars_since_last_trade_;
    }

    void record_trade() noexcept {
        bars_since_last_trade_ = 0;
    }

    [[nodiscard]] std::array<float, kPopFeatureCountV1> build(
        OrderSide side,
        double proposed_size,
        std::size_t data_index,
        const std::vector<DataFeed*>& feeds,
        const BackBroker& broker) const
    {
        std::array<float, kPopFeatureCountV1> fv{};
        if (!initialized_ || data_index >= feeds.size() ||
            data_index >= feed_indicators_.size()) {
            return fv;
        }

        const auto& fi = *feed_indicators_[data_index];
        const auto* feed = feeds[data_index];
        const double close = feed->close()[0];
        const double initial = broker.initial_cash();

        fv[idx(PopFeatureIndex::kProposedSizeNormalized)] =
            safe_float(initial > 0.0 ? proposed_size / initial : 0.0);

        fv[idx(PopFeatureIndex::kEntryPrice)] = safe_float(close);

        fv[idx(PopFeatureIndex::kBidAskSpreadBps)] = 0.0f;  // no bid/ask in backtest

        fv[idx(PopFeatureIndex::kAtrRatio)] =
            safe_float(close > 0.0 ? fi.atr_val() / close : 0.0);

        fv[idx(PopFeatureIndex::kVolatilityZ)] = safe_float(fi.volatility_z());

        fv[idx(PopFeatureIndex::kRsi14)] = safe_float(fi.rsi_val());

        fv[idx(PopFeatureIndex::kBbPercentB)] = safe_float(fi.bb_pctb_val());

        fv[idx(PopFeatureIndex::kMacdHistogram)] = safe_float(fi.macd_histo_val());

        fv[idx(PopFeatureIndex::kVolumeRatio)] = safe_float(fi.volume_ratio());

        const double pv = broker.portfolio_value(feeds);
        const double cash = broker.cash();
        const double unrealized = pv - cash;
        fv[idx(PopFeatureIndex::kUnrealizedPnlNorm)] =
            safe_float(initial > 0.0 ? unrealized / initial : 0.0);

        std::uint32_t open_count = 0;
        for (const auto& [trade, _] : broker.open_trades()) {
            if (trade.is_open()) ++open_count;
        }
        fv[idx(PopFeatureIndex::kOpenPositionCount)] =
            static_cast<float>(open_count);

        fv[idx(PopFeatureIndex::kBarsSinceLastTrade)] =
            static_cast<float>(bars_since_last_trade_);

        const double dd = compute_drawdown_pct_(pv);
        fv[idx(PopFeatureIndex::kCurrentDrawdownPct)] = safe_float(dd);

        fv[idx(PopFeatureIndex::kSideEncoded)] =
            (side == OrderSide::Buy) ? 1.0f : -1.0f;

        return fv;
    }

    void update_equity_peak(double portfolio_value) noexcept {
        if (portfolio_value > equity_peak_) {
            equity_peak_ = portfolio_value;
        }
    }

private:
    struct FeedIndicators {
        explicit FeedIndicators(
            Line<double>& close,
            Line<double>& high,
            Line<double>& low,
            Line<double>& volume)
            : rsi(close, 14)
            , atr(high, low, close, 14)
            , bb_pct(close, 20, 2.0)
            , macd(close, 12, 26, 9)
            , vol_sma(volume, 20)
            , close_sma(close, 20)
            , close_stddev(close, 20)
            , close_ref(close)
            , volume_ref(volume) {}

        void advance() {
            rsi.next();
            atr.next();
            bb_pct.next();
            macd.next();
            vol_sma.next();
            close_sma.next();
            close_stddev.next();
        }

        [[nodiscard]] double rsi_val() const noexcept {
            return last_val(rsi.line());
        }

        [[nodiscard]] double atr_val() const noexcept {
            return last_val(atr.line());
        }

        [[nodiscard]] double bb_pctb_val() const noexcept {
            return last_val(bb_pct.pctb());
        }

        [[nodiscard]] double macd_histo_val() const noexcept {
            return last_val(macd.histogram());
        }

        [[nodiscard]] double volume_ratio() const noexcept {
            const double vol = volume_ref.empty() ? 0.0 : volume_ref.data().back();
            const double avg = last_val(vol_sma.line());
            return (std::isfinite(avg) && avg > 0.0) ? vol / avg : 0.0;
        }

        [[nodiscard]] double volatility_z() const noexcept {
            const double c = close_ref.empty() ? 0.0 : close_ref.data().back();
            const double sma = last_val(close_sma.line());
            const double sd = last_val(close_stddev.line());
            if (!std::isfinite(sma) || !std::isfinite(sd) || sd < 1e-15) return 0.0;
            return (c - sma) / sd;
        }

        RSI              rsi;
        ATR              atr;
        BollingerBandsPct bb_pct;
        MACD             macd;
        SMA              vol_sma;
        SMA              close_sma;
        StdDev           close_stddev;
        Line<double>&    close_ref;
        Line<double>&    volume_ref;

    private:
        [[nodiscard]] static double last_val(const Line<double>& line) noexcept {
            if (line.empty()) return 0.0;
            const double v = line.data().back();
            return std::isfinite(v) ? v : 0.0;
        }
    };

    static constexpr std::size_t idx(PopFeatureIndex i) noexcept {
        return static_cast<std::size_t>(i);
    }

    [[nodiscard]] static float safe_float(double v) noexcept {
        return std::isfinite(v) ? static_cast<float>(v) : 0.0f;
    }

    [[nodiscard]] double compute_drawdown_pct_(double pv) const noexcept {
        if (equity_peak_ <= 0.0) return 0.0;
        return (equity_peak_ - pv) / equity_peak_ * 100.0;
    }

    std::vector<std::unique_ptr<FeedIndicators>> feed_indicators_;
    bool initialized_ = false;
    std::size_t bars_since_last_trade_ = 0;
    mutable double equity_peak_ = 0.0;
};

} // namespace stratforge::corrective
