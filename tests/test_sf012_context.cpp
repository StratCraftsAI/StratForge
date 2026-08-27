#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/indicators/pattern_context_common.hpp>
#include <stratforge/indicators/pattern_context.hpp>

#include <cmath>
#include <limits>

namespace {

using stratforge::PatternLifecycleState;
using stratforge::SignalOverlapPolicy;
using stratforge::pattern_context::LifecycleSample;
using stratforge::pattern_context::SignalLifecycle;

LifecycleSample bar(double signal, double price, double confirmation,
                    double invalidation) {
    return {signal, price, confirmation, invalidation};
}

} // namespace

TEST_CASE("SignalLifecycle freezes state codes and bullish transition ordering",
          "[sf012][context][lifecycle]") {
    CHECK(static_cast<int>(PatternLifecycleState::idle) == 0);
    CHECK(static_cast<int>(PatternLifecycleState::pending) == 1);
    CHECK(static_cast<int>(PatternLifecycleState::confirmed) == 2);
    CHECK(static_cast<int>(PatternLifecycleState::invalidated) == -1);
    CHECK(static_cast<int>(PatternLifecycleState::expired) == -2);

    SignalLifecycle lifecycle(3uz);
    auto output = lifecycle.update(bar(7.0, 100.0, 110.0, 90.0));
    CHECK(output.state == PatternLifecycleState::pending);
    CHECK(output.event_id == 1);
    CHECK(output.age == 0uz);
    CHECK(output.direction == 1);

    output = lifecycle.update(bar(0.0, 110.0, 110.0, 110.0));
    CHECK(output.state == PatternLifecycleState::invalidated); // both true
    CHECK(output.age == 1uz);

    output = lifecycle.update(bar(0.0, 100.0, 110.0, 90.0));
    CHECK(output.state == PatternLifecycleState::idle);
    CHECK(output.event_id == 0);
    CHECK(output.direction == 0);
}

TEST_CASE("SignalLifecycle supports bearish equality and checks price before expiry",
          "[sf012][context][lifecycle]") {
    SignalLifecycle confirmed(0uz);
    const auto confirmation = confirmed.update(bar(-1.0, 90.0, 90.0, 110.0));
    CHECK(confirmation.state == PatternLifecycleState::confirmed);
    CHECK(confirmation.direction == -1);
    CHECK(confirmation.age == 0uz);

    SignalLifecycle invalidated(0uz);
    const auto invalidation = invalidated.update(bar(-1.0, 110.0, 90.0, 110.0));
    CHECK(invalidation.state == PatternLifecycleState::invalidated);

    SignalLifecycle expired(2uz);
    CHECK(expired.update(bar(1.0, 100.0, 110.0, 90.0)).state ==
          PatternLifecycleState::pending);
    CHECK(expired.update(bar(0.0, 100.0, 110.0, 90.0)).age == 1uz);
    const auto expiry = expired.update(bar(0.0, 100.0, 110.0, 90.0));
    CHECK(expiry.state == PatternLifecycleState::expired);
    CHECK(expiry.age == 2uz);
}

TEST_CASE("SignalLifecycle invalidates pending events on every non-finite required input",
          "[sf012][context][lifecycle][invalid]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (int field = 0; field < 4; ++field) {
        SignalLifecycle lifecycle(4uz);
        REQUIRE(lifecycle.update(bar(1.0, 100.0, 110.0, 90.0)).state ==
                PatternLifecycleState::pending);
        auto sample = bar(0.0, 100.0, 110.0, 90.0);
        if (field == 0) sample.signal = nan;
        if (field == 1) sample.price = nan;
        if (field == 2) sample.confirmation_threshold = nan;
        if (field == 3) sample.invalidation_threshold = nan;
        CHECK(lifecycle.update(sample).state == PatternLifecycleState::invalidated);
    }

    SignalLifecycle idle(1uz);
    CHECK(idle.update(bar(nan, 100.0, 110.0, 90.0)).state ==
          PatternLifecycleState::idle);
}

