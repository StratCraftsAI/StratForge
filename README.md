<p align="center">
  <h1 align="center">StratForge</h1>
  <p align="center">
    <strong>Modern C++23 Backtesting Engine for Systematic Trading Research</strong>
  </p>
  <p align="center">
    Header-Only | Golden-Master Validated | Multi-Timeframe | Zero-Allocation Hot Path Target
  </p>
</p>

<p align="center">
  <a href="https://github.com/StratCraftsAI/StratForge/actions/workflows/ci.yml"><img src="https://github.com/StratCraftsAI/StratForge/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-blue.svg" alt="C++23">
  <img src="https://img.shields.io/badge/Library-Header--Only-blue.svg" alt="Header-Only">
  <img src="https://img.shields.io/badge/License-Apache%202.0-green.svg" alt="Apache 2.0">
  <img src="https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg" alt="Cross-Platform">
  <img src="https://img.shields.io/badge/Indicators-157-orange.svg" alt="157 Indicators">
  <img src="https://img.shields.io/badge/Validation-Golden%20References-brightgreen.svg" alt="Golden References">
</p>

<p align="center">
  <a href="#why-stratforge">Why StratForge</a> |
  <a href="#features">Features</a> |
  <a href="#quick-start">Quick Start</a> |
  <a href="#validation">Validation</a> |
  <a href="#performance">Performance</a> |
  <a href="#documentation">Documentation</a> |
  <a href="#community">Community</a>
</p>

---

## Why StratForge

**StratForge** is a high-performance **C++23** backtesting engine for strategy research, indicator development, and systematic trading workflows.

It is built for users who want:

- A native C++ research loop instead of a Python runtime in the critical path
- A broad indicator surface without virtual-dispatch overhead on every bar
- Reproducible validation through golden-reference tests and benchmark harnesses
- A small dependency footprint that is easy to embed into larger quant stacks

StratForge is focused on **backtesting and strategy research**. Live connectivity, proprietary adapters, and internal orchestration layers are intentionally out of scope for the public distribution.

---

## Features

### Core Capabilities

- **157 indicators and utility primitives** spanning trend, momentum, volatility, volume, statistics, and candlestick analysis
- **10 candlestick patterns** including Doji, Hammer, Engulfing, Morning Star, and Shooting Star
- **Header-only distribution** via `#include <stratforge/stratforge.hpp>`
- **Multi-timeframe support** with resample and replay workflows
- **Portfolio and broker model** with orders, positions, trades, commission, slippage, and sizing
- **Analyzers** for Sharpe ratio, drawdown, returns, trade statistics, and extended metrics
- **Grid optimizer** for parameter sweeps
- **Example strategies** that compile and produce trades with the bundled sample dataset

### Modern C++23

- `std::expected`-friendly error-handling direction
- Concepts, designated initializers, and compile-time validation where appropriate
- Warning-clean CI across GCC, Clang, and MSVC
- CMake package export for both `FetchContent` and `find_package`

---

## Quick Start

### Requirements

- **C++23 compiler**: GCC 13+, Clang 17+, or MSVC 2022 17.10+
- **CMake**: 3.25+

### Build and Test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Run an Example

```bash
cmake --build build --target examples
./build/bin/examples/sma_crossover
```

The bundled examples read `tools/golden_extract/datas/2006-day-001.txt` and are intended to be the fastest way to confirm that your toolchain, broker model, analyzers, and data loading path are all working.

### Minimal Example

```cpp
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <iostream>
#include <memory>
#include <optional>

class SmaCross : public stratforge::Strategy {
public:
    void init() override {
        fast_ = std::make_unique<stratforge::SMA>(data().close(), 10);
        slow_ = std::make_unique<stratforge::SMA>(data().close(), 30);
    }

    void next() override {
        fast_->next();
        slow_->next();

        if (fast_->line().size() == 0 || slow_->line().size() == 0) {
            return;
        }

        if (!position().size && fast_->line()[0] > slow_->line()[0]) {
            (void)buy(2.0);
        } else if (position().size > 0 && fast_->line()[0] < slow_->line()[0]) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::SMA> fast_;
    std::unique_ptr<stratforge::SMA> slow_;
};

int main() {
    stratforge::Cerebro cerebro;

    auto feed = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
        .filename = "tools/golden_extract/datas/2006-day-001.txt",
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });

    if (!feed->load()) {
        std::cerr << "Failed to load data\n";
        return 1;
    }

    cerebro.add_data(std::move(feed));
    cerebro.add_strategy<SmaCross>();
    auto& trades = cerebro.add_analyzer<stratforge::TradeAnalyzer>();

    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});
    cerebro.run();

    std::cout << "Final cash: " << cerebro.broker().cash() << '\n';
    std::cout << "Total trades: " << trades.get_analysis().total.total << '\n';
}
```

