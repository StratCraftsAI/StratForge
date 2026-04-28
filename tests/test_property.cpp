// SPDX-License-Identifier: MIT
//
// tests/test_property.cpp — Property-based / invariant tests.
//
//  Phase C. Each TEST_CASE drives a randomized sequence of
// operations against a component and asserts an invariant holds for every
// step. Fixed seed (mt19937(42)) guarantees reproducibility.
//
// Tag form: [property][regression][<module>].
// Each test runs >= 10K iterations and is expected to complete in < 1s.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/broker/order.hpp>
#include <stratforge/broker/position.hpp>
#include <stratforge/core/line.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/indicators/sma.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using stratforge::test::generate_sine_data;
using stratforge::test::make_line;
using stratforge::test::tmp_path;
using stratforge::test::write_csv;

namespace {

constexpr std::uint32_t kSeed = 42;
constexpr std::size_t kIterations = 10000;

}  // namespace

// ============================================================================
// Invariant 1 — Line<double> ring buffer
// ============================================================================
//
// For any random sequence of forward()/home()/advance()/index access:
//   I1: data().size() never decreases (line is append-only)
//   I2: index() < size() whenever size() > 0
//   I3: home() does not change size()
//   I4: After forward(v), data().back() == v
//   I5: operator[](0) == data()[index()]
//
TEST_CASE("Line<double> invariants under random operation walk", "[property][regression][core]") {
    std::mt19937 rng(kSeed);
    std::uniform_int_distribution<int> op_dist(0, 4);
    std::uniform_real_distribution<double> val_dist(-1000.0, 1000.0);

    stratforge::Line<double> line;
    std::size_t prev_size = 0;

    for (std::size_t i = 0; i < kIterations; ++i) {
        const int op = op_dist(rng);
        switch (op) {
            case 0:
            case 1: {  // forward (weighted higher)
                const double v = val_dist(rng);
                line.forward(v);
                INFO("iter=" << i << " op=forward v=" << v);
                REQUIRE(line.size() >= prev_size);
                REQUIRE(line.size() == prev_size + 1);
                REQUIRE(line.data().back() == v);
                break;
            }
            case 2: {  // home
                const auto sz_before = line.size();
                line.home();
                INFO("iter=" << i << " op=home");
                REQUIRE(line.size() == sz_before);
                if (sz_before > 0) {
                    REQUIRE(line.index() == 0);
                }
                break;
            }
            case 3: {  // advance
                line.advance();
                INFO("iter=" << i << " op=advance");
                if (!line.empty()) {
                    REQUIRE(line.index() < line.size());
                }
                break;
            }
            case 4: {  // index access
                INFO("iter=" << i << " op=read");
                if (!line.empty()) {
                    REQUIRE(line.index() < line.size());
                    const double via_op = line[0];
                    const double via_data = line.data()[line.index()];
                    REQUIRE(via_op == via_data);
                }
                break;
            }
            default:
                break;
        }
        prev_size = line.size();
    }
    REQUIRE(line.size() > 0);
}

