<p align="center">
  <h1 align="center">StratForge</h1>
  <p align="center">
    <strong>Modern C++23 Backtesting Engine for Systematic Trading Research</strong>
  </p>
  <p align="center">
    Header-Only | Golden-Master Validated | Multi-Timeframe | Zero-Allocation Hot Path
  </p>
  <p align="center">
    <a href="https://github.com/StratCraftsAI/StratForge/actions/workflows/ci.yml"><img src="https://github.com/StratCraftsAI/StratForge/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI"></a>
    <img src="https://img.shields.io/badge/C%2B%2B-23-blue.svg" alt="C++23">
    <img src="https://img.shields.io/badge/license-Apache--2.0-green.svg" alt="License">
  </p>
</p>

<p align="center">
  <a href="https://github.com/StratCraftsAI/StratForge/actions/workflows/ci.yml"><img src="https://github.com/StratCraftsAI/StratForge/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
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
  <a href="#validation">Validation</a> |
  <a href="#quick-start">Quick Start</a> |
  <a href="#documentation">Documentation</a> |
  <a href="#performance">Performance</a>
</p>

---

## Why StratForge

**StratForge** is a high-performance **C++23** backtesting engine for strategy research, indicator development, and systematic trading workflows. It packages a backtrader-style programming model into a **header-only**, **zero-overhead** library with a strong focus on deterministic behavior, reproducible tests, and public benchmark methodology.

It is designed for users who want:

- A native C++ engine instead of a Python runtime in the research loop
- A broad indicator surface area without paying virtual-dispatch or dynamic-allocation costs on every bar
- Reproducible behavior through golden-reference tests, not just ad hoc examples
- A small dependency footprint that is easy to embed into larger quant stacks

---

## Features

### Core Capabilities

- **157 indicators and utility primitives** covering trend, momentum, volatility, volume, statistics, and candlestick analysis
- **10 candlestick patterns** including Doji, Hammer, Engulfing, Morning Star, and Shooting Star
- **Header-only distribution** via `#include <stratforge/stratforge.hpp>`
- **Zero-allocation hot path target** for per-bar processing
- **Multi-timeframe support** with resample and replay flows
- **Portfolio and broker model** with orders, positions, trades, commission, and sizing
- **Analyzers** for Sharpe ratio, drawdown, returns, trade statistics, and extended metrics
- **Grid optimizer** for multi-parameter strategy sweeps
- **LLM-oriented examples** for code generation and strategy templating workflows

### Modern C++23

- `std::expected`-friendly design direction and exception avoidance on hot paths
- Concepts, designated initializers, and compile-time validation where appropriate
- Strict warning-clean builds across GCC, Clang, and MSVC
- Portable CMake-based integration for local builds and FetchContent consumers

---

## Validation

StratForge is validated as an engineering project, not just a code drop.

### What is covered

- **Golden reference tests** under `tests/golden/` for indicators, broker behavior, analytics, resampling, replay, and multi-data flows
- **Cross-validation inputs** under `tools/golden_extract/` for reproducible comparisons
- **End-to-end integration tests** covering strategy execution and analyzer outputs
- **Benchmark suites** for indicators, composite strategies, optimizers, and allocation audits

### Why it matters

- Indicator correctness is checked against persisted expected outputs
- Regression detection is easier because data files and expected values live in the repository
- Performance claims can be tied back to reproducible benchmark code instead of one-off measurements

---

## Quick Start

### Requirements

- **C++23 compiler**: GCC 13+, Clang 17+, or MSVC 2022 17.10+
- **CMake**: 3.25+

### Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Minimal Example

