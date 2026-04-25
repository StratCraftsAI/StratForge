# TICKET_SF001: Comprehensive Test Coverage Initiative

**Status**: Design
**Priority**: High
**Category**: Quality / Testing Infrastructure
**Reference**: [Why AI Code Needs the Same Rigor](https://dev.to/whetlan/why-ai-code-needs-the-same-rigor-we-should-have-been-using-all-along-1kk4)

---

## Motivation

StratForge is an open-source backtesting engine. Incorrect indicator computation, silent broker state corruption, or subtle data feed bugs can produce misleading backtest results and real financial damage when strategies are deployed. Every public header should have corresponding test coverage.

AI-assisted codebases need the same (or more) rigor as hand-written code. Verification that what got built is actually what was intended requires unit tests, golden master tests, and CI gates.

---

## Current State

### Well-Tested (have dedicated test files)

| Module | Test File | Approx Cases |
|--------|-----------|-------------|
| Indicators (basic ops) | `test_indicator_basicops.cpp` | 20+ |
| Indicators (golden master) | `test_indicator_golden.cpp` | 30+ |
| Indicators (ATR) | `test_indicator_atr.cpp` | 5+ |
| Indicators (Phase 8 series) | `test_indicator_phase8*.cpp` (5 files) | 50+ |
| Indicators (TICKET_011) | `test_indicator_ticket011.cpp` | 10+ |
| Indicators (candlestick) | `test_candlestick.cpp` | 10+ |
| Indicators (TA-Lib cross) | `test_talib_cross_validation.cpp` | 20+ |
| Broker | `test_broker.cpp` | 10+ |
| Commission | `test_commission.cpp` | 5+ |
| Order | `test_order.cpp` | 10+ |
| Sizer | `test_sizer.cpp` | 5+ |
| Data Feed | `test_data_feed.cpp` | 10+ |
| Line | `test_line.cpp` | 10+ |
| Strategy | `test_strategy.cpp` | 10+ |
| Cerebro | `test_cerebro.cpp` | 5+ |
| Optimizer | `test_optstrategy.cpp` | 5+ |
| Replay | `test_replay_golden.cpp` | 5+ |
| Resample | `test_resample_golden.cpp` | 5+ |
| E2E Integration | `test_e2e_integration.cpp` | 10+ |
| Extended Metrics | `test_extended_metrics.cpp` | 5+ |
| Analytics Golden | `test_analytics_golden.cpp` | 5+ |
| Multi-data Golden | `test_multidata_golden.cpp` | 5+ |
| Phase 7 Hardening | `test_phase7_hardening.cpp` | 10+ |
| Registry Compile | `test_registry_compile.cpp` | 5+ |
| Advanced Orders | `test_advanced_orders.cpp` | 5+ |

### No Dedicated Coverage

| Category | Headers | Count |
|----------|---------|-------|
| Analyzers | `analyzer.hpp`, `returns.hpp`, `sharpe_ratio.hpp`, `trade_analyzer.hpp` | 4 |
| Broker | `position.hpp`, `trade.hpp` | 2 |
| Core | `line_series.hpp`, `params.hpp`, `period_validate.hpp` | 3 |
| Data | `timeframe.hpp` | 1 |
| Engine | `optimizer.hpp` (partial via `test_optstrategy.cpp`) | 1 |
| Observers | `observer.hpp`, `cash_value.hpp`, `value.hpp`, `buy_sell.hpp` | 4 |
| Strategy | `signal.hpp` | 1 |

---

## Implementation Plan

### Phase 1: Core Infrastructure Coverage (High Priority)

Components on the backtest hot path where bugs cause incorrect P&L or state corruption.

#### 1A. Position and Trade Tracking
**File**: new `test_position.cpp`
**Target**: `broker/position.hpp`, `broker/trade.hpp`
**Test Cases**:
- Open long position, verify size/price
- Open short position
- Partial close
- Full close with P&L calculation
- Multiple entries (averaging)
- Position reversal (long to short)
- Trade record: entry/exit prices, commission, P&L

#### 1B. Analyzers
**File**: new `test_analyzers.cpp`
**Target**: `analyzers/analyzer.hpp`, `returns.hpp`, `sharpe_ratio.hpp`, `trade_analyzer.hpp`
**Test Cases**:
- Returns analyzer: per-bar returns calculation
- Sharpe ratio: known input/output pairs
- Trade analyzer: win/loss count, average P&L, max drawdown
- Analyzer lifecycle: attach to cerebro, run, extract results
- Empty strategy (no trades) edge case

#### 1C. Observers
**File**: new `test_observers.cpp`
**Target**: `observers/observer.hpp`, `cash_value.hpp`, `value.hpp`, `buy_sell.hpp`
**Test Cases**:
- Cash observer tracks cash after each bar
- Value observer tracks portfolio value
- Buy/sell observer records order events
- Observer data accessible after run

#### 1D. Signal Strategy
**File**: extend `test_strategy.cpp`
**Target**: `strategy/signal.hpp`
**Test Cases**:
- Signal-based entry/exit
- Signal with period parameter
- Multiple signals combined

---

### Phase 2: Data Layer Coverage (Medium Priority)

#### 2A. Timeframe
**File**: new `test_timeframe.cpp`
**Target**: `data/timeframe.hpp`
**Test Cases**:
- Timeframe comparison operators
- Timeframe compression ratios
- Minutes, days, weeks, months

#### 2B. Core Utilities
**File**: extend `test_line.cpp`
**Target**: `core/line_series.hpp`, `core/params.hpp`, `core/period_validate.hpp`
**Test Cases**:
- LineSeries multi-line access
- Params: set/get, type safety
- Period validation: boundary values, zero, negative

---

### Phase 3: Indicator Gap Coverage (Lower Priority)

Indicators are well-tested via golden master, but some individual headers lack edge-case unit tests.

#### 3A. Indicator Edge Cases
**File**: extend existing indicator tests
**Test Cases**:
- Period = 1 (minimum)
- Period > data length
- NaN/Inf input handling
- Single-bar data
- All-same-value data (zero variance)

---

## Success Criteria

| Metric | Current | Target |
|--------|---------|--------|
| Header coverage (non-indicator) | ~60% (18/30) | 90%+ (27/30) |
| Header coverage (indicators) | ~95% (via golden) | 95%+ |
| Approx test cases | 250+ | 400+ |
| Core path components tested | Partial | 100% |
| CI coverage gate | None | 80% line coverage |

---

## New Test Files Summary

| File | Phase | Targets |
|------|-------|---------|
| `test_position.cpp` | 1A | position.hpp, trade.hpp |
| `test_analyzers.cpp` | 1B | analyzer.hpp, returns.hpp, sharpe_ratio.hpp, trade_analyzer.hpp |
| `test_observers.cpp` | 1C | observer.hpp, cash_value.hpp, value.hpp, buy_sell.hpp |
| `test_timeframe.cpp` | 2A | timeframe.hpp |

Existing files to extend: `test_strategy.cpp`, `test_line.cpp`, indicator test files

---

## References

- [Why AI Code Needs the Same Rigor](https://dev.to/whetlan/why-ai-code-needs-the-same-rigor-we-should-have-been-using-all-along-1kk4)
- Adapted from NexusFIX TICKET_462 convention
