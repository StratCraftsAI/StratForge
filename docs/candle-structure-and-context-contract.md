# Candle Structure and Context Contract

Status: frozen for TICKET_SF012 WP1 (2026-08-27)

This document fixes the public semantics required by WP2-WP8. Later work may
change implementation details, but changing a formula, boundary, output code,
or readiness rule below requires an explicit contract revision and new golden
fixtures.

## Common conventions

- A bar at index `t` is `(O_t, H_t, L_t, C_t)` and is valid only when all four
  values are finite and `L_t <= min(O_t, C_t) <= max(O_t, C_t) <= H_t`.
- A valid volume, signal, trend, or level sample is finite. Volume must also be
  non-negative.
- `NaN` means unavailable or invalid continuous output. Discrete predicates
  emit `0` while unavailable or invalid; lifecycle state emits `invalidated`
  when an already-pending event is made unusable by invalid input.
- Invalid samples are still chronological bars. They emit the values above,
  clear structure state that could span the invalid bar, and are not inserted
  into rolling measurement windows. A rolling output becomes ready again only
  after the required number of consecutive valid samples. ATR behavior itself
  remains that of the public `ATR` type; a normalized geometry output is valid
  only when both its geometry and the corresponding ATR sample are finite.
- Every period parameter is a positive number of bars. Construction with zero
  is invalid and must throw `std::invalid_argument`; the new public APIs must
  not silently clamp zero to one.
- `minimum_period()` is the number of chronological source bars needed for the
  first result on an all-valid stream. Runtime invalidity can temporarily
  produce unavailable output after that static minimum.

## Names and public types

The public namespace is `stratforge`. Registry names and C++ class names are
identical unless an alias is listed here.

| Type | Parameters | Primary output | Additional accessors / aliases |
|---|---|---|---|
| `CandleGeometry` | OHLC | `body` | `range`, `upper_wick`, `lower_wick`, `signed_gap`, `gap`; `gap` is absolute |
| `NormalizedCandleGeometry` | OHLC, `atr_period=14` | `body` | same accessors as `CandleGeometry`, each ATR-normalized |
| `CandlePercentile` | OHLC, measurement, `period=50` | selected percentile | measurement is `body`, `range`, `upper_wick`, `lower_wick`, or `gap` |
| `OutsideBar` | high, low, `boundary=inclusive` | `0` or `1` | `strict` is an explicit boundary option, not another class |
| `InsideBarDepth` | high, low, `boundary=inclusive` | `depth` | `mother_range`, `breakout` |
| `NarrowRange` | OHLC, `period` | `0` or `1` | `NR4` and `NR7` are aliases/presets for periods 4 and 7 |
| `WideRange` | OHLC, `period` | `0` or `1` | none |
| `PatternConfirmation` | signal and supplied thresholds, `expiry_bars`, overlap policy | lifecycle state | `event_id`, `age`, `direction` |
| `TrendContext` | signal, supplied trend line | alignment | `direction` |
| `VolumeConfirmation` | signal, volume, supplied baseline | ratio | `confirmed` |
| `SupportResistanceContext` | signal, price, supplied support/resistance lines | signed distance | `support_distance`, `resistance_distance` |
| `CandleQualityScore` | supplied component lines and weights | weighted score | `geometry`, `trend`, `confirmation`, `volume` |

Enums used in constructors are scoped public enums: `BoundaryMode { strict,
inclusive }`, `CandleMeasurement`, `ConfirmationState`, and `OverlapPolicy {
ignore, replace, queue_one }`. The default overlap policy is `replace`.

## Geometry and ATR normalization

For a valid current bar:

```text
body        = abs(C_t - O_t)
range       = H_t - L_t
upper_wick  = H_t - max(O_t, C_t)
lower_wick  = min(O_t, C_t) - L_t
signed_gap  = O_t - C_(t-1)
gap         = abs(signed_gap)
```

The four same-bar measurements are available on the first bar. Both gap
outputs require a valid immediately preceding bar; an invalid predecessor
cannot be skipped.