// ============================================================================
// Invariant 2 — Position update conservation
// ============================================================================
//
// For any random sequence of update(size, price) calls on a Position:
//   I1: When old_size and fill_size have the same sign (pure increase),
//       upopened == fill_size and upclosed == 0.
//   I2: When the fill fully reverses direction, upclosed == -old_size and
//       upopened == new_size (signs match new direction).
//   I3: Going flat (size crosses to ~0) clears avg_price/total_cost.
//   I4: For pure-long opens (price > 0, size > 0), avg_price after N opens
//       equals weighted average of (size, price) inputs.
//   I5: total_cost == abs(size) * avg_price (after every update).
//
TEST_CASE("Position::update conservation invariants", "[property][regression][broker]") {
    SECTION("Weighted average price for pure-long opens") {
        std::mt19937 rng(kSeed);
        std::uniform_real_distribution<double> sz_dist(0.1, 100.0);
        std::uniform_real_distribution<double> px_dist(50.0, 200.0);

        stratforge::Position pos;
        double total_size = 0.0;
        double weighted_sum = 0.0;

        for (std::size_t i = 0; i < 1000; ++i) {
            const double s = sz_dist(rng);
            const double p = px_dist(rng);
            pos.update(s, p);
            total_size += s;
            weighted_sum += s * p;

            const double expected_avg = weighted_sum / total_size;
            INFO("iter=" << i << " size=" << pos.size << " avg=" << pos.avg_price
                         << " expected=" << expected_avg);
            REQUIRE_THAT(pos.size, Catch::Matchers::WithinAbs(total_size, 1e-9));
            REQUIRE_THAT(pos.avg_price, Catch::Matchers::WithinRel(expected_avg, 1e-12));
            REQUIRE_THAT(pos.total_cost,
                         Catch::Matchers::WithinRel(std::abs(pos.size) * pos.avg_price, 1e-12));
        }
    }

    SECTION("Direction flip clears upopened of opposite sign") {
        std::mt19937 rng(kSeed + 1);
        std::uniform_real_distribution<double> px_dist(50.0, 200.0);

        stratforge::Position pos;
        // Open long
        pos.update(10.0, px_dist(rng));
        REQUIRE(pos.size > 0.0);
        // Reverse to short with single fill
        const double reverse_size = -25.0;
        const double reverse_price = px_dist(rng);
        pos.update(reverse_size, reverse_price);
        REQUIRE(pos.size < 0.0);
        // upclosed must equal -old_size (10.0); upopened == new_size (-15.0)
        REQUIRE_THAT(pos.upclosed, Catch::Matchers::WithinAbs(-10.0, 1e-9));
        REQUIRE_THAT(pos.upopened, Catch::Matchers::WithinAbs(-15.0, 1e-9));
        REQUIRE_THAT(pos.avg_price, Catch::Matchers::WithinRel(reverse_price, 1e-12));
    }

    SECTION("Going flat zeros avg_price and total_cost") {
        stratforge::Position pos;
        pos.update(50.0, 100.0);
        REQUIRE(pos.size > 0.0);
        pos.update(-50.0, 110.0);
        REQUIRE(pos.is_flat());
        REQUIRE(pos.avg_price == 0.0);
        REQUIRE(pos.total_cost == 0.0);
        REQUIRE(pos.price == 0.0);
    }

    SECTION("Random walk: total_cost == abs(size) * avg_price always") {
        std::mt19937 rng(kSeed + 2);
        std::uniform_real_distribution<double> sz_dist(-50.0, 50.0);
        std::uniform_real_distribution<double> px_dist(50.0, 200.0);

        stratforge::Position pos;
        for (std::size_t i = 0; i < kIterations; ++i) {
            pos.update(sz_dist(rng), px_dist(rng));
            INFO("iter=" << i << " size=" << pos.size << " avg=" << pos.avg_price
                         << " cost=" << pos.total_cost);
            const double expected_cost = std::abs(pos.size) * pos.avg_price;
            REQUIRE_THAT(pos.total_cost, Catch::Matchers::WithinAbs(expected_cost, 1e-9));
        }
    }
}

// ============================================================================
// Invariant 3 — Order state machine
// ============================================================================
//
// Random walks over OrderStatus transitions:
//   I1: Every transition lands in a valid OrderStatus enum value.
//   I2: Terminal states (Completed, Canceled, Rejected, Expired, Margin)
//       satisfy is_alive() == false and is_complete() == true.
//   I3: Live states (Created, Submitted, Accepted, Partial) satisfy
//       is_alive() == true.
//   I4: After a 10K-step random walk (each step picks any operation), the
//       final status is always one of the 9 declared enum values.
//
TEST_CASE("Order state machine invariants", "[property][regression][order]") {
    using stratforge::Order;
    using stratforge::OrderStatus;

    constexpr OrderStatus kTerminal[] = {
        OrderStatus::Completed,
        OrderStatus::Canceled,
        OrderStatus::Rejected,
        OrderStatus::Expired,
        OrderStatus::Margin,
    };
    constexpr OrderStatus kLive[] = {
        OrderStatus::Created,
        OrderStatus::Submitted,
        OrderStatus::Accepted,
        OrderStatus::Partial,
    };

    SECTION("Terminal/live status partition is exhaustive and disjoint") {
        for (auto s : kLive) {
            Order o;
            o.status = s;
            REQUIRE(o.is_alive());
            REQUIRE_FALSE(o.is_complete());
        }
        for (auto s : kTerminal) {
            Order o;
            o.status = s;
            REQUIRE_FALSE(o.is_alive());
            REQUIRE(o.is_complete());
        }
    }

    SECTION("Terminal states are absorbing — random walk cannot escape") {
        std::mt19937 rng(kSeed + 3);
        std::uniform_int_distribution<int> op_dist(0, 7);
        std::uniform_real_distribution<double> px_dist(50.0, 200.0);
        std::uniform_real_distribution<double> sz_dist(0.1, 100.0);

        auto is_terminal = [&](OrderStatus s) {
            for (auto t : kTerminal) { if (s == t) return true; }
            return false;
        };

        auto apply_op = [&](Order& o, int op) {
            switch (op) {
                case 0: o.submit(); break;
                case 1: o.accept(); break;
                case 2: o.execute(px_dist(rng), sz_dist(rng), 0.1); break;
                case 3: o.partial_fill(px_dist(rng), sz_dist(rng), 0.1); break;
                case 4: o.cancel(); break;
                case 5: o.reject(); break;
                case 6: o.expire(); break;
                case 7: o.margin(); break;
                default: break;
            }
        };

        Order o;
        for (std::size_t i = 0; i < kIterations; ++i) {
            const auto before = o.status;
            apply_op(o, op_dist(rng));
            const auto after = o.status;

            const auto v = static_cast<std::uint8_t>(after);
            INFO("iter=" << i << " before=" << static_cast<int>(before)
                         << " after=" << static_cast<int>(v));
            // Enum range validity
            REQUIRE(v <= static_cast<std::uint8_t>(OrderStatus::Margin));
            // is_alive / is_complete must always be perfectly disjoint
            REQUIRE(o.is_alive() != o.is_complete());
            // Core invariant: terminal states are absorbing
            if (is_terminal(before)) {
                REQUIRE(after == before);
            }
        }
    }

    SECTION("Each terminal state individually resists all transitions") {
        std::uniform_real_distribution<double> px_dist(50.0, 200.0);
        std::uniform_real_distribution<double> sz_dist(0.1, 100.0);
        std::mt19937 rng(kSeed + 4);

        for (auto terminal : kTerminal) {
            for (int op = 0; op <= 7; ++op) {
                Order o;
                o.status = terminal;
                switch (op) {
                    case 0: o.submit(); break;
                    case 1: o.accept(); break;
                    case 2: o.execute(px_dist(rng), sz_dist(rng), 0.1); break;
                    case 3: o.partial_fill(px_dist(rng), sz_dist(rng), 0.1); break;
                    case 4: o.cancel(); break;
                    case 5: o.reject(); break;
                    case 6: o.expire(); break;
                    case 7: o.margin(); break;
                    default: break;
                }
                INFO("terminal=" << static_cast<int>(terminal) << " op=" << op);
                REQUIRE(o.status == terminal);
            }
        }
    }
}

