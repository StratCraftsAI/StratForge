# stratforge:: API Reference

**Version**: Phase 10
**Status**: API Freeze for LLM Generation
**Date**: 2026-04-24
**Indicators**: 260 registry entries (including compatibility aliases and presets)

This document defines the complete public API surface for stratforge:: C++23 backtesting engine.

For the complete Qlib Alpha158 and TA-Lib candlestick compatibility surface,
including the distinction between continuous factors and discrete pattern
signals, see [Alpha158 and TA-Lib Candlestick Compatibility](alpha158-candlestick-compatibility.md).

---

## Table of Contents

1. [Architecture](#architecture)
2. [Core Engine](#core-engine)
3. [Strategy](#strategy)
4. [Indicators](#indicators)
5. [Broker](#broker)
6. [Data Feeds](#data-feeds)
7. [Analyzers](#analyzers)
8. [Observers](#observers)
9. [Sizers](#sizers)
10. [Optimizer](#optimizer)
11. [Live Trading](#live-trading)

---

## Architecture

### CRTP Indicator System

All indicators use a dual-base CRTP architecture for zero-overhead dispatch:

```cpp
// Virtual base for polymorphic storage (used only by MACDExt)
class IndicatorBase {
public:
    virtual void next() = 0;
    [[nodiscard]] virtual std::size_t minimum_period() const noexcept = 0;
    [[nodiscard]] const Line<double>& line() const noexcept;
protected:
    void reserve_output(std::size_t expected_bars);
    Line<double> line_;
};

// CRTP base — all concrete indicators inherit from this
template <typename Derived>
class Indicator : public IndicatorBase {
    void next() final { static_cast<Derived*>(this)->next_impl(); }
    [[nodiscard]] std::size_t minimum_period() const noexcept final {
        return static_cast<const Derived*>(this)->minimum_period_impl();
    }
};

// Period-based CRTP base for indicators operating on trailing windows
template <typename Derived>
class PeriodN : public IndicatorBase {
protected:
    [[nodiscard]] bool in_warmup() const noexcept;
};
```

### Compile-time Period Validation

```cpp
#include <stratforge/core/period_validate.hpp>

consteval std::size_t validated_period(std::size_t p);
// Usage: SMA sma(source, validated_period(14));  // compile-time error if 0
```

---

## Core Engine

### `stratforge::Cerebro`

Central orchestrator for backtesting.

```cpp
class Cerebro {
public:
    struct RunOptions {
        bool runonce = false;
        bool preload = true;
    };

    // Data management
    void add_data(std::unique_ptr<DataFeed> feed, std::string name = {});

    // Strategy management
    template <typename S, typename... Args>
    S& add_strategy(Args&&... args);
    void add_strategy(std::unique_ptr<Strategy> strategy);

    template <typename S, typename... Args>
    S& add_strategy_with_params(ParamMap params, Args&&... args);

    // Analyzer management
    template <typename A, typename... Args>
    A& add_analyzer(Args&&... args);

    // Observer management
    template <typename O, typename... Args>
    O& add_observer(Args&&... args);

    // Broker configuration
    void set_cash(double cash);
    void set_commission(CommissionInfo info);
    void set_slippage_perc(double perc, ...);
    void set_slippage_fixed(double fixed, ...);

    // Execution
    void run(RunOptions options = {});

    // Access
    const BackBroker& broker() const noexcept;
    BackBroker& broker() noexcept;
};
```

---

## Strategy

### `stratforge::Strategy`

Base class for all trading strategies.

```cpp
class Strategy {
public:
    // Lifecycle hooks (override in derived)
    virtual void init();
    virtual void start();
    virtual void prenext();
    virtual void nextstart();
    virtual void next();
    virtual void stop();

    // Notification callbacks
    virtual void notify_order(const Order& order);
    virtual void notify_trade(const Trade& trade, double original_size);

    // Order submission
    [[nodiscard]] std::size_t buy(std::optional<double> size = std::nullopt,
                                   double price = 0.0,
                                   OrderType type = OrderType::Market,
                                   std::size_t data_index = 0);

    [[nodiscard]] std::size_t sell(std::optional<double> size = std::nullopt,
                                    double price = 0.0,
                                    OrderType type = OrderType::Market,
                                    std::size_t data_index = 0);

    [[nodiscard]] std::size_t close(std::size_t data_index = 0);
    void cancel(std::size_t order_id);

    // Extended order submission
    [[nodiscard]] std::size_t buy_ext(double size, const BackBroker::OrderParams& params);
    [[nodiscard]] std::size_t sell_ext(double size, const BackBroker::OrderParams& params);

    // Bracket orders (take-profit + stop-loss)
    [[nodiscard]] BackBroker::BracketResult buy_bracket(
        double size, double limit_price, double stop_price, std::size_t data_index = 0);
    [[nodiscard]] BackBroker::BracketResult sell_bracket(
        double size, double limit_price, double stop_price, std::size_t data_index = 0);

    // Data access
    [[nodiscard]] const DataFeed& data(std::size_t index = 0) const;
    [[nodiscard]] const DataFeed& data(std::string_view name) const;
    [[nodiscard]] bool has_data(std::string_view name) const;
    [[nodiscard]] const std::string& data_name(std::size_t index) const;
    [[nodiscard]] std::size_t data_count() const noexcept;

    // Position access
    [[nodiscard]] const Position& position(std::size_t data_index = 0) const;

    // Parameters
    [[nodiscard]] virtual ParamMap default_params() const;
    [[nodiscard]] ParamView params() const noexcept;
    void set_params(ParamMap params);

    // Sizer
    void setsizer(std::unique_ptr<Sizer> sizer);
    [[nodiscard]] const Sizer& sizer() const noexcept;
};
```

---

## Indicators

stratforge provides **260 registry entries**, including compatibility aliases
and parameter presets. All indicators use the CRTP pattern via
`Indicator<Derived>` or `PeriodN<Derived>`.

### Common API Pattern

```cpp
class IndicatorName : public Indicator<IndicatorName> {
public:
    explicit IndicatorName(const Line<double>& source, std::size_t period, ...);

    void next_impl();                                           // called via CRTP
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept;

    // Output line access
    [[nodiscard]] const Line<double>& line() const noexcept;    // inherited
    // Multi-line indicators have named accessors (e.g., macd(), signal(), histogram())
};
```

### Moving Averages (15)

| Class | Aliases | Header |
|-------|---------|--------|
| `SMA` | `MA` | sma.hpp |
| `EMA` | — | ema.hpp |
| `SMMA` | — | smma.hpp |
| `WMA` | — | wma.hpp |
| `HMA` | — | hma.hpp |
| `DoubleExponentialMovingAverage` | `DEMA` | dema.hpp |
| `TripleExponentialMovingAverage` | `TEMA` | tema.hpp |
| `TRIMA` | `TriangularMovingAverage` | trima.hpp |
| `AdaptiveMovingAverage` | `KAMA` | kama.hpp |
| `DicksonMovingAverage` | `DMA` | dma.hpp |
| `ZeroLagExponentialMovingAverage` | `ZLEMA` | zlema.hpp |
| `ZeroLagIndicator` | `ZLIndicator`, `EC` | zerolag.hpp |
| `ExponentialSmoothing` | `ExpSmoothing` | exponentialsmoothing.hpp |
| `ExponentialSmoothingDynamic` | `ExpSmoothingDynamic` | exponentialsmoothing.hpp |
| `VIDYA` | — | adaptive.hpp |
| `FRAMA` | — | adaptive.hpp |
| `T3MovingAverage` | `T3` | oscillator_extra.hpp |

### Oscillators & Momentum (25)

| Class | Aliases | Header |
|-------|---------|--------|
| `RSI` | — | rsi.hpp |
| `RSI_Safe` | — | rsi.hpp |
| `RSI_SMA` | — | rsi.hpp |
| `RSI_EMA` | — | rsi.hpp |
| `RelativeMomentumIndex` | `RMI` | rsi.hpp |
| `MACD` | — | macd.hpp |
| `MACDHisto` | — | macd.hpp |
| `MACDExt` | `MACDEXT` | macdext.hpp |
| `MACDFix` | `MACDFIX` | macdfix.hpp |
| `Stochastic` | `StochasticSlow` | stochastic.hpp |
| `StochasticFull` | — | stochastic.hpp |
| `StochasticFast` | — | stochastic.hpp |
| `KDJ` | — | stochastic.hpp |
| `StochRSI` | `STOCHRSI` | stochrsi.hpp |
| `CCI` | — | cci.hpp |
| `Momentum` | — | momentum.hpp |
| `ROC` | — | momentum.hpp |
| `ROC100` | — | momentum.hpp |
| `MomentumOscillator` | — | momentum.hpp |
| `ROCP` | `RateOfChangePercentage` | roc_variants.hpp |
| `ROCR` | `RateOfChangeRatio` | roc_variants.hpp |
| `ROCR100` | `RateOfChangeRatio100` | roc_variants.hpp |
| `ChandeMomentumOscillator` | `CMO` | oscillator_extra.hpp |
| `UltimateOscillator` | `UO` | ultimateoscillator.hpp |
| `TrueStrengthIndicator` | `TSI` | tsi.hpp |
| `KnowSureThing` | `KST` | kst.hpp |
| `AwesomeOscillator` | `AO` | awesomeoscillator.hpp |
| `AccelerationDecelerationOscillator` | `AccDeOsc` | awesomeoscillator.hpp |

### Volatility & Bands (9)

| Class | Aliases | Header |
|-------|---------|--------|
| `BollingerBands` | — | bollinger.hpp |
| `BollingerBandsPct` | `BBandsPct` | bollingerpct.hpp |
| `ATR` | — | atr.hpp |
| `NATR` | `NormalizedATR` | natr.hpp |
| `StdDev` | — | stddev.hpp |
| `DonchianChannels` | `Donchian` | volatility.hpp |
| `KeltnerChannel` | `Keltner` | volatility.hpp |
| `UlcerIndex` | `Ulcer` | volatility.hpp |
| `ChaikinVolatility` | — | volatility.hpp |

### Trend (10)

| Class | Aliases | Header |
|-------|---------|--------|
| `DirectionalMovement` | — | directionalmovement.hpp |
| `PlusDirectionalIndicator` | — | directionalmovement.hpp |
| `MinusDirectionalIndicator` | — | directionalmovement.hpp |
| `ADX` | — | directionalmovement.hpp |
| `DirectionalIndicator` | — | directionalmovement.hpp |
| `ADXR` | — | directionalmovement.hpp |
| `Aroon` / `AroonUp` / `AroonDown` | — | aroon.hpp |
| `AroonOscillator` / `AroonUpDown` / `AroonUpDownOscillator` | — | aroon.hpp |
| `ParabolicSAR` | `PSAR` | psar.hpp |
| `Ichimoku` | — | ichimoku.hpp |
| `SuperTrend` | — | trend.hpp |
| `ChandelierExit` | — | trend.hpp |
| `Vortex` | `VortexIndicator` | vortex.hpp |
| `DetrendedPriceOscillator` | `DPO` | dpo.hpp |

### Price Transforms (5)

| Class | Aliases | Header |
|-------|---------|--------|
| `AvgPrice` | `AVGPRICE` | avgprice.hpp |
| `MedPrice` | `MEDPRICE`, `MedianPrice` | medprice.hpp |
| `TypPrice` | `TYPPRICE`, `TypicalPrice` | typprice.hpp |
| `WclPrice` | `WCLPRICE`, `WeightedClosePrice` | wclprice.hpp |
| `HeikinAshi` | — | heikinashi.hpp |

### Volume (7)

| Class | Aliases | Header |
|-------|---------|--------|
| `OBV` | — | obv.hpp |
| `AccumulationDistribution` | `AD` | volume.hpp |
| `PriceVolumeTrend` | `PVT` | volume.hpp |
| `VolumeWeightedAveragePrice` | `VWAP` | volume.hpp |
| `ChaikinMoneyFlow` | `CMF` | volume.hpp |
| `MoneyFlowIndex` | `MFI` | volume.hpp |
| `EaseOfMovement` | `EMV` | volume.hpp |
| `KlingerOscillator` | `Klinger` | volume.hpp |

### Statistics & Regression (8)

| Class | Aliases | Header |
|-------|---------|--------|
| `LinearReg` | `LINEARREG` | linearreg.hpp |
| `LinearRegSlope` | `LINEARREG_SLOPE` | linearreg.hpp |
| `LinearRegIntercept` | `LINEARREG_INTERCEPT` | linearreg.hpp |
| `LinearRegAngle` | `LINEARREG_ANGLE` | linearreg.hpp |
| `TSF` | `TimeSeriesForecast` | linearreg.hpp |
| `Correlation` | `Correl` | statistics.hpp |
| `RSquared` | — | statistics.hpp |
| `Variance` | `VAR` | variance.hpp |

### Crossover & Logic (5)

| Class | Aliases | Header |
|-------|---------|--------|
| `CrossOver` | — | crossover.hpp |
| `CrossUp` | — | crossover.hpp |
| `CrossDown` | — | crossover.hpp |
| `NonZeroDifference` | `NZD` | crossover.hpp |
| `AnyN` | — | logicaln.hpp |
| `AllN` | — | logicaln.hpp |

### Utility & Helpers (14)

| Class | Aliases | Header |
|-------|---------|--------|
| `Accum` | `CumSum` | accum.hpp |
| `SumN` | `Sum` | sumn.hpp |
| `Average` | `Mean` | average.hpp |
| `WeightedAverage` | `AverageWeighted` | weightedaverage.hpp |
| `PercentRank` | `PctRank` | pctrank.hpp |
| `PercentChange` | `PctChange` | percentchange.hpp |
| `Highest` | `MaxN` | highest.hpp |
| `Lowest` | `MinN` | lowest.hpp |
| `MinMax` / `MaxIndex` / `MinIndex` / `MinMaxIndex` | — | minmax.hpp |
| `MidPoint` | `MIDPOINT` | midpoint.hpp |
| `MidPrice` | `MIDPRICE` | midpoint.hpp |
| `BalanceOfPower` | `BOP` | bop.hpp |
| `DV2` | — | dv2.hpp |
| `MeanDeviation` | — | meandeviation.hpp |

### Directional Movement Standalone (2)

| Class | Aliases | Header |
|-------|---------|--------|
| `PlusDM` | `PLUS_DM` | dm_standalone.hpp |
| `MinusDM` | `MINUS_DM` | dm_standalone.hpp |

### True Range Components (3)

| Class | Header |
|-------|--------|
| `TrueHigh` | truehigh.hpp |
| `TrueLow` | truelow.hpp |
| `TrueRange` | truerange.hpp |

### Day/Move Components (5)

| Class | Header |
|-------|--------|
| `UpDay` / `UpDayBool` | upday.hpp |
| `DownDay` / `DownDayBool` | downday.hpp |
| `UpMove` | upmove.hpp |
| `DownMove` | downmove.hpp |

### Pattern Recognition (3)

| Class | Aliases | Header |
|-------|---------|--------|
| `FisherTransform` | `Fisher` | pattern.hpp |
| `Fractal` | — | pattern.hpp |
| `ZigZag` | — | pattern.hpp |

### Candle Structure and Context-Free Geometry

These types complement, rather than redefine, the TA-Lib `CDL*` recognizers.
Their exact validity, tie, warmup, containment, and recovery semantics are
specified in [Candle Structure and Context Contract](candle-structure-and-context-contract.md).

| Class / preset | Outputs | Parameters and behavior | Header |
|----------------|---------|-------------------------|--------|
| `CandleGeometry` | `body`, `range`, `upper_wick`, `lower_wick`, `signed_gap`, `gap` | Raw continuous current-bar geometry; gap outputs require a valid preceding bar | candle_structure_common.hpp |
| `NormalizedCandleGeometry` | same as `CandleGeometry` | Divides all outputs by one shared Wilder `ATR`; `atr_period` defaults to 14 | candle_structure_common.hpp |
| `CandlePercentile` | `line` | Strict-below rolling rank for `body`, `range`, `upper_wick`, `lower_wick`, or absolute `gap`; current sample participates | candle_structure_common.hpp |
| `OutsideBar` | `line` (`0/1`) | Inclusive by default; pass `BoundaryMode::strict` for strict containment | candle_structure.hpp |
| `InsideBarDepth` | `depth`, `mother_range`, `breakout` | Tracks the active mother bar; breakout is `-1`, `0`, or `+1` | candle_structure.hpp |
| `NarrowRange` | `line` (`0/1`) | Current range must be the unique strict minimum in `period`; registry presets `NR4` and `NR7` use periods 4 and 7 | candle_structure.hpp |
| `WideRange` | `line` (`0/1`) | Current range must be the unique strict maximum in `period` | candle_structure.hpp |

Example:

```cpp
stratforge::NormalizedCandleGeometry normalized{
    data.open(), data.high(), data.low(), data.close(), 14};
stratforge::CandlePercentile range_rank{
    data.open(), data.high(), data.low(), data.close(),
    stratforge::CandleMeasurement::range, 50};
stratforge::InsideBarDepth inside{
    data.high(), data.low(), stratforge::BoundaryMode::inclusive};
stratforge::NarrowRange nr4{
    data.open(), data.high(), data.low(), data.close(), 4};
```

### Candlestick Patterns (10)

| Class | Header |
|-------|--------|
| `CDLDoji` | candlestick.hpp |
| `CDLHammer` | candlestick.hpp |
| `CDLEngulfing` | candlestick.hpp |
| `CDLMorningStar` | candlestick.hpp |
| `CDLEveningStar` | candlestick.hpp |
| `CDLHarami` | candlestick.hpp |
| `CDLShootingStar` | candlestick.hpp |
| `CDLHangingMan` | candlestick.hpp |
| `CDLMarubozu` | candlestick.hpp |
| `CDLSpinningTop` | candlestick.hpp |

### Hilbert Transform (2)

| Class | Aliases | Header |
|-------|---------|--------|
| `HT_Trendline` | `HilbertTrendline` | hilbert.hpp |
| `MAMA` | `MESAAdaptiveMovingAverage` | hilbert.hpp |

### Pivot Points (3)

| Class | Header |
|-------|--------|
| `PivotPoint` | pivotpoint.hpp |
| `FibonacciPivotPoint` | pivotpoint.hpp |
| `DemarkPivotPoint` | pivotpoint.hpp |

### Laguerre (2)

| Class | Aliases | Header |
|-------|---------|--------|
| `LaguerreRSI` | `LRSI` | laguerre.hpp |
| `LaguerreFilter` | `LAGF` | laguerre.hpp |

### TRIX (2)

| Class | Aliases | Header |
|-------|---------|--------|
| `Trix` | `TRIX` | trix.hpp |
| `TrixSignal` | `TRIXSignal` | trix.hpp |

### Hurst & OLS (4)

| Class | Aliases | Header |
|-------|---------|--------|
| `HurstExponent` | `Hurst` | hurst.hpp |
| `OLS_Slope_InterceptN` | — | ols.hpp |
| `OLS_TransformationN` | — | ols.hpp |
| `OLS_BetaN` | — | ols.hpp |

### FindIndex (4)

| Class | Header |
|-------|--------|
| `FindFirstIndexHighest` | findindex.hpp |
| `FindLastIndexHighest` | findindex.hpp |
| `FindFirstIndexLowest` | findindex.hpp |
| `FindLastIndexLowest` | findindex.hpp |

### haDelta (1)

| Class | Aliases | Header |
|-------|---------|--------|
| `haDelta` | `haD` | hadelta.hpp |

### Oscillator Templates (per MA type)

`MovingAverageOscillator<T>` and `MovingAverageEnvelope<T>` are template classes instantiated for all MA types (SMA, EMA, SMMA, WMA, HMA, DEMA, TEMA, KAMA, DMA, ZLEMA). Each generates aliases like `SMAOscillator`, `EMAEnvelope`, etc.

### Price/Percentage Oscillators

| Class | Aliases | Header |
|-------|---------|--------|
| `PriceOscillator` | `APO` | oscillator.hpp |
| `PercentagePriceOscillator` | `PPO` | oscillator.hpp |
| `PercentagePriceOscillatorShort` | `PPOShort` | oscillator.hpp |
| `CoppockCurve` | `Coppock` | oscillator_extra.hpp |
| `PrettyGoodOscillator` | `PGO` | prettygoodoscillator.hpp |
| `WilliamsR` | `WillR` | williams.hpp |
| `WilliamsAD` | `WAD` | williams.hpp |

---

## Broker

### `stratforge::BackBroker`

Simulated broker for backtesting.

```cpp
class BackBroker {
public:
    // Cash management
    void set_cash(double cash);
    [[nodiscard]] double cash() const noexcept;
    [[nodiscard]] double initial_cash() const noexcept;

    // Portfolio value
    [[nodiscard]] double portfolio_value(const std::vector<DataFeed*>& data_feeds) const noexcept;

    // Commission
    void set_commission(CommissionInfo info);

    // Slippage
    void set_slippage_perc(double perc, bool slip_open = true, bool slip_limit = true,
                           bool slip_match = true, bool slip_out = false);
    void set_slippage_fixed(double fixed, bool slip_open = true, bool slip_limit = true,
                            bool slip_match = true, bool slip_out = false);

    // Position access
    [[nodiscard]] const Position& position(std::size_t data_index = 0) const;

    // Order submission
    [[nodiscard]] std::size_t buy(double size, double price = 0.0, ...);
    [[nodiscard]] std::size_t sell(double size, double price = 0.0, ...);
    [[nodiscard]] std::size_t buy_ext(double size, const OrderParams& params);
    [[nodiscard]] std::size_t sell_ext(double size, const OrderParams& params);
    [[nodiscard]] std::size_t close(std::size_t data_index = 0);
    void cancel_order(std::size_t order_id);

    // Bracket orders
    struct BracketResult { std::size_t main_id; std::size_t stop_id; std::size_t limit_id; };
    [[nodiscard]] BracketResult buy_bracket(double size, double limit_price, double stop_price, ...);
    [[nodiscard]] BracketResult sell_bracket(double size, double limit_price, double stop_price, ...);

    // Extended order parameters
    struct OrderParams {
        OrderType type = OrderType::Market;
        double price = 0.0;
        std::optional<double> stop_price = std::nullopt;
        std::size_t data_index = 0;
        std::optional<std::size_t> valid_until_bar = std::nullopt;
        double trail_amount = 0.0;
        double trail_percent = 0.0;
        std::size_t oco_group_id = 0;
        std::size_t parent_id = 0;
        bool transmit = true;
    };

    // Live trading
    void set_live_connector(MarketConnector* connector);
    void set_live_symbols(std::vector<std::string> symbols);
    [[nodiscard]] bool live_mode() const noexcept;

    // Trade/order access
    [[nodiscard]] const std::vector<Order>& orders() const noexcept;
    [[nodiscard]] const std::vector<Order>& pending_orders() const noexcept;
    [[nodiscard]] const std::vector<std::pair<Trade, double>>& closed_trades() const noexcept;
    [[nodiscard]] const std::vector<std::pair<Trade, double>>& open_trades() const noexcept;
};
```

### Order Types

```cpp
enum class OrderType {
    Market,
    Limit,
    Stop,
    StopLimit,
    StopTrail,
    StopTrailLimit
};

enum class OrderStatus {
    Created,
    Submitted,
    Accepted,
    Partial,
    Completed,
    Canceled,
    Margin,
    Rejected,
    Expired
};
```

---

## Data Feeds

### `stratforge::CsvData`

CSV file data feed.

```cpp
class CsvData : public DataFeed {
public:
    struct Params {
        std::string filename;
        std::unordered_map<std::string, std::size_t> columns = {};
        std::string date_format = "%Y-%m-%d";
        char separator = ',';
        bool has_headers = true;
        std::optional<std::string> fromdate = std::nullopt;
        std::optional<std::string> todate = std::nullopt;
    };

    explicit CsvData(Params params);
    [[nodiscard]] bool load() override;
};
```

### `stratforge::Resampler`

Resample data to different timeframe.

```cpp
class Resampler : public DataFeed {
public:
    Resampler(const DataFeed& source, TimeFrame timeframe, int compression);
};

enum class TimeFrame {
    Ticks, Seconds, Minutes, Hours, Days, Weeks, Months, Years
};
```

---

## Analyzers

### `stratforge::TradeAnalyzer`

Track trade statistics.

```cpp
class TradeAnalyzer : public Analyzer {
public:
    struct Analysis {
        struct Stats { std::size_t total = 0; /* ... */ };
        Stats total, won, lost;
        struct PnL { struct Net { double total = 0.0; /* ... */ } net; /* ... */ } pnl;
    };

    [[nodiscard]] const Analysis& get_analysis() const noexcept;
};
```

### `stratforge::SharpeRatio`

Calculate Sharpe ratio.

```cpp
class SharpeRatio : public Analyzer {
public:
    struct Analysis {
        double sharperatio = 0.0;
    };

    [[nodiscard]] const Analysis& get_analysis() const noexcept;
};
```

### `stratforge::Drawdown`

Track drawdown.

```cpp
class Drawdown : public Analyzer {
public:
    struct Analysis {
        struct Max { double drawdown = 0.0; double moneydown = 0.0; /* ... */ } max;
    };

    [[nodiscard]] const Analysis& get_analysis() const noexcept;
};
```

### `stratforge::Returns`

Calculate returns.

```cpp
class Returns : public Analyzer {
public:
    struct Analysis {
        double rtot = 0.0;   // Total return
        double ravg = 0.0;   // Average return
        double rnorm = 0.0;  // Normalized return
        double rnorm100 = 0.0;  // Normalized return * 100
    };

    [[nodiscard]] const Analysis& get_analysis() const noexcept;
};
```

---

## Observers

### `stratforge::BuySellObserver`

Track buy/sell signals for visualization.

```cpp
class BuySellObserver : public Observer {
public:
    explicit BuySellObserver(bool barplot = false, double bardist = 0.015,
                             std::size_t data_index = 0);
};
```

### `stratforge::ValueObserver`

Track portfolio value over time.

```cpp
class ValueObserver : public Observer {
public:
    explicit ValueObserver(std::size_t data_index = 0);
};
```

---

## Sizers

### `stratforge::PercentSizer`

Size positions as percentage of portfolio value.

```cpp
class PercentSizer : public Sizer {
public:
    explicit PercentSizer(double percents = 20.0);
};
```

### `stratforge::FixedSize`

Fixed position size.

```cpp
class FixedSize : public Sizer {
public:
    explicit FixedSize(double size = 1.0);
};
```

### `stratforge::AllInSizer`

All-in sizing (use all available cash).

```cpp
class AllInSizer : public Sizer {
public:
    AllInSizer() = default;
};
```

---

## Optimizer

### `stratforge::Optimizer`

Parameter optimization with parallel grid search using `std::jthread`.

```cpp
class Optimizer {
public:
    using StrategyFactory = std::function<std::unique_ptr<Strategy>(const ParamMap&)>;
    using ResultExtractor = std::function<OptResult(const Cerebro&, const ParamMap&)>;

    struct Config {
        double cash = 10000.0;
        CommissionInfo commission;
        std::size_t max_threads = 0; // 0 = hardware_concurrency
    };

    explicit Optimizer(Config config = {});

    [[nodiscard]] std::vector<OptResult> run(
        const std::vector<DataFeed*>& data_feeds,
        const StrategyFactory& factory,
        const std::vector<ParamRange>& ranges,
        const ResultExtractor& extractor);
};

struct OptResult {
    ParamMap params;
    double final_value = 0.0;
    double sharpe_ratio = 0.0;
    double max_drawdown = 0.0;
    std::size_t total_trades = 0;
};

struct ParamRange {
    std::string name;
    std::vector<ParamValue> values;
};
```

See `examples/optimizer_example.cpp` for complete usage.

---

## Live Trading

### `stratforge::MarketConnector`

Abstract base for exchange/broker connectivity.

```cpp
class MarketConnector {
public:
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual void submit_order(const Order& order) = 0;
    virtual void cancel_order(OrderId id) = 0;
    virtual void subscribe_market_data(std::string_view symbol) = 0;

    virtual void on_fill(FillCallback callback) = 0;
    virtual void on_market_data(MarketDataCallback callback) = 0;
    virtual void on_state(StateCallback callback) = 0;
};

enum class ConnectorState : std::uint8_t {
    Disconnected, Connecting, Connected, Reconnecting, Error
};

struct MarketDataSnapshot {
    std::string symbol;
    double bid_price, ask_price, last_price;
    double bid_size, ask_size, last_size;
    std::size_t data_index = 0;
};
```

Implemented adapters: `NexuFixConnector` (Forex/TradFi). Remaining adapters (CCAPI, CTP, XTP, IB) implement the same interface.

---

## Version & Compatibility

**API Version**: Phase 10
**Freeze Date**: 2026-04-21
**Updated**: 2026-04-24
**C++ Standard**: C++23 (ISO/IEC 14882:2024)

**Stability Guarantee**:
- Public API frozen for LLM code generation (/005)
- Breaking changes require major version bump
- Additions (new indicators, features) are backwards-compatible

**Usage**:
```cpp
#include <stratforge/stratforge.hpp>  // All-in-one header
// Or include specific headers:
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/strategy.hpp>
#include <stratforge/indicators/sma.hpp>
// etc.
```
