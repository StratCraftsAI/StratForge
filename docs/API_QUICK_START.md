# StratForge API Quick Start

This guide is the shortest path from clone to a working backtest.

If you want broader project context first, start with the [README](../README.md). If you want complete examples, go to [examples/README.md](../examples/README.md).

---

## 1. Include the Main Headers

```cpp
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/strategy/strategy.hpp>
```

---

## 2. Define a Strategy

Indicators are not advanced automatically inside your strategy. In `next()`, call `indicator->next()` before reading the latest value.

```cpp
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
```

---

## 3. Load CSV Data

```cpp
#include <optional>

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
```

Expected CSV shape:

- `Date`
- `Open`
- `High`
- `Low`
- `Close`
- `Volume`

---

## 4. Build the Backtest

```cpp
stratforge::Cerebro cerebro;

cerebro.add_data(std::move(feed));
cerebro.add_strategy<SmaCross>();
auto& trades = cerebro.add_analyzer<stratforge::TradeAnalyzer>();

cerebro.set_cash(10000.0);
cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});
cerebro.run();

std::cout << "Final cash: " << cerebro.broker().cash() << '\n';
std::cout << "Total trades: " << trades.get_analysis().total.total << '\n';
```

---

## 5. Build It

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run a bundled example:

```bash
./build/bin/examples/sma_crossover
```

Run the full test suite:

```bash
ctest --test-dir build --output-on-failure
```

---

## 6. Common Next Steps

Use these when you move past the minimal SMA example:

- Add `stratforge::Drawdown` or `stratforge::SharpeRatio` analyzers for richer output
- Use `stratforge::PercentSizer` when you want capital-relative sizing instead of fixed share counts
- Add a second feed and use `data(1)` for multi-data or pairs workflows
- Use `stratforge::Resampler` to build weekly data from a daily source
- Use `engine/optimizer.hpp` when you want parameter sweeps

---

## 7. Common Mistakes

- **No trades execute**: position size is too large for available cash
- **Indicators stay empty**: you forgot to call `indicator->next()` inside `Strategy::next()`
- **Results differ unexpectedly**: confirm the dataset, commission, and slippage settings
- **Benchmark numbers look too slow**: verify `Release` mode and run the dedicated benchmark executables instead of timing debug builds

---

## 8. Where to Go Next

- [examples/README.md](../examples/README.md)
- [benchmarks/README.md](../benchmarks/README.md)
- [CONTRIBUTING.md](../CONTRIBUTING.md)
- [SUPPORT.md](../SUPPORT.md)