TEST_CASE("SignalLifecycle ignore retains the active event",
          "[sf012][context][overlap]") {
    SignalLifecycle lifecycle(3uz, SignalOverlapPolicy::ignore);
    CHECK(lifecycle.overlap_policy() == SignalOverlapPolicy::ignore);
    REQUIRE(lifecycle.update(bar(1.0, 100.0, 110.0, 90.0)).state ==
            PatternLifecycleState::pending);
    const auto output = lifecycle.update(bar(-1.0, 110.0, 110.0, 90.0));
    CHECK(output.state == PatternLifecycleState::confirmed);
    CHECK(output.event_id == 1);
    CHECK(output.direction == 1);
    CHECK(output.age == 1uz);
}

TEST_CASE("SignalLifecycle replace starts and evaluates a new event on the same bar",
          "[sf012][context][overlap]") {
    SignalLifecycle lifecycle(3uz, SignalOverlapPolicy::replace);
    REQUIRE(lifecycle.update(bar(1.0, 100.0, 110.0, 90.0)).state ==
            PatternLifecycleState::pending);
    const auto output = lifecycle.update(bar(-4.0, 95.0, 95.0, 120.0));
    CHECK(output.state == PatternLifecycleState::confirmed);
    CHECK(output.event_id == 2);
    CHECK(output.direction == -1);
    CHECK(output.age == 0uz);
}

TEST_CASE("SignalLifecycle queue_one stores only the first signal and starts it next bar",
          "[sf012][context][overlap]") {
    SignalLifecycle lifecycle(4uz, SignalOverlapPolicy::queue_one);
    REQUIRE(lifecycle.update(bar(1.0, 100.0, 110.0, 90.0)).state ==
            PatternLifecycleState::pending);
    auto output = lifecycle.update(bar(-1.0, 100.0, 110.0, 90.0));
    CHECK(output.state == PatternLifecycleState::pending);
    CHECK(output.event_id == 1);
    output = lifecycle.update(bar(1.0, 110.0, 110.0, 90.0));
    CHECK(output.state == PatternLifecycleState::confirmed);
    CHECK(output.event_id == 1);

    // The queued bearish event uses this later bar's thresholds. The additional
    // bullish signal is ignored because the queue was already occupied.
    output = lifecycle.update(bar(0.0, 95.0, 95.0, 120.0));
    CHECK(output.state == PatternLifecycleState::confirmed);
    CHECK(output.event_id == 2);
    CHECK(output.direction == -1);
    CHECK(output.age == 0uz);
}

TEST_CASE("PatternConfirmation wraps arbitrary lines and exposes lifecycle components",
          "[sf012][context][wp7]") {
    stratforge::Line<double> signal, price, confirmation, invalidation;
    stratforge::PatternConfirmation context(signal, price, confirmation, invalidation, 2uz);
    CHECK(context.overlap_policy() == SignalOverlapPolicy::replace);

    const auto push = [&](double s, double p, double c, double i) {
        signal.forward(s); price.forward(p); confirmation.forward(c); invalidation.forward(i);
        context.next();
    };
    push(1.0, 100.0, 110.0, 90.0);
    CHECK(context.state()[0] == 1.0);
    CHECK(context.event_id()[0] == 1.0);
    CHECK(context.age()[0] == 0.0);
    CHECK(context.direction()[0] == 1.0);
    push(-1.0, 95.0, 90.0, 120.0);
    CHECK(context.state()[0] == 1.0);
    CHECK(context.event_id()[0] == 2.0);
    CHECK(context.direction()[0] == -1.0);
    push(0.0, 90.0, 90.0, 120.0);
    CHECK(context.state()[0] == 2.0);
    CHECK(context.age()[0] == 1.0);
}

