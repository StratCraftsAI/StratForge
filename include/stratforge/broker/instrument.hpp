#pragma once

#include <stratforge/broker/currency.hpp>
#include <stratforge/broker/venue.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace stratforge {

enum class SecurityType : std::uint8_t {
    CommonStock,
    Future,
    FxSpot,
    OptionCall,
    OptionPut,
    CorporateBond,
};

class Instrument {
public:
    virtual ~Instrument() = default;

    [[nodiscard]] virtual SecurityType type() const noexcept = 0;
    [[nodiscard]] virtual std::string_view symbol() const noexcept = 0;
    [[nodiscard]] virtual const Venue& venue() const noexcept = 0;
    [[nodiscard]] virtual Currency settlement_ccy() const noexcept = 0;

    [[nodiscard]] virtual double notional(double qty, double mark_px) const noexcept = 0;
    [[nodiscard]] virtual double mark_to_market(double qty, double avg_px,
                                                  double mark_px) const noexcept = 0;
    [[nodiscard]] virtual double initial_margin(double qty,
                                                  double mark_px) const noexcept = 0;
    [[nodiscard]] virtual double maintenance_margin(double qty,
                                                     double mark_px) const noexcept = 0;
};

class Equity final : public Instrument {
public:
    Equity(std::string symbol, const Venue& venue, Currency ccy) noexcept
        : symbol_(std::move(symbol)), venue_(venue), ccy_(ccy) {}

    [[nodiscard]] SecurityType type() const noexcept override { return SecurityType::CommonStock; }
    [[nodiscard]] std::string_view symbol() const noexcept override { return symbol_; }
    [[nodiscard]] const Venue& venue() const noexcept override { return venue_; }
    [[nodiscard]] Currency settlement_ccy() const noexcept override { return ccy_; }

    [[nodiscard]] double notional(double qty, double mark_px) const noexcept override {
        return qty * mark_px;
    }
    [[nodiscard]] double mark_to_market(double qty, double avg_px,
                                          double mark_px) const noexcept override {
        return qty * (mark_px - avg_px);
    }
    [[nodiscard]] double initial_margin(double qty, double mark_px) const noexcept override {
        return 0.5 * std::abs(qty) * mark_px;
    }
    [[nodiscard]] double maintenance_margin(double qty, double mark_px) const noexcept override {
        return 0.25 * std::abs(qty) * mark_px;
    }

private:
    std::string symbol_;
    const Venue& venue_;
    Currency ccy_;
};

class Future final : public Instrument {
public:
    Future(std::string symbol, const Venue& venue, Currency ccy,
           double contract_multiplier, double tick_size,
           DateTime expiry, double exchange_initial_margin) noexcept
        : symbol_(std::move(symbol)), venue_(venue), ccy_(ccy),
          multiplier_(contract_multiplier), tick_size_(tick_size),
          expiry_(expiry), exchange_im_(exchange_initial_margin) {}

    [[nodiscard]] SecurityType type() const noexcept override { return SecurityType::Future; }
    [[nodiscard]] std::string_view symbol() const noexcept override { return symbol_; }
    [[nodiscard]] const Venue& venue() const noexcept override { return venue_; }
    [[nodiscard]] Currency settlement_ccy() const noexcept override { return ccy_; }

    [[nodiscard]] double notional(double qty, double mark_px) const noexcept override {
        return qty * mark_px * multiplier_;
    }
    [[nodiscard]] double mark_to_market(double qty, double avg_px,
                                          double mark_px) const noexcept override {
        return qty * (mark_px - avg_px) * multiplier_;
    }
    [[nodiscard]] double initial_margin(double qty, double mark_px) const noexcept override {
        (void)mark_px;
        return std::abs(qty) * exchange_im_;
    }
    [[nodiscard]] double maintenance_margin(double qty, double mark_px) const noexcept override {
        (void)mark_px;
        return std::abs(qty) * exchange_im_ * 0.8;
    }

    [[nodiscard]] double multiplier() const noexcept { return multiplier_; }
    [[nodiscard]] double tick_size() const noexcept { return tick_size_; }
    [[nodiscard]] DateTime expiry() const noexcept { return expiry_; }

private:
    std::string symbol_;
    const Venue& venue_;
    Currency ccy_;
    double multiplier_;
    double tick_size_;
    DateTime expiry_;
    double exchange_im_;
};

class FxSpot final : public Instrument {
public:
    FxSpot(Currency base, Currency quote, const Venue& venue) noexcept
        : venue_(venue), base_(base), quote_(quote) {
        symbol_ = std::string(iso_code(base)) + std::string(iso_code(quote));
    }

    [[nodiscard]] SecurityType type() const noexcept override { return SecurityType::FxSpot; }
    [[nodiscard]] std::string_view symbol() const noexcept override { return symbol_; }
    [[nodiscard]] const Venue& venue() const noexcept override { return venue_; }
    [[nodiscard]] Currency settlement_ccy() const noexcept override { return quote_; }

    [[nodiscard]] double notional(double qty, double /*mark_px*/) const noexcept override {
        return qty;
    }
    [[nodiscard]] double mark_to_market(double qty, double avg_px,
                                          double mark_px) const noexcept override {
        return qty * (mark_px - avg_px);
    }
    [[nodiscard]] double initial_margin(double qty, double /*mark_px*/) const noexcept override {
        return 0.02 * std::abs(qty);
    }
    [[nodiscard]] double maintenance_margin(double qty, double /*mark_px*/) const noexcept override {
        return 0.01 * std::abs(qty);
    }

