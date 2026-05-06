# Benchmark Suite

Performance benchmarks for the engine with proper methodology:
warmup phases, I/O-isolated timing, per-bar micro-benchmarks, allocation
counting, and machine-parseable JSON output.

## Quick Start

```bash
# Build all benchmarks (Release mode required for meaningful results)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target gen_bench_data indicator_benchmarks \
    strategy_benchmarks optimization_benchmarks memory_benchmarks \
    csv_parse_benchmark line_series_benchmark order_routing_benchmark \
    cold_start_benchmark

# Generate synthetic 100K-bar dataset
./build/bin/benchmarks/gen_bench_data

# Run all benchmarks
./build/bin/benchmarks/indicator_benchmarks
./build/bin/benchmarks/strategy_benchmarks
./build/bin/benchmarks/optimization_benchmarks
./build/bin/benchmarks/memory_benchmarks
./build/bin/benchmarks/csv_parse_benchmark
./build/bin/benchmarks/line_series_benchmark
./build/bin/benchmarks/order_routing_benchmark
./build/bin/benchmarks/cold_start_benchmark
```

If you only want one first signal, run `indicator_benchmarks` and `memory_benchmarks`. Together they tell you whether latency and allocation behavior moved in the right direction.

## Benchmark Executables

### `gen_bench_data`

Generates a synthetic 100K-bar OHLCV CSV using geometric Brownian motion.
Deterministic (fixed seed=42) for reproducible benchmarks.

```bash
# Default: 100K bars
./build/bin/benchmarks/gen_bench_data

# Custom bar count and output directory
./build/bin/benchmarks/gen_bench_data 500000 build/bench_data
```

### `indicator_benchmarks`

Benchmarks 11 indicators with three measurement methods:

1. **Whole-run**: Total time for all bars (50 iterations, 3 warmup)
2. **Per-bar micro-benchmark**: Individual `next()` call timing (50-bar warmup discarded)
3. **Allocation count**: Heap allocations in hot path (60-bar warmup discarded)

Tests on both 512-bar golden extract and 100K-bar synthetic datasets.

### `strategy_benchmarks`

Benchmarks composite strategy (5 indicators + broker):

- Cold/warm startup comparison
- Whole-run timing (I/O isolated — data loaded once, cloned per iteration)
- Full backtest wall-clock (512 and 100K bars)
- Strategy-level allocation audit

### `optimization_benchmarks`

Benchmarks parallel parameter grid optimization (4 threads):

- 2×2, 3×3, 5×5 grids
- Reports combos/sec throughput

### `memory_benchmarks`

Dedicated allocation audit for all indicators and strategy:

- Counts heap allocations via global `operator new` override
- 60-bar warmup to exclude initial vector growth
- Verifies zero-alloc hot-path target per design spec

### `csv_parse_benchmark`

Benchmarks CSV data feed ingestion throughput:

- Full disk I/O + parse at 1K, 10K, 100K rows
- In-memory tokenization throughput (MB/s)
- Per-row amortized latency
- Real dataset (golden extract) throughput

### `line_series_benchmark`

Benchmarks the core `Line<T>` and `LineSeries<T>` data structures:

- `Line::forward` append at 512, 10K, 100K elements (with/without reserve)
- Random lookback access (`operator[]` with negative offsets)
- Sequential access (simulating indicator iteration)
- `LineSeries` OHLCV multi-line access patterns
- Allocation audit (reserved vs unreserved growth)

### `order_routing_benchmark`

Benchmarks broker order creation → fill pipeline:

- Market order flood (order every bar)
- Limit order strategy (complex fill logic)
- Cold/warm comparison
- Allocation audit for order lifecycle

### `cold_start_benchmark`

Measures first-execution penalty vs steady-state:

- CSV load cold/warm ratio
- Indicator pipeline cold/warm ratio
- Full backtest cold/warm ratio
- Summary table of all cold/warm ratios

## How to Read the Results

- `avg` is useful for broad trend tracking
- `P50` is the main target for typical hot-path latency
- `P99` and `P999` catch tail regressions hidden by averages
- Allocation counts matter even when latency looks stable, because hidden heap traffic usually reappears later as jitter

Prefer comparing like-for-like runs:

- Same compiler and standard library
- Same build type
- Same benchmark dataset
- Same CPU scaling and affinity conditions

## Methodology

### Timing

- `std::chrono::steady_clock` for nanosecond-precision wall-clock
- **Warmup phase**: 3 whole-run warmup iterations discarded; 50-bar per-bar warmup
- **I/O isolation**: Data loaded once before timing loop; `clone()` + `load()` used per iteration (no disk I/O in hot path)
- Metrics: avg, P50, P99, P999, min, max

### Allocation Counting

- Global `operator new`/`operator delete` overrides with thread-local counter
- `AllocationCounter` RAII scope tracks allocations in marked regions
- 60-bar warmup absorbs initial `Line<double>` vector growth

### Output

- Human-readable to stdout
- JSON reports to `build/bench_results/` for regression tracking

## Baseline Results

See [BASELINE.md](BASELINE.md) for the full baseline report.

**Highlights (512-bar, per-bar P50)**:
- EMA: 16 ns/bar (58M bars/sec)
- SMA: 29 ns/bar (33M bars/sec)
- MACD: 22 ns/bar (42M bars/sec)
- Ichimoku: 67 ns/bar (15M bars/sec) — only zero-alloc indicator
- All indicators meet <100ns P50 target

These figures are repository baselines, not universal promises. Re-run them on your own hardware before drawing conclusions about cross-machine comparisons.

## Performance Targets

| Target | Metric | Status |
|--------|--------|--------|
| Per-indicator computation | <100 ns/bar P50 | **MET** (17-67 ns) |
| Per-bar composite strategy | <100 ns/bar | NOT MET (~1.2μs, includes broker) |
| Hot-path allocations | 0 allocations | NOT MET (vector growth — see BASELINE.md) |
| Parallel optimization | Linear speedup | **MET** (~4x on 4 threads) |

## Performance Regression Tracking

Automated regression detection via `tools/bench_regression.py`. Compares current
`build/bench_results/*.json` against stored baselines in `benchmarks/baselines/`.

```bash
# Seed baselines from current results
python3 tools/bench_regression.py --update-baseline

# Compare current results against baselines (exit 1 on regression)
python3 tools/bench_regression.py

# Run benchmarks first, then compare
python3 tools/bench_regression.py --run

# Custom thresholds
python3 tools/bench_regression.py --latency-threshold 0.20 --p99-threshold 0.25

# Single suite
python3 tools/bench_regression.py --suite indicator_benchmarks
```

**Default thresholds**:

| Metric | Threshold | Direction |
|--------|-----------|-----------|
| `p50_ns` | +15% | Higher is worse |
| `p99_ns` | +20% | Higher is worse |
| `avg_ns` | +15% | Higher is worse |
| `alloc_count` | +0 (exact) | Any increase is regression |

## Datasets

| Dataset | Bars | Source |
|---------|------|--------|
| 512-bar daily | 512 | `tools/golden_extract/datas/2005-2006-day-001.txt` |
| 100K synthetic | 100,000 | `build/bench_data/synthetic_100k.csv` (generated) |

## Related Docs

- `examples/README.md` for end-to-end strategy behavior
- `docs/performance_results.md` for historical benchmark discussion
- `tools/golden_extract/datas/README.md` for sample dataset details