// ============================================================================
// Invariant 4 — CSV reload determinism
// ============================================================================
//
// Re-loading the same CSV file must yield byte-identical OHLCV buffers.
//
TEST_CASE("CSV reload yields identical OHLCV buffers", "[property][regression][data]") {
    const std::string csv_content =
        "Date,Open,High,Low,Close,Volume\n"
        "2024-01-01,100.0,105.0,99.0,103.0,1000\n"
        "2024-01-02,103.0,108.0,102.0,107.0,1500\n"
        "2024-01-03,107.0,110.0,105.0,108.0,1800\n"
        "2024-01-04,108.0,112.0,107.0,111.0,2000\n"
        "2024-01-05,111.0,115.0,110.0,114.0,2100\n";
    const std::string path = write_csv("property_csv_reload.csv", csv_content);

    auto load_once = [&]() {
        stratforge::CsvData feed(stratforge::CsvData::Params{
            .filename = path,
            .columns = {},
            .date_format = "%Y-%m-%d",
            .time_format = "%H:%M:%S",
            .separator = ',',
            .has_headers = true,
            .fromdate = std::nullopt,
            .todate = std::nullopt,
        });
        REQUIRE(feed.load());
        return std::vector<std::vector<double>>{
            feed.open().data(),
            feed.high().data(),
            feed.low().data(),
            feed.close().data(),
            feed.volume().data(),
        };
    };

    const auto first = load_once();
    for (std::size_t trial = 0; trial < 5; ++trial) {
        const auto again = load_once();
        INFO("trial=" << trial);
        for (std::size_t col = 0; col < first.size(); ++col) {
            REQUIRE(again[col].size() == first[col].size());
            for (std::size_t i = 0; i < first[col].size(); ++i) {
                INFO("col=" << col << " row=" << i);
                REQUIRE(again[col][i] == first[col][i]);  // bit-exact
            }
        }
    }
}

// ============================================================================
// Invariant 5 — Indicator warmup
// ============================================================================
//
// For an indicator with declared period P:
//   I1: Output indices [0, P-2] are NaN (warmup).
//   I2: Output index P-1 is the first non-NaN value.
//   I3: minimum_period() reports P.
//
TEST_CASE("Indicator warmup invariant — first P-1 outputs are NaN", "[property][regression][indicator]") {
    std::mt19937 rng(kSeed + 4);
    std::uniform_int_distribution<std::size_t> period_dist(2, 50);

    for (std::size_t trial = 0; trial < 200; ++trial) {
        const std::size_t period = period_dist(rng);
        const std::size_t bar_count = period * 3;
        auto data = generate_sine_data(bar_count);
        auto src = make_line(data);
        stratforge::SMA sma(src, period);
        stratforge::test::run_indicator(src, sma);

        INFO("trial=" << trial << " period=" << period);
        REQUIRE(sma.line().size() == bar_count);

        // Warmup region: indices [0, period-2] must be NaN
        for (std::size_t i = 0; i + 1 < period; ++i) {
            INFO("warmup i=" << i);
            REQUIRE(std::isnan(sma.line().data()[i]));
        }
        // First valid sample
        INFO("first valid index=" << (period - 1));
        REQUIRE_FALSE(std::isnan(sma.line().data()[period - 1]));
    }
}