### Downstream CMake Integration

#### `FetchContent`

```cmake
include(FetchContent)

FetchContent_Declare(
  stratforge
  GIT_REPOSITORY https://github.com/StratCraftsAI/StratForge.git
  GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(stratforge)

target_link_libraries(your_target PRIVATE stratforge)
```

#### Installed Package

```cmake
find_package(StratForge CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE StratForge::stratforge)
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `SF_BUILD_TESTS` | `ON` | Build the Catch2-based test suite |
| `SF_BUILD_EXAMPLES` | `ON` | Build example strategies |
| `SF_BUILD_BENCHMARKS` | `ON` | Build benchmark executables |
| `SF_ENABLE_SIMD` | `OFF` | Enable AVX2 flags for SIMD-aware builds |
| `SF_ENABLE_COVERAGE` | `OFF` | Enable coverage instrumentation on GCC/Clang |

---

## Validation

StratForge is validated as an engineering project, not just presented as a code sample.

### What is covered

- **Golden reference tests** under `tests/golden/` for indicators, broker behavior, analytics, resampling, replay, and multi-data flows
- **End-to-end tests** that exercise strategies and analyzers together
- **Example executables** that are expected to produce real trades on the bundled sample dataset
- **Benchmark suites** for indicators, composite strategies, optimizers, and allocation audits

### Why it matters

- Indicator correctness is checked against persisted expected outputs
- Regressions are easier to diagnose because the data files and expected values live in the repository
- Performance claims can be tied back to reproducible benchmark code instead of one-off screenshots

---

## Performance

The repository includes dedicated benchmark executables and allocation audits.

Current targets from [benchmarks/README.md](benchmarks/README.md):

| Metric | Target |
|--------|--------|
| Per-indicator computation | < 100 ns/bar P50 |
| Composite strategy bar processing | < 100 ns/bar |
| Hot-path allocations | 0 |
| Parallel optimization scaling | Near-linear |

These numbers should be treated as **engineering targets**, not unconditional marketing guarantees. Use the provided benchmark suite on your own hardware when validating changes or comparing configurations.

---

## Documentation

- [API Quick Start](docs/API_QUICK_START.md) for the shortest path from include to first backtest
- [Examples](examples/README.md) for end-to-end strategy patterns and expected outputs
- [Benchmarks](benchmarks/README.md) for measurement methodology and regression tracking
- [Support](SUPPORT.md) for bug reports and usage questions
- [Contributing](CONTRIBUTING.md) for patch expectations and performance validation rules
- [Roadmap](ROADMAP.md) for likely expansion areas
- [Changelog](CHANGELOG.md) for release-to-release changes
- [Security](SECURITY.md) for private vulnerability reporting
- [`tests/golden/`](tests/golden/) for persisted validation artifacts
- [`tools/golden_extract/datas/`](tools/golden_extract/datas/README.md) for bundled sample datasets

### Example Catalog

| Example | Description |
|---------|-------------|
| `sma_crossover.cpp` | Moving-average crossover strategy |
| `rsi_mean_reversion.cpp` | RSI threshold mean reversion |
| `macd_trend.cpp` | MACD signal-based trend following |
| `bollinger_bands.cpp` | Volatility breakout with stops |
| `multi_timeframe.cpp` | Daily/weekly multi-timeframe strategy |
| `pairs_trading.cpp` | Two-instrument spread trading example |
| `optimizer_example.cpp` | Parameter grid optimization |

---

## Community

- Open a GitHub issue for bugs, documentation gaps, or feature proposals
- Follow [SUPPORT.md](SUPPORT.md) for what to include in build and runtime reports
- Review [CONTRIBUTING.md](CONTRIBUTING.md) before sending public API, benchmark, or indicator changes
- Report vulnerabilities privately using [SECURITY.md](SECURITY.md)

---

## License

StratForge is licensed under the [Apache License 2.0](LICENSE).
