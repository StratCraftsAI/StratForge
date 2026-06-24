#pragma once

#include <stratforge/broker/instrument.hpp>

#include <cmath>
#include <cstddef>

namespace stratforge {

struct Position {
    const Instrument* instrument = nullptr;
    std::size_t data_index = 0;
    double size = 0.0;
    double avg_price = 0.0;
    double price = 0.0;
    double price_orig = 0.0;
    double upopened = 0.0;
    double upclosed = 0.0;

    void update(double fill_size, double fill_price) noexcept {
        constexpr double eps = 1e-10;

        price_orig = price;
        const double old_size = size;
        const double new_size = old_size + fill_size;

        if (std::abs(old_size) < eps) {
            upopened = fill_size;
            upclosed = 0.0;
        } else if (old_size > 0.0) {
            if (fill_size > 0.0) {
                upopened = fill_size;
                upclosed = 0.0;
            } else if (new_size >= 0.0) {
                upopened = 0.0;
                upclosed = fill_size;
            } else {
                upopened = new_size;
                upclosed = -old_size;
            }
        } else {
            if (fill_size < 0.0) {
                upopened = fill_size;
                upclosed = 0.0;
            } else if (new_size <= 0.0) {
                upopened = 0.0;
                upclosed = fill_size;
            } else {
                upopened = new_size;
                upclosed = -old_size;
            }
        }

        size = new_size;

        if (std::abs(size) < eps) {
            size = 0.0;
            avg_price = 0.0;
            price = 0.0;
            return;
        }

        if (std::abs(old_size) < eps) {
            avg_price = fill_price;
            price = fill_price;
        } else if ((old_size > 0.0 && fill_size > 0.0) || (old_size < 0.0 && fill_size < 0.0)) {
            avg_price = ((price * old_size) + (fill_size * fill_price)) / size;
            price = avg_price;
        } else if ((old_size > 0.0 && size < 0.0) || (old_size < 0.0 && size > 0.0)) {
            avg_price = fill_price;
            price = fill_price;
        }
    }

    [[nodiscard]] double unrealized_pnl(double mark_px) const noexcept {
        if (instrument) {
            return instrument->mark_to_market(size, avg_price, mark_px);
        }
        return size * (mark_px - avg_price);
    }

    [[nodiscard]] bool is_flat() const noexcept {
        return std::abs(size) < 1e-10;
    }

    [[nodiscard]] bool is_long() const noexcept {
        return size > 1e-10;
    }

    [[nodiscard]] bool is_short() const noexcept {
        return size < -1e-10;
    }
};

} // namespace stratforge
