#pragma once

#include <stratforge/indicators/macd.hpp>

namespace stratforge {

/// MACD with fixed periods (12, 26, 9) — convenience alias.
class MACDFix : public MACD {
public:
    explicit MACDFix(const Line<double>& source, std::size_t signal_period = 9)
        : MACD(source, 12, 26, signal_period) {}
};

using MACDFIX = MACDFix;

} // namespace stratforge