```cpp
#include <stratforge/stratforge.hpp>

#include <iostream>
#include <memory>

class SmaCross : public stratforge::Strategy {
    std::unique_ptr<stratforge::SMA> fast_;
    std::unique_ptr<stratforge::SMA> slow_;

public:
    void init() override {
        fast_ = std::make_unique<stratforge::SMA>(data().close(), 10);
        slow_ = std::make_unique<stratforge::SMA>(data().close(), 30);
    }

    void next() override {
        if (fast_->line().size() == 0 || slow_->line().size() == 0) {
            return;
        }

        const double fast = fast_->line()[0];
        const double slow = slow_->line()[0];

        if (!position().size && fast > slow) {
            (void)buy(100.0);
        } else if (position().size > 0 && fast < slow) {
            (void)close();
        }
    }
};

int main() {
    stratforge::Cerebro cerebro;
    cerebro.add_strategy<SmaCross>();

    auto feed = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
        .filename = "data.csv",
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
    auto& trades = cerebro.add_analyzer<stratforge::TradeAnalyzer>();
    cerebro.set_cash(10000.0);
    cerebro.run();

    std::cout << "Final cash: " << cerebro.broker().cash() << '\n';
    std::cout << "Total trades: " << trades.get_analysis().total.total << '\n';
}
```

### CMake Integration

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

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `SF_BUILD_TESTS` | `ON` | Build Catch2-based test suite |
| `SF_BUILD_EXAMPLES` | `ON` | Build example strategies |
| `SF_BUILD_BENCHMARKS` | `ON` | Build benchmark executables |

The bundled examples use `SF_SOURCE_DIR` to locate repository sample data. External consumers can point `CsvData` at their own datasets directly.

---

## Documentation

- [Examples](examples/README.md) for end-to-end strategy patterns
- [Benchmarks](benchmarks/README.md) for methodology and baseline targets
- [Support](SUPPORT.md) for bug reports and usage questions
- [Roadmap](ROADMAP.md) for planned project expansion
- [Changelog](CHANGELOG.md) for release-to-release changes
- [`tests/golden/`](tests/golden/) for persisted reference outputs
- [`tools/golden_extract/datas/`](tools/golden_extract/datas/README.md) for sample datasets used in validation
- [`docs/tickets/`](docs/tickets/) for current project hardening tickets

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

## Performance

The repository includes dedicated benchmark executables and allocation audits.

Current benchmark targets from [`benchmarks/README.md`](benchmarks/README.md):

| Metric | Target |
|--------|--------|
| Per-indicator computation | < 100 ns/bar P50 |
| Composite strategy bar processing | < 100 ns/bar |
| Hot-path allocations | 0 |
| Parallel optimization scaling | Near-linear |

Use the benchmark suite to validate changes rather than treating these numbers as static marketing claims.

---

## Indicators

<details>
<summary>Full indicator list</summary>

### Trend
SMA, EMA, DEMA, TEMA, WMA, HMA, KAMA, TRIMA, ZLEMA, ZeroLag, SMMA, DMA, Laguerre, ExponentialSmoothing

### Momentum
RSI, MACD, MACDExt, Stochastic, StochRSI, CCI, MOM, ROC, ROCP, ROCR, ROCR100, Williams %R, Ultimate Oscillator, TSI, KST, Awesome Oscillator, DPO, Pretty Good Oscillator, BOP, DV2, Aroon

### Volatility
ATR, NATR, Bollinger Bands, Bollinger %B, Envelope, True Range, True High, True Low, Standard Deviation, Variance, Hurst Exponent, Volatility

### Volume
OBV, Accumulation/Distribution, Volume indicators

### Overlap
Parabolic SAR, Ichimoku, Pivot Points, Hilbert Transform, Linear Regression

### Statistics
Mean Deviation, Percent Rank, OLS Regression, Min/Max, Highest, Lowest, Midpoint

### Candlestick Patterns
Doji, Hammer, Inverted Hammer, Bullish Engulfing, Bearish Engulfing, Morning Star, Evening Star, Shooting Star, Hanging Man, Marubozu

### Utility
Crossover, UpDay, DownDay, UpMove, DownMove, PercentChange, Accumulator, FindIndex

</details>

---

## Community

- Contribution policy: [CONTRIBUTING.md](CONTRIBUTING.md)
- Support policy: [SUPPORT.md](SUPPORT.md)
- Security reporting: [SECURITY.md](SECURITY.md)
- Code of conduct: [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)

## License

StratForge is released under the [Apache License 2.0](LICENSE).