    [[nodiscard]] Currency base_ccy() const noexcept { return base_; }
    [[nodiscard]] Currency quote_ccy() const noexcept { return quote_; }

private:
    std::string symbol_;
    const Venue& venue_;
    Currency base_;
    Currency quote_;
};

class OptionCall final : public Instrument {
public:
    OptionCall(std::string symbol, const Venue& venue, Currency ccy,
               double strike, DateTime expiry, double multiplier = 100.0) noexcept
        : symbol_(std::move(symbol)), venue_(venue), ccy_(ccy),
          strike_(strike), expiry_(expiry), multiplier_(multiplier) {}

    [[nodiscard]] SecurityType type() const noexcept override { return SecurityType::OptionCall; }
    [[nodiscard]] std::string_view symbol() const noexcept override { return symbol_; }
    [[nodiscard]] const Venue& venue() const noexcept override { return venue_; }
    [[nodiscard]] Currency settlement_ccy() const noexcept override { return ccy_; }

    [[nodiscard]] double notional(double qty, double mark_px) const noexcept override {
        return qty * mark_px * multiplier_;
    }
    [[nodiscard]] double mark_to_market(double qty, double avg_px,
                                          double mark_px) const noexcept override {
        return qty * (mark_px - avg_px) * multiplier_;
    }
    [[nodiscard]] double initial_margin(double qty, double mark_px) const noexcept override {
        return std::abs(qty) * mark_px * multiplier_;
    }
    [[nodiscard]] double maintenance_margin(double qty, double mark_px) const noexcept override {
        return std::abs(qty) * mark_px * multiplier_;
    }

    [[nodiscard]] double strike() const noexcept { return strike_; }
    [[nodiscard]] DateTime expiry() const noexcept { return expiry_; }
    [[nodiscard]] double multiplier() const noexcept { return multiplier_; }

private:
    std::string symbol_;
    const Venue& venue_;
    Currency ccy_;
    double strike_;
    DateTime expiry_;
    double multiplier_;
};

class OptionPut final : public Instrument {
public:
    OptionPut(std::string symbol, const Venue& venue, Currency ccy,
              double strike, DateTime expiry, double multiplier = 100.0) noexcept
        : symbol_(std::move(symbol)), venue_(venue), ccy_(ccy),
          strike_(strike), expiry_(expiry), multiplier_(multiplier) {}

    [[nodiscard]] SecurityType type() const noexcept override { return SecurityType::OptionPut; }
    [[nodiscard]] std::string_view symbol() const noexcept override { return symbol_; }
    [[nodiscard]] const Venue& venue() const noexcept override { return venue_; }
    [[nodiscard]] Currency settlement_ccy() const noexcept override { return ccy_; }

    [[nodiscard]] double notional(double qty, double mark_px) const noexcept override {
        return qty * mark_px * multiplier_;
    }
    [[nodiscard]] double mark_to_market(double qty, double avg_px,
                                          double mark_px) const noexcept override {
        return qty * (mark_px - avg_px) * multiplier_;
    }
    [[nodiscard]] double initial_margin(double qty, double mark_px) const noexcept override {
        return std::abs(qty) * mark_px * multiplier_;
    }
    [[nodiscard]] double maintenance_margin(double qty, double mark_px) const noexcept override {
        return std::abs(qty) * mark_px * multiplier_;
    }

    [[nodiscard]] double strike() const noexcept { return strike_; }
    [[nodiscard]] DateTime expiry() const noexcept { return expiry_; }
    [[nodiscard]] double multiplier() const noexcept { return multiplier_; }

private:
    std::string symbol_;
    const Venue& venue_;
    Currency ccy_;
    double strike_;
    DateTime expiry_;
    double multiplier_;
};

class CorporateBond final : public Instrument {
public:
    CorporateBond(std::string symbol, const Venue& venue, Currency ccy,
                  double face_value, double coupon_rate) noexcept
        : symbol_(std::move(symbol)), venue_(venue), ccy_(ccy),
          face_value_(face_value), coupon_rate_(coupon_rate) {}

    [[nodiscard]] SecurityType type() const noexcept override { return SecurityType::CorporateBond; }
    [[nodiscard]] std::string_view symbol() const noexcept override { return symbol_; }
    [[nodiscard]] const Venue& venue() const noexcept override { return venue_; }
    [[nodiscard]] Currency settlement_ccy() const noexcept override { return ccy_; }

    [[nodiscard]] double notional(double qty, double mark_px) const noexcept override {
        return qty * mark_px * face_value_ / 100.0;
    }
    [[nodiscard]] double mark_to_market(double qty, double avg_px,
                                          double mark_px) const noexcept override {
        return qty * (mark_px - avg_px) * face_value_ / 100.0;
    }
    [[nodiscard]] double initial_margin(double qty, double mark_px) const noexcept override {
        return 0.1 * std::abs(qty) * mark_px * face_value_ / 100.0;
    }
    [[nodiscard]] double maintenance_margin(double qty, double mark_px) const noexcept override {
        return 0.05 * std::abs(qty) * mark_px * face_value_ / 100.0;
    }

    [[nodiscard]] double face_value() const noexcept { return face_value_; }
    [[nodiscard]] double coupon_rate() const noexcept { return coupon_rate_; }

private:
    std::string symbol_;
    const Venue& venue_;
    Currency ccy_;
    double face_value_;
    double coupon_rate_;
};

} // namespace stratforge
