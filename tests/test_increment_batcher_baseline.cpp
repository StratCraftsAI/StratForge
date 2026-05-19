// SPDX-License-Identifier: MIT
//
// tests/test_increment_batcher_baseline.cpp
//
// -C — Test #8 from §6: frozen-baseline CSV parity.
//
// Locks the IncrementBatcher wire shape against a self-generated baseline
// for a deterministic SMA-crossover strategy on a deterministic 1000-bar
// feed. Tagged [regression], NOT [golden] — Python backtrader has no
// IncrementBatcher equivalent, so this is not a backtrader parity test
// (see  §6 #8 note).
//
// Baseline schema (one row per flush, comma-separated, no header):
//   seq,processed_bars,is_final,new_bars_count,new_trades_count,realized_pnl,current_dd_pct
//
// Regeneration: rebuild with the `NBT_REGEN_BASELINE` env var set; the
// test rewrites tests/golden/increment_batcher_sma_cross_1000bar.csv and
// PASSES (so CI noticing a regen would have to fail explicitly). Always
// review the diff manually before committing a regenerated baseline.

#include <catch2/catch_test_macros.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/observers/increment_batcher.hpp>
#include <stratforge/observers/increment_types.hpp>
#include <stratforge/strategy/strategy.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using stratforge::Cerebro;
using stratforge::IncrementBatcher;
using stratforge::IncrementSnapshot;
using stratforge::SMA;
using stratforge::Strategy;
using stratforge::test::StaticFeed;

namespace {

// --- deterministic feed ---------------------------------------------------

/// Build a 1000-bar feed where close = trend + sine cycle. Tuned to
/// produce a non-trivial number of SMA-crossover events so the baseline
/// exercises both bar and trade accumulation paths.
[[nodiscard]] std::vector<StaticFeed::Bar> build_baseline_bars() {
    constexpr std::size_t kBars = 1000;
    constexpr double      kTrend = 0.05;       // slow drift per bar
    constexpr double      kAmp   = 15.0;       // oscillation amplitude
    constexpr double      kFreq  = 0.05;       // radians per bar

    std::vector<StaticFeed::Bar> bars;
    bars.reserve(kBars);
    for (std::size_t i = 0; i < kBars; ++i) {
        const double x = static_cast<double>(i);
        const double close = 100.0 + kTrend * x + kAmp * std::sin(kFreq * x);
        bars.push_back(StaticFeed::Bar{
            .open  = close,
            .high  = close,
            .low   = close,
            .close = close,
        });
    }
    return bars;
}

// --- deterministic SMA-crossover strategy --------------------------------

/// Buys 1 unit when fast SMA crosses above slow SMA, closes when it
/// crosses below. Mirrors examples/sma_crossover.cpp but with fixed
/// periods chosen so the baseline has ~5-10 round-trips on the fixture
/// feed (enough to exercise trade accumulation without overwhelming the
/// CSV diff).
class BaselineSmaCross final : public Strategy {
public:
    void init() override {
        fast_ = std::make_unique<SMA>(data().close(), 10);
        slow_ = std::make_unique<SMA>(data().close(), 30);
    }

    void next() override {
        fast_->next();
        slow_->next();

        if (fast_->line().size() == 0 || slow_->line().size() == 0) {
            return;
        }

        const double fast = fast_->line()[0];
        const double slow = slow_->line()[0];

        if (!position().is_long() && fast > slow) {
            static_cast<void>(buy(1.0));
        } else if (position().is_long() && fast < slow) {
            static_cast<void>(close());
        }
    }

private:
    std::unique_ptr<SMA> fast_;
    std::unique_ptr<SMA> slow_;
};

// --- baseline projection --------------------------------------------------

/// Project a snapshot to its baseline CSV row. Keeping this narrow (7
/// columns) is deliberate — every column the projection touches becomes
/// a constraint that future refactors must honor. Don't expand without
/// thinking about churn cost (§13.5 risk register).
[[nodiscard]] std::string project_row(const IncrementSnapshot& s) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(6);
    os << s.seq << ','
       << s.processed_bars << ','
       << (s.is_final ? 1 : 0) << ','
       << s.new_bars.size() << ','
       << s.new_trades.size() << ','
       << s.current_metrics.realized_pnl << ','
       << s.current_metrics.current_dd_pct;
    return os.str();
}

[[nodiscard]] std::vector<IncrementSnapshot> run_baseline_capture() {
    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.set_cash(10'000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(build_baseline_bars()));
    cerebro.add_strategy(std::make_unique<BaselineSmaCross>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 200,
            .max_interval               = std::chrono::milliseconds{60'000},  // disable interval
            .emit_first_bar_immediately = true,
        },
        [&snaps](const IncrementSnapshot& s, const std::vector<stratforge::DataFeed*>&) { snaps.push_back(s); }));

    cerebro.run();
    return snaps;
}

[[nodiscard]] std::filesystem::path baseline_path() {
    return std::filesystem::path(SF_SOURCE_DIR)
         / "tests" / "golden" / "increment_batcher_sma_cross_1000bar.csv";
}

[[nodiscard]] std::vector<std::string> read_baseline_rows(const std::filesystem::path& p) {
    std::vector<std::string> rows;
    std::ifstream in(p);
    if (!in.is_open()) {
        return rows;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            rows.push_back(std::move(line));
        }
    }
    return rows;
}

void write_baseline_rows(const std::filesystem::path& p,
                         const std::vector<std::string>& rows) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::trunc);
    for (const auto& r : rows) {
        out << r << '\n';
    }
}

[[nodiscard]] bool regen_requested() {
    const char* v = std::getenv("NBT_REGEN_BASELINE");
    return v != nullptr && v[0] != '\0' && std::strcmp(v, "0") != 0;
}

}  // namespace

TEST_CASE("IncrementBatcher snapshot stream matches the frozen 1000-bar SMA-cross baseline",
          "[observer][increment][regression]") {
    const auto snaps = run_baseline_capture();
    REQUIRE_FALSE(snaps.empty());

    // Invariants that the CSV alone does not pin down.
    REQUIRE(snaps.back().is_final);
    REQUIRE(snaps.back().processed_bars == 1000u);
    REQUIRE(snaps.back().total_bars.has_value());
    REQUIRE(*snaps.back().total_bars == 1000u);

    std::vector<std::string> actual_rows;
    actual_rows.reserve(snaps.size());
    for (const auto& s : snaps) {
        actual_rows.push_back(project_row(s));
    }

    const auto path = baseline_path();

    if (regen_requested()) {
        write_baseline_rows(path, actual_rows);
        WARN("Regenerated baseline at " << path.string()
             << " — review the diff before committing.");
        return;
    }

    const auto expected_rows = read_baseline_rows(path);
    INFO("baseline file: " << path.string());
    REQUIRE_FALSE(expected_rows.empty());
    REQUIRE(actual_rows.size() == expected_rows.size());

    for (std::size_t i = 0; i < actual_rows.size(); ++i) {
        INFO("row " << i << " actual=" << actual_rows[i]
                    << " expected=" << expected_rows[i]);
        CHECK(actual_rows[i] == expected_rows[i]);
    }
}

TEST_CASE("IncrementBatcher baseline run is deterministic across repeated invocations",
          "[observer][increment][regression]") {
    const auto first  = run_baseline_capture();
    const auto second = run_baseline_capture();
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        INFO("snapshot " << i);
        CHECK(project_row(first[i]) == project_row(second[i]));
    }
}
