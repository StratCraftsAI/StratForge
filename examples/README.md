# StratForge Example Strategies

Self-contained, production-quality example strategies demonstrating the stratforge:: API.

These examples serve as:
1. **Documentation** - Learn StratForge API usage patterns
2. **Templates** - Starting points for your own strategies
3. **AI Prompting References** - Example implementations for code generation workflows

## Building Examples

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target examples
```

Executables are output to: `build/bin/examples/`

## Running Examples

```bash
# Individual examples
./build/bin/examples/sma_crossover
./build/bin/examples/rsi_mean_reversion
./build/bin/examples/macd_trend
./build/bin/examples/bollinger_bands
./build/bin/examples/multi_timeframe
./build/bin/examples/pairs_trading

# Run all examples
for exe in ./build/bin/examples/*; do
    echo "=== Running $exe ==="
    $exe
    echo
done
```

## Example Catalog

### 1. `sma_crossover.cpp`
**Demonstrates:** Buy/sell signals, TradeAnalyzer, basic reporting

**Strategy:** Buy when fast SMA crosses above slow SMA, sell when crosses below.

**Key API:**
- `stratforge::SMA` - Simple Moving Average indicator
- `stratforge::Strategy::buy()` / `close()` - Order submission
- `stratforge::TradeAnalyzer` - Trade statistics
- `stratforge::Cerebro::run()` - Backtest execution

**Indicators:** SMA(10), SMA(30)

---

### 2. `rsi_mean_reversion.cpp`
**Demonstrates:** Threshold buy/sell, position sizing, DrawDown analyzer

**Strategy:** Buy when RSI < 25 (oversold), sell when RSI > 75 (overbought).

**Key API:**
- `stratforge::RSI` - Relative Strength Index
- `stratforge::PercentSizer` - Percentage-based position sizing (95% of cash)
- `stratforge::Strategy::setsizer()` - Configure position sizing
- `stratforge::Drawdown` - Drawdown tracking
- `stratforge::Strategy::default_params()` - Strategy parameterization

**Indicators:** RSI(14)

**Parameters:** `rsi_period`, `oversold`, `overbought`

---

### 3. `macd_trend.cpp`
**Demonstrates:** Signal crossover, SharpeRatio analyzer

**Strategy:** Buy when MACD crosses above signal line, sell when crosses below.

**Key API:**
- `stratforge::MACD` - MACD indicator with 3 lines (MACD, signal, histogram)
- `stratforge::SharpeRatio` - Risk-adjusted performance metric
- Multi-line indicator access: `macd.macd()`, `macd.signal()`, `macd.histogram()`

**Indicators:** MACD(12, 26, 9)

---

### 4. `bollinger_bands.cpp`
**Demonstrates:** Band breakout, stop-loss orders, order notifications

**Strategy:** Buy on lower band touch, sell on upper band touch or stop-loss.

**Key API:**
- `stratforge::BollingerBands` - Volatility bands (top, mid, bottom)
- `stratforge::Strategy::notify_order()` - Order status callbacks
- `stratforge::OrderType::Stop` - Stop-loss orders
- `stratforge::Strategy::cancel()` - Cancel pending orders
- `stratforge::Order` status tracking

**Indicators:** BollingerBands(20, 2.0)

**Features:** Automatic 5% stop-loss on entries

---

### 5. `multi_timeframe.cpp`
**Demonstrates:** Resampler, multi-data access, timeframe alignment

**Strategy:** Trade on daily data, use weekly EMA as trend filter.

**Key API:**
- `stratforge::Resampler` - Convert daily to weekly data
- `stratforge::Strategy::data(index)` - Access multiple data feeds
- `stratforge::Strategy::data_name(index)` - Get feed names
- `stratforge::Cerebro::add_data()` with names - Named multi-data
- Multi-timeframe trend filtering

**Indicators:** Daily EMA(20), Weekly EMA(10)

**Data Feeds:** 2 (daily + weekly resampled)

---

### 6. `pairs_trading.cpp`
**Demonstrates:** Multi-data, spread calculation, market-neutral strategy

**Strategy:** Trade the spread between two assets when it diverges/reverts.

**Key API:**
- Multiple data feed access: `data(0)`, `data(1)`
- `stratforge::Strategy::position(data_index)` - Per-feed positions
- Order submission with `data_index` parameter
- Spread statistics tracking (z-score)
- Market-neutral long/short pairs

**Indicators:** SMA(20) on both assets

**Data Feeds:** 2 (asset1, asset2)

**Note:** Example uses same data twice for demonstration. Production usage requires two correlated assets.

---

## Common Patterns

### Loading Data

```cpp
auto feed = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
    .filename = source_path("tools/golden_extract/datas/2006-day-001.txt"),
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
```

### Strategy Structure

```cpp
class MyStrategy : public stratforge::Strategy {
public:
    void init() override {
        // Create indicators here
        indicator_ = std::make_unique<stratforge::SMA>(data().close(), 20);
    }

    void next() override {
        // Check indicator validity
        if (indicator_->line().size() == 0) return;

        // Trading logic here
        if (!position().size && /* entry condition */) {
            (void)buy(100.0);
        }
        else if (position().size > 0 && /* exit condition */) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::SMA> indicator_;
};
```

### Adding Analyzers

```cpp
auto& trade_analyzer = cerebro.add_analyzer<stratforge::TradeAnalyzer>();
auto& sharpe = cerebro.add_analyzer<stratforge::SharpeRatio>();
auto& drawdown = cerebro.add_analyzer<stratforge::Drawdown>();

cerebro.run();

// Access results
const auto& trades = trade_analyzer.get_analysis();
const auto& sharpe_analysis = sharpe.get_analysis();
const auto& dd = drawdown.get_analysis();
```

### Broker Configuration

```cpp
cerebro.set_cash(10000.0);
cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001}); // 0.1%
cerebro.set_slippage_perc(0.001); // 0.1% slippage
```

## Data Files

All examples use: `tools/golden_extract/datas/2006-day-001.txt`
- Format: CSV with headers (Date, Open, High, Low, Close, Volume)
- Frequency: Daily
- Period: Full year 2006
- Bars: 255

## Code Style Guidelines

These examples follow strict coding standards for reusable strategy templates:

1. **Self-contained** - Each example compiles independently
2. **Production-quality** - Clean code, proper error handling, no TODO comments
3. **Well-documented** - Clear comments explaining strategy logic
4. **API-focused** - Demonstrates specific StratForge features
5. **Realistic** - Trade logic makes sense, not toy examples

## AI-Assisted Code Generation

These examples are designed to serve as templates for AI-assisted strategy generation workflows.

**Template Selection Criteria:**
- SMA Crossover: Basic trend following
- RSI Mean Reversion: Oscillator-based mean reversion
- MACD Trend: Multi-line indicator signals
- Bollinger Bands: Volatility breakout + risk management
- Multi-Timeframe: Complex multi-data trend filtering
- Pairs Trading: Market-neutral statistical arbitrage

## Next Steps

- Extend examples with more indicators (Stochastic, Ichimoku, etc.)
- Create optimization examples using `stratforge::Optimizer`
- Add more AI-assisted strategy examples
