#pragma once

#include <stratforge/bar.hpp>
#include <stratforge/broker/currency.hpp>

#include <cstdint>
#include <memory>

namespace stratforge {

enum class MIC : std::uint16_t {
    XNAS, XNYS, XCME, XCBOT, XCBO, XNYM,
    XLON, XPAR, XAMS, XETR, XSWX,
    XTKS, XHKG, XSES, XKRX, XASX,
    XFX,
};

class FeeSchedule {
public:
    virtual ~FeeSchedule() = default;
    [[nodiscard]] virtual double commission(double qty, double price,
                                             Currency ccy) const = 0;
};

class ZeroFeeSchedule final : public FeeSchedule {
public:
    [[nodiscard]] double commission(double /*qty*/, double /*price*/,
                                     Currency /*ccy*/) const noexcept override {
        return 0.0;
    }
};

class PercentageFeeSchedule final : public FeeSchedule {
public:
    explicit PercentageFeeSchedule(double rate) noexcept : rate_(rate) {}
    [[nodiscard]] double commission(double qty, double price,
                                     Currency /*ccy*/) const noexcept override {
        return std::abs(qty) * price * rate_;
    }
private:
    double rate_ = 0.0;
};

class Calendar {
public:
    virtual ~Calendar() = default;
    [[nodiscard]] virtual bool is_open(DateTime at) const noexcept = 0;
    [[nodiscard]] virtual DateTime next_open(DateTime after) const noexcept = 0;
    [[nodiscard]] virtual DateTime next_close(DateTime after) const noexcept = 0;
};

class AlwaysOpenCalendar final : public Calendar {
public:
    [[nodiscard]] bool is_open(DateTime /*at*/) const noexcept override { return true; }
    [[nodiscard]] DateTime next_open(DateTime after) const noexcept override { return after; }
    [[nodiscard]] DateTime next_close(DateTime /*after*/) const noexcept override {
        return DateTime::max();
    }
};

class Venue {
public:
    Venue(MIC mic, Currency base_ccy,
          std::unique_ptr<FeeSchedule> fees,
          std::shared_ptr<const Calendar> cal) noexcept
        : mic_(mic), base_ccy_(base_ccy),
          fees_(std::move(fees)), cal_(std::move(cal)) {}

    Venue(const Venue&) = delete;
    Venue& operator=(const Venue&) = delete;
    Venue(Venue&&) = delete;
    Venue& operator=(Venue&&) = delete;

    [[nodiscard]] MIC mic() const noexcept { return mic_; }
    [[nodiscard]] Currency base_ccy() const noexcept { return base_ccy_; }
    [[nodiscard]] const FeeSchedule& fees() const noexcept { return *fees_; }
    [[nodiscard]] bool is_open(DateTime at) const noexcept {
        return cal_ ? cal_->is_open(at) : true;
    }
    [[nodiscard]] DateTime next_open(DateTime after) const noexcept {
        return cal_ ? cal_->next_open(after) : after;
    }
    [[nodiscard]] DateTime next_close(DateTime after) const noexcept {
        return cal_ ? cal_->next_close(after) : DateTime::max();
    }
    [[nodiscard]] std::uint8_t settlement_days() const noexcept { return settlement_days_; }

    void set_settlement_days(std::uint8_t days) noexcept { settlement_days_ = days; }

private:
    MIC mic_;
    Currency base_ccy_;
    std::unique_ptr<FeeSchedule> fees_;
    std::shared_ptr<const Calendar> cal_;
    std::uint8_t settlement_days_ = 2;
};

} // namespace stratforge
