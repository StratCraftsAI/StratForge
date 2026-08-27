#pragma once

/// @file pattern_context_common.hpp
/// @brief Generic directional-signal lifecycle primitives for TICKET_SF012.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace stratforge {

enum class PatternLifecycleState : int {
    expired = -2,
    invalidated = -1,
    idle = 0,
    pending = 1,
    confirmed = 2,
};

enum class SignalOverlapPolicy {
    ignore,
    replace,
    queue_one,
};

// Public spellings frozen by the SF012 contract. Keep the descriptive core
// names above source-compatible for code written during WP6.
using ConfirmationState = PatternLifecycleState;
using OverlapPolicy = SignalOverlapPolicy;

namespace pattern_context {

struct LifecycleSample {
    double signal{0.0};
    double price{std::numeric_limits<double>::quiet_NaN()};
    double confirmation_threshold{std::numeric_limits<double>::quiet_NaN()};
    double invalidation_threshold{std::numeric_limits<double>::quiet_NaN()};
};

struct LifecycleOutput {
    PatternLifecycleState state{PatternLifecycleState::idle};
    std::uint64_t event_id{0};
    std::size_t age{0};
    int direction{0};
};

/// Stateful core shared by context indicators that follow a directional event.
///
/// A non-zero finite signal starts an event. Threshold tests include equality,
/// invalidation wins if both thresholds are reached, and expiry is tested last.
/// Terminal states are one-sample pulses. The class owns no input lines and
/// therefore cannot advance or resample an upstream feed.
class SignalLifecycle {
public:
    explicit SignalLifecycle(
        std::size_t expiry_bars,
        SignalOverlapPolicy overlap = SignalOverlapPolicy::ignore) noexcept
        : expiry_bars_(expiry_bars), overlap_(overlap) {}

    [[nodiscard]] LifecycleOutput update(const LifecycleSample& sample) {
        begin_bar();

        const int incoming = signal_direction(sample.signal);
        if (incoming != 0) {
            if (!active_) {
                start(incoming);
            } else {
                handle_overlap(incoming);
            }
        }

        if (!active_) return {};

        LifecycleOutput output{PatternLifecycleState::pending, event_id_, age_, direction_};
        if (!required_inputs_are_finite(sample)) {
            output.state = PatternLifecycleState::invalidated;
            finish();
            return output;
        }

        const bool confirms = direction_ > 0
                                  ? sample.price >= sample.confirmation_threshold
                                  : sample.price <= sample.confirmation_threshold;
        const bool invalidates = direction_ > 0
                                     ? sample.price <= sample.invalidation_threshold
                                     : sample.price >= sample.invalidation_threshold;
        if (invalidates) {
            output.state = PatternLifecycleState::invalidated;
            finish();
        } else if (confirms) {
            output.state = PatternLifecycleState::confirmed;
            finish();
        } else if (age_ == expiry_bars_) {
            output.state = PatternLifecycleState::expired;
            finish();
        }
        return output;
    }

    [[nodiscard]] std::size_t expiry_bars() const noexcept { return expiry_bars_; }
    [[nodiscard]] SignalOverlapPolicy overlap_policy() const noexcept { return overlap_; }

private:
    static int signal_direction(double signal) noexcept {
        if (!std::isfinite(signal) || signal == 0.0) return 0;
        return signal > 0.0 ? 1 : -1;
    }

    static bool required_inputs_are_finite(const LifecycleSample& sample) noexcept {
        return std::isfinite(sample.signal) && std::isfinite(sample.price) &&
               std::isfinite(sample.confirmation_threshold) &&
               std::isfinite(sample.invalidation_threshold);
    }

    void begin_bar() {
        if (terminal_last_bar_) {
            terminal_last_bar_ = false;
            if (queued_direction_ != 0) {
                const int direction = queued_direction_;
                queued_direction_ = 0;
                start(direction);
            }
        } else if (active_) {
            ++age_;
        }
    }

    void handle_overlap(int incoming) {
        switch (overlap_) {
        case SignalOverlapPolicy::ignore:
            break;
        case SignalOverlapPolicy::replace:
            start(incoming);
            break;
        case SignalOverlapPolicy::queue_one:
            if (queued_direction_ == 0) queued_direction_ = incoming;
            break;
        }
    }

    void start(int direction) {
        if (next_event_id_ == 0) {
            throw std::overflow_error("pattern lifecycle event_id exhausted");
        }
        active_ = true;
        direction_ = direction;
        age_ = 0;
        event_id_ = next_event_id_++;
    }

    void finish() noexcept {
        active_ = false;
        terminal_last_bar_ = true;
    }

    std::size_t expiry_bars_;
    SignalOverlapPolicy overlap_;
    bool active_{false};
    bool terminal_last_bar_{false};
    int direction_{0};
    int queued_direction_{0};
    std::size_t age_{0};
    std::uint64_t event_id_{0};
    std::uint64_t next_event_id_{1};
};

} // namespace pattern_context
} // namespace stratforge
