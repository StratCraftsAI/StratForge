// SPDX-License-Identifier: MIT
//
//  P4: C++ calibration -- parity with calibration.ts.
// Supports isotonic (breakpoint interpolation) and Platt sigmoid.
// Loaded from artifact manifest at preflight time.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace stratforge::corrective {

struct CalibrationBreakpoint {
    float x = 0.0f;
    float y = 0.0f;
};

enum class CalibrationMethod : std::uint8_t {
    kIsotonic = 0,
    kPlatt    = 1,
};

class PopCalibrator {
public:
    // Isotonic calibration from breakpoints
    static PopCalibrator from_isotonic(
        std::vector<CalibrationBreakpoint> breakpoints) noexcept
    {
        PopCalibrator cal;
        cal.method_ = CalibrationMethod::kIsotonic;
        cal.breakpoints_ = std::move(breakpoints);
        return cal;
    }

    // Platt sigmoid calibration: p = sigma(a*x + b)
    static PopCalibrator from_platt(float a, float b) noexcept {
        PopCalibrator cal;
        cal.method_ = CalibrationMethod::kPlatt;
        cal.platt_a_ = a;
        cal.platt_b_ = b;
        return cal;
    }

    [[nodiscard]] float calibrate(float raw_prediction) const noexcept {
        switch (method_) {
            case CalibrationMethod::kIsotonic:
                return apply_isotonic_(raw_prediction);
            case CalibrationMethod::kPlatt:
                return apply_platt_(raw_prediction);
        }
        return raw_prediction;
    }

    [[nodiscard]] CalibrationMethod method() const noexcept { return method_; }
    [[nodiscard]] const std::vector<CalibrationBreakpoint>& breakpoints() const noexcept {
        return breakpoints_;
    }
    [[nodiscard]] float platt_a() const noexcept { return platt_a_; }
    [[nodiscard]] float platt_b() const noexcept { return platt_b_; }

private:
    // Parity with TS applyIsotonicCalibration: binary-search + linear interp
    [[nodiscard]] float apply_isotonic_(float raw) const noexcept {
        if (breakpoints_.empty()) return raw;
        if (breakpoints_.size() == 1) return breakpoints_[0].y;

        if (raw <= breakpoints_.front().x) return breakpoints_.front().y;
        if (raw >= breakpoints_.back().x) return breakpoints_.back().y;

        // Binary search for the interval [lo, hi] containing raw
        std::size_t lo = 0;
        std::size_t hi = breakpoints_.size() - 1;
        while (lo < hi - 1) {
            const std::size_t mid = (lo + hi) >> 1;
            if (breakpoints_[mid].x <= raw) {
                lo = mid;
            } else {
                hi = mid;
            }
        }

        const float x0 = breakpoints_[lo].x;
        const float y0 = breakpoints_[lo].y;
        const float x1 = breakpoints_[hi].x;
        const float y1 = breakpoints_[hi].y;

        if (x1 == x0) return y0;
        const float t = (raw - x0) / (x1 - x0);
        return y0 + t * (y1 - y0);
    }

    // Parity with TS applyPlattCalibration: numerically stable sigmoid
    [[nodiscard]] float apply_platt_(float raw) const noexcept {
        const float fApB = raw * platt_a_ + platt_b_;
        if (fApB >= 0.0f) {
            const float e = std::exp(-fApB);
            return e / (1.0f + e);
        }
        return 1.0f / (1.0f + std::exp(fApB));
    }

    CalibrationMethod method_ = CalibrationMethod::kIsotonic;
    std::vector<CalibrationBreakpoint> breakpoints_;
    float platt_a_ = 0.0f;
    float platt_b_ = 0.0f;
};

} // namespace stratforge::corrective
