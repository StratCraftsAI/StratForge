# Indicator History Contract

StratForge exposes a versioned, machine-consumable resolver for the earliest bar
on which a public indicator output accessor is calculation-ready:

```cpp
#include <stratforge/indicators/history_contract.hpp>

using namespace stratforge::indicator_history;

Parameters parameters{
    {"fast_period", std::uint64_t{12}},
    {"slow_period", std::uint64_t{26}},
    {"signal_period", std::uint64_t{9}},
};

auto period = effective_minimum_period("MACD", parameters, "signal");
```

The result above is 34. `MACD.macd()` is ready after 26 bars, while `signal()`
and `histogram()` are ready after 34 bars. Indicator aliases, parameter types,
defaults, and public accessors are available through `descriptors()`.

For a historical read `line()[-N]`, calculate the required warmup as:

```text
lookback_depth = N
required_warmup = accessor minimum period + lookback_depth
```

`required_warmup()` performs the checked addition. Positive C++ offsets are
lookahead and must be rejected by the source validator. A dynamic offset must
also be rejected unless the validator can prove a finite, non-positive bound.

Generated strategies declare the resulting warmup through their public strategy
base. During `prenext()`, the base advances indicators without invoking business
decision methods. On the first ready bar and later bars, it advances each
indicator before invoking business logic.

Multi-feed generated strategies override
`indicator_history_requirements()` and return one `{feed_index, bars}` entry for
each owning feed. They override `update_indicators(feed_index)` to advance only
the members bound to that feed. Engines own readiness: business logic runs on
the feed-0 execution clock only after every declared feed requirement is met.

## Public live execution core

The public live surface consists of `MarketConnector`, `LiveDataFeed`, and
`LiveEngine`, plus the existing Broker APIs used for order submission and fill
recording. It has no dependency on SQLite, runtime compilation, shared-library
loading, IPC, or vendor-specific connectors. Those facilities remain owning-
application concerns; an owning layer can pass an already-created strategy to
`LiveEngine` or `replace_strategy()`.

Connector market, fill, and state callbacks enter `LiveEngine` through one
serialized callback gate. `stop()` unregisters all callbacks and waits for an
in-flight callback to leave that gate. Destruction invalidates the gate before
engine state disappears, so a callback retained by a connector becomes a safe
no-op. Feed 0 is the live business-evaluation clock; secondary-feed callbacks
advance their declared indicators without invoking business decisions.

The contract version is available as `contract_version`. Resolver failures use
stable `ErrorCode` values and never return a guessed period.