`NormalizedCandleGeometry` owns one public-contract-equivalent `ATR(high, low,
close, atr_period)` instance and divides every accessor by that one current ATR
sample. It does not calculate a second range average. If ATR is zero (including
`-0.0`), non-finite, or not ready, every normalized accessor emits `NaN`.
Near-zero finite positive ATR is not clamped. The first ready normalized bar is
bar index `atr_period`, matching `ATR::minimum_period() == atr_period + 1`.

## Percentile and extrema policies

`CandlePercentile` ranks the non-negative magnitude selected by
`CandleMeasurement`; `gap` means absolute gap. Its trailing window contains the
current measurement and the preceding `period - 1` measurements. Its value is:

```text
count(window value < current value) / period
```

Thus ties do not count, the minimum and an all-equal window produce `0`, and
the maximum possible result is `(period - 1) / period`. This exactly matches
the existing `PercentRank` tie and current-window rule. A gap percentile needs
one predecessor plus `period` valid gap observations.

`NarrowRange(period)` and `WideRange(period)` compare `H-L` over a trailing
window that includes the current bar. Narrow range uses `current < every prior
range`; wide range uses `current > every prior range`. Consequently a tie,
including an all-equal window, emits `0` for both. This deliberate strict tie
policy is independent of `BoundaryMode`. `period=1` makes both predicates true
on every valid bar.

## Containment and mother-bar state

For `OutsideBar`, inclusive mode is:

```text
H_t >= H_(t-1) && L_t <= L_(t-1)
```

and strict mode replaces both operators with strict comparisons. Both sides
must satisfy the selected mode; equality on only one side therefore counts
only in inclusive mode.

`InsideBarDepth` compares descendants with one active mother bar `(H_m, L_m)`.
Inclusive containment, the default, is `H_t <= H_m && L_t >= L_m`; strict
containment uses `<` and `>`. The first valid bar starts a mother and emits
depth `0`, its range, and breakout `0`. A contained bar increments depth and
keeps the same mother, even if it is also contained by the immediately prior
bar. Nested bars never replace or shrink the mother.

For a non-contained valid bar, breakout is `+1` when it crosses above the
mother high only, `-1` when it crosses below the mother low only, and `0` when
it crosses neither or both sides. Boundary contact is a crossing in strict
mode and is not a crossing in inclusive mode. After emitting that result, the
current bar becomes the new mother, `depth` resets to `0`, and
`mother_range` immediately reports the new mother's range. An invalid bar
clears the mother and emits `depth=0`, `mother_range=NaN`, `breakout=0`; the
next valid bar starts a new mother.

## Generic context and lifecycle

A signal sample is directional: positive is bullish, negative is bearish, and
zero means no event. Magnitude has no lifecycle meaning. Each accepted signal
gets a monotonically increasing, non-reused `event_id` and direction `+1` or
`-1`. Public lifecycle codes are stable:

| State | Code | Meaning |
|---|---:|---|
| `idle` | 0 | no event is active and no transition occurred this bar |
| `pending` | 1 | an accepted event is awaiting an outcome |
| `confirmed` | 2 | terminal transition occurred on this bar |
| `invalidated` | -1 | terminal transition occurred on this bar |
| `expired` | -2 | terminal transition occurred on this bar |

Terminal states are emitted for one bar and return to `idle` on the next bar,
unless a queued event starts then. `age` is zero on the signal bar, one on the
next bar, and remains the terminal causal age on a transition bar. An event
with `expiry_bars=N` may transition on ages `0..N`; it expires at the end of
age `N` only if neither price transition happened. Thus same-bar confirmation
and invalidation are permitted and are evaluated before expiry.

Confirmation and invalidation thresholds are supplied, finite price lines;
there is no hidden ATR, pivot, or candle-pattern calculation. For bullish
events, confirmation is `price >= confirmation_threshold` and invalidation is
`price <= invalidation_threshold`; bearish inequalities are reversed. If both
are true on one bar, invalidation wins. Missing/non-finite required input while
pending also invalidates the event. Threshold equality counts.

