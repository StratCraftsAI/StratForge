#pragma once

// : Frozen sanitized Larry Williams strategy fixture.
//
// Represents a reviewed "Williams %R" rules shape that an LLM generation flow
// admitted and reviewed. This fixture exists so the StratForge integration can
// prove — independent of any upstream generation-service correction — that the
// reviewed rules shape produces admissible StratForge C++ against the current
// API and the ABI v2 factory contract.
//
// Sanitized: no prompts, provider responses, credentials, or reasoning content.
// It is a plain StratForge strategy, a first-class open-source test deliverable.
//
// Rules (Larry Williams %R mean-reversion):
//   - Indicator: WilliamsR(high, low, close, period)
//   - Enter long when %R crosses up out of oversold (<= -80).
//   - Exit long when %R crosses up into overbought (>= -20).

#include <stratforge/broker/sizer.hpp>
#include <stratforge/indicators/williams.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <memory>

namespace stratforge::fixtures {

/// Sanitized Larry Williams %R strategy — the reviewed production rules shape.
class WilliamsRStrategy final : public ::stratforge::Strategy {
public:
    static constexpr std::size_t kPeriod = 14;
    static constexpr double kOversold = -80.0;
    static constexpr double kOverbought = -20.0;

    void init() override {
        wr_ = std::make_unique<::stratforge::WilliamsR>(
            data().high(), data().low(), data().close(), kPeriod);
        // 50% of portfolio value per entry. Sizing is computed from the close,
        // but a market order fills at the next bar's open; a conservative
        // fraction leaves margin headroom so entries are not rejected on an
        // up-gap (backtrader-consistent margin rejection otherwise applies).
        setsizer(std::make_unique<::stratforge::PercentSizer>(50.0));
    }

    void next() override {
        wr_->next();
        if (wr_->line().size() < kPeriod) {
            return; // warmup
        }

        const double curr = wr_->line()[0];
        const double prev = wr_->line()[-1];

        // Entry: %R crosses up out of the oversold zone.
        if (!position().size && prev <= kOversold && curr > kOversold) {
            static_cast<void>(buy());
            ++entries_;
        }
        // Exit: %R crosses up into the overbought zone.
        else if (position().size > 0.0 && prev < kOverbought && curr >= kOverbought) {
            static_cast<void>(close());
            ++exits_;
        }
    }

    [[nodiscard]] int entries() const noexcept { return entries_; }
    [[nodiscard]] int exits() const noexcept { return exits_; }

private:
    std::unique_ptr<::stratforge::WilliamsR> wr_;
    int entries_ = 0;
    int exits_ = 0;
};

} // namespace stratforge::fixtures
