# Alpha158 and TA-Lib Candlestick Compatibility

StratForge exposes two related but semantically different OHLC feature surfaces.
They must not be interpreted as the same kind of signal.

## Qlib Alpha158 continuous factors

Alpha158 contains 158 continuous numeric factors. StratForge provides public
current-bar indicators for Qlib's nine K-bar shape factors: `KMID`, `KLEN`,
`KMID2`, `KUP`, `KUP2`, `KLOW`, `KLOW2`, `KSFT`, and `KSFT2`. Include
`<stratforge/indicators/qlib_kbar.hpp>` or the aggregate
`<stratforge/stratforge.hpp>` header.

```cpp
stratforge::KMID body_to_open{
    data.open(), data.high(), data.low(), data.close()};
body_to_open.next();
double ratio = body_to_open.line()[0];
```

These indicators return dimensionless `double` ratios, use only the current
bar, and have `minimum_period() == 1`. The Alpha158 executor records that as
`warmup_bars == 0` because no preceding bar is skipped. Invalid or non-finite
OHLC input, invalid OHLC ordering, and `open == 0` produce `NaN`. The four
range-normalized fields use Qlib's exact `high - low + 1e-12` denominator.

The other Alpha158 IDs remain parameterized routes through shared indicators
or expression primitives. They are logical catalog factors, not 149 additional
C++ classes. The checked Phase 8 report records the route and parity status for
all 158 IDs.

## TA-Lib candlestick pattern signals

The 61 TA-Lib `CDL*` indicators are discrete pattern recognizers. Include
`<stratforge/indicators/talib_compat.hpp>` to use exact TA-Lib names, or
`<stratforge/indicators/candlestick.hpp>` to use the native class names.

```cpp
stratforge::talib::CDLENGULFING engulfing{
    data.open(), data.high(), data.low(), data.close()};
engulfing.next();
double signal = engulfing.line()[0];
```

Their output is an encoded signal, not a ratio: `0` means no match and the sign
indicates bullish or bearish direction. Standard matches use magnitude `100`;
some TA-Lib patterns have documented exceptional strengths such as Hikkake
confirmation at magnitude `200`. Readiness varies by pattern because TA-Lib's
adaptive candle settings use prior bars. Configurable penetration parameters
retain their TA-Lib defaults.

## Coverage contract

`tools/golden_extract/output/sf011/coverage_report.json` is the checked,
machine-readable integration inventory. Its two namespaces prevent collisions:
`alpha158:<factor_id>` for 158 continuous factors and
`talib_candlestick:<CDL name>` for 61 discrete patterns. Every row records
discoverability, executability, category, and pinned-oracle parity.

Regenerate or verify it with:

```bash
python3 tools/golden_extract/generate_sf011_coverage.py
python3 tools/golden_extract/generate_sf011_coverage.py --check
```

The registry compile generator produces one compile-and-run test per public
registry row; its `--check` mode makes missing or stale generated cases a CI
failure.