When a non-zero signal arrives while an event is pending, `ignore` discards
it, `replace` invalidates the old event and starts the new event on the same
bar (the public state is the new event's resulting state), and `queue_one`
stores the first waiting signal and ignores further signals until that slot is
consumed. A queued event starts on the bar after the active event's terminal
transition, using that later bar's supplied thresholds; it cannot inspect the
terminal bar retroactively.

Context wrappers consume arbitrary aligned input lines. They neither advance
nor resample those lines. At source time `T`, only the latest explicitly
aligned context sample whose timestamp is `<= T` may be presented by engine
wiring; a future sample is never visible. Repeated slower-clock context values
are allowed and do not create new signal events unless the signal line itself
has a new non-zero sample.

`TrendContext` outputs `+1` when signal direction and the sign of the supplied
trend line agree, `-1` when they disagree, and `0` for no signal or a zero
trend. `VolumeConfirmation` outputs `volume / baseline`; a zero, negative, or
non-finite baseline yields `NaN`, and `confirmed` is `1` when the finite ratio
is at least its configured threshold. `SupportResistanceContext` uses
`price-level`, so values above a level are positive; each supplied level has
its own accessor and missing lines are not synthesized.

`CandleQualityScore` accepts four component lines normalized by their producer
to `[0,1]` and four finite, non-negative weights. It exposes the clamped input
components unchanged and computes `sum(weight*component)/sum(weight)`.
Zero total weight or any unavailable required component produces `NaN`; there
is no implicit reweighting. No component is calculated internally.

## Readiness and history formulas

Let `A = atr_period`, `P = rolling period`, `E = expiry_bars`, and let `M(x)`
be the upstream `minimum_period()` of supplied line `x`. All values below are
bar counts, not zero-based indexes.

| Type / accessor | `minimum_period()` or accessor minimum |
|---|---:|
| `CandleGeometry.body/range/upper_wick/lower_wick` | 1 |
| `CandleGeometry.signed_gap/gap` | 2 |
| `CandleGeometry` aggregate | 2 |
| every `NormalizedCandleGeometry` accessor | `A + 1` |
| `CandlePercentile` same-bar measurement | `P` |
| `CandlePercentile(gap)` | `P + 1` |
| `OutsideBar` | 2 |
| every `InsideBarDepth` accessor | 1 |
| `NarrowRange`, `WideRange` | `P` |
| `PatternConfirmation.state/event_id/age/direction` | `max(M(signal), M(price), M(confirm), M(invalidate))` |
| `TrendContext` | `max(M(signal), M(trend))` |
| `VolumeConfirmation.ratio/confirmed` | `max(M(signal), M(volume), M(baseline))` |
| support/resistance distance accessor | `max(M(signal), M(price), M(its level))` |
| every `CandleQualityScore` accessor | maximum `M(component)` among configured components |

`E` does not increase static readiness: it controls when a later lifecycle
transition can occur, not when state can first be evaluated. Likewise,
mother-bar breakout can first be non-zero on bar two, but all
`InsideBarDepth` accessors are defined on bar one.

Registry history entries must encode these formulas directly. Multi-output
types use named accessor history rather than inflating all accessors to the
slowest one; in particular, first-bar candle geometry must not inherit the gap
accessor's two-bar requirement.

## Registry design constraints

- One registry row owns each implementation. `NR4` and `NR7` bind the
  `NarrowRange` period and must not generate C++ classes or separate algorithms.
- `BoundaryMode` is serialized as `inclusive` or `strict`; inclusive is the
  default for both containment types.
- `CandleMeasurement` is serialized using the accessor spellings in the table
  above. Signed gap is intentionally not percentile-ranked.
- Context registry constructors identify supplied lines explicitly. They must
  not select an implicit moving average, volume average, pivot algorithm, or
  TA-Lib recognizer.
- Discrete structure outputs use `0/1`; direction exists only on `breakout` and
  context outputs documented above. No SF012 type aliases an existing `CDL*`,
  Qlib K-bar, `CdlInside`, `CdlZ`, Heikin-Ashi, or QStick implementation.

## Required contract fixtures

Implementation tests must pin equality on each high/low and threshold
boundary, equal NR/WR ranges, flat and near-flat ATR, invalid OHLC and recovery,
gap recovery, nested inside bars, two-sided mother breakout, same-bar lifecycle
outcomes, every overlap policy, sparse aligned context, and component/weight
failure cases. Tests compare every bar, including warmup and terminal pulses.
