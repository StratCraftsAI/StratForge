#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

namespace detail {

struct QlibKBarValues {
    double kmid;
    double klen;
    double kmid2;
    double kup;
    double kup2;
    double klow;
    double klow2;
    double ksft;
    double ksft2;
};

/// Calculate Qlib Alpha158's nine current-bar candle-shape fields once.
/// Invalid/non-finite OHLC data and a zero open produce NaN for every field.
[[nodiscard]] inline QlibKBarValues qlib_kbar_values(
    double open, double high, double low, double close) noexcept {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const QlibKBarValues invalid{nan, nan, nan, nan, nan, nan, nan, nan, nan};
    if (!std::isfinite(open) || !std::isfinite(high) ||
        !std::isfinite(low) || !std::isfinite(close) || open == 0.0 ||
        high < low || high < std::max(open, close) ||
        low > std::min(open, close)) {
        return invalid;
    }

    const double range_denominator = high - low + 1e-12;
    const double upper = high - std::max(open, close);
    const double lower = std::min(open, close) - low;
    const double shift = 2.0 * close - high - low;
    return {
        (close - open) / open,
        (high - low) / open,
        (close - open) / range_denominator,
        upper / open,
        upper / range_denominator,
        lower / open,
        lower / range_denominator,
        shift / open,
        shift / range_denominator,
    };
}

enum class QlibKBarField { kmid, klen, kmid2, kup, kup2, klow, klow2, ksft, ksft2 };

template <QlibKBarField Field>
[[nodiscard]] inline double select_qlib_kbar_value(const QlibKBarValues& values) noexcept {
    if constexpr (Field == QlibKBarField::kmid) return values.kmid;
    if constexpr (Field == QlibKBarField::klen) return values.klen;
    if constexpr (Field == QlibKBarField::kmid2) return values.kmid2;
    if constexpr (Field == QlibKBarField::kup) return values.kup;
    if constexpr (Field == QlibKBarField::kup2) return values.kup2;
    if constexpr (Field == QlibKBarField::klow) return values.klow;
    if constexpr (Field == QlibKBarField::klow2) return values.klow2;
    if constexpr (Field == QlibKBarField::ksft) return values.ksft;
    return values.ksft2;
}

} // namespace detail

template <detail::QlibKBarField Field>
class QlibKBarIndicator : public Indicator<QlibKBarIndicator<Field>> {
public:
    QlibKBarIndicator(const Line<double>& open, const Line<double>& high,
                      const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        if (this->line_.empty()) [[unlikely]] { this->reserve_output(close_.size()); }
        const auto idx = close_.index();
        const auto values = detail::qlib_kbar_values(
            open_.data()[idx], high_.data()[idx], low_.data()[idx], close_.data()[idx]);
        this->line_.forward(detail::select_qlib_kbar_value<Field>(values));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1; }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
};

using KMid = QlibKBarIndicator<detail::QlibKBarField::kmid>;
using KLen = QlibKBarIndicator<detail::QlibKBarField::klen>;
using KMid2 = QlibKBarIndicator<detail::QlibKBarField::kmid2>;
using KUp = QlibKBarIndicator<detail::QlibKBarField::kup>;
using KUp2 = QlibKBarIndicator<detail::QlibKBarField::kup2>;
using KLow = QlibKBarIndicator<detail::QlibKBarField::klow>;
using KLow2 = QlibKBarIndicator<detail::QlibKBarField::klow2>;
using KSft = QlibKBarIndicator<detail::QlibKBarField::ksft>;
using KSft2 = QlibKBarIndicator<detail::QlibKBarField::ksft2>;

using KMID = KMid;
using KLEN = KLen;
using KMID2 = KMid2;
using KUP = KUp;
using KUP2 = KUp2;
using KLOW = KLow;
using KLOW2 = KLow2;
using KSFT = KSft;
using KSFT2 = KSft2;

} // namespace stratforge
