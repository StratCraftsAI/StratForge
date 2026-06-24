#pragma once

#include <stratforge/bar.hpp>

#include <cstdint>
#include <string_view>

namespace stratforge {

enum class Currency : std::uint16_t {
    USD = 840, EUR = 978, JPY = 392, GBP = 826,
    HKD = 344, CHF = 756, CAD = 124, AUD = 36,
    CNY = 156, KRW = 410, SGD = 702, NZD = 554,
};

[[nodiscard]] constexpr std::string_view iso_code(Currency c) noexcept {
    switch (c) {
        case Currency::USD: return "USD";
        case Currency::EUR: return "EUR";
        case Currency::JPY: return "JPY";
        case Currency::GBP: return "GBP";
        case Currency::HKD: return "HKD";
        case Currency::CHF: return "CHF";
        case Currency::CAD: return "CAD";
        case Currency::AUD: return "AUD";
        case Currency::CNY: return "CNY";
        case Currency::KRW: return "KRW";
        case Currency::SGD: return "SGD";
        case Currency::NZD: return "NZD";
    }
    return "???";
}

class FxRateProvider {
public:
    virtual ~FxRateProvider() = default;
    [[nodiscard]] virtual double rate(Currency from, Currency to,
                                       DateTime at) const = 0;
};

class ConstantFxProvider final : public FxRateProvider {
public:
    explicit ConstantFxProvider(double rate = 1.0) noexcept : rate_(rate) {}
    [[nodiscard]] double rate(Currency from, Currency to,
                               DateTime /*at*/) const noexcept override {
        if (from == to) return 1.0;
        return rate_;
    }
private:
    double rate_ = 1.0;
};

} // namespace stratforge

template<>
struct std::hash<stratforge::Currency> {
    std::size_t operator()(stratforge::Currency c) const noexcept {
        return std::hash<std::uint16_t>{}(static_cast<std::uint16_t>(c));
    }
};