TEST_CASE("TrendContext classifies only finite directional signals",
          "[sf012][context][wp7]") {
    stratforge::Line<double> signal, trend;
    stratforge::TrendContext context(signal, trend);
    const auto push = [&](double s, double t) {
        signal.forward(s); trend.forward(t); context.next();
    };
    push(2.0, 3.0);
    CHECK(context.alignment()[0] == 1.0);
    CHECK(context.direction()[0] == 1.0);
    push(-2.0, 3.0);
    CHECK(context.alignment()[0] == -1.0);
    CHECK(context.direction()[0] == -1.0);
    push(0.0, -3.0);
    CHECK(context.alignment()[0] == 0.0);
    CHECK(context.direction()[0] == 0.0);
    push(1.0, std::numeric_limits<double>::quiet_NaN());
    CHECK(context.alignment()[0] == 0.0);
    CHECK(context.direction()[0] == 0.0);
}

TEST_CASE("VolumeConfirmation enforces signal volume and baseline contracts",
          "[sf012][context][wp7]") {
    stratforge::Line<double> signal, volume, baseline;
    stratforge::VolumeConfirmation context(signal, volume, baseline, 1.5);
    const auto push = [&](double s, double v, double b) {
        signal.forward(s); volume.forward(v); baseline.forward(b); context.next();
    };
    push(1.0, 150.0, 100.0);
    CHECK(context.ratio()[0] == 1.5);
    CHECK(context.confirmed()[0] == 1.0);
    push(-1.0, 149.0, 100.0);
    CHECK(context.confirmed()[0] == 0.0);
    push(0.0, 200.0, 100.0);
    CHECK(std::isnan(context.ratio()[0]));
    push(1.0, 100.0, 0.0);
    CHECK(std::isnan(context.ratio()[0]));
    CHECK(context.confirmed()[0] == 0.0);
    CHECK_THROWS_AS(stratforge::VolumeConfirmation(signal, volume, baseline, -1.0),
                    std::invalid_argument);
}

TEST_CASE("SupportResistanceContext uses supplied lines without hidden pivots",
          "[sf012][context][wp7]") {
    stratforge::Line<double> signal, price, support, resistance;
    stratforge::SupportResistanceContext context(signal, price, support, resistance);
    signal.forward(-1.0); price.forward(100.0); support.forward(95.0); resistance.forward(108.0);
    context.next();
    CHECK(context.support_distance()[0] == 5.0);
    CHECK(context.resistance_distance()[0] == -8.0);
    signal.forward(0.0); price.forward(101.0); support.forward(95.0); resistance.forward(108.0);
    context.next();
    CHECK(std::isnan(context.support_distance()[0]));
    CHECK(std::isnan(context.resistance_distance()[0]));
}

TEST_CASE("CandleQualityScore clamps transparent components and validates weights",
          "[sf012][context][wp7]") {
    stratforge::Line<double> geometry, trend, confirmation, volume;
    const stratforge::CandleQualityWeights weights{1.0, 2.0, 3.0, 4.0};
    stratforge::CandleQualityScore score(geometry, trend, confirmation, volume, weights);
    geometry.forward(-0.2); trend.forward(0.5); confirmation.forward(1.2); volume.forward(0.25);
    score.next();
    CHECK(score.geometry()[0] == 0.0);
    CHECK(score.trend()[0] == 0.5);
    CHECK(score.confirmation()[0] == 1.0);
    CHECK(score.volume()[0] == 0.25);
    CHECK(score.score()[0] == Catch::Approx(0.5));

    geometry.forward(0.5); trend.forward(0.5);
    confirmation.forward(std::numeric_limits<double>::quiet_NaN()); volume.forward(0.5);
    score.next();
    CHECK(std::isnan(score.confirmation()[0]));
    CHECK(std::isnan(score.score()[0]));

    const stratforge::CandleQualityWeights zero{0.0, 0.0, 0.0, 0.0};
    stratforge::CandleQualityScore zero_score(geometry, trend, confirmation, volume, zero);
    zero_score.next();
    CHECK(std::isnan(zero_score.score()[0]));

    const stratforge::CandleQualityWeights bad{-1.0, 1.0, 1.0, 1.0};
    CHECK_THROWS_AS(stratforge::CandleQualityScore(geometry, trend, confirmation, volume, bad),
                    std::invalid_argument);
}
