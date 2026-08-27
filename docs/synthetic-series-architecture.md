# Synthetic Price Series Architecture Decision

Status: **WP9 approved for architecture; production implementation deferred**
Ticket: `TICKET_SF012`
Date: 2026-08-27

## Decision

Renko, Kagi, Three-Line Break, and Point & Figure are variable-cardinality
series transformations, not indicators. They must not implement `Indicator`,
publish one value per source bar, enter the indicator registry, or masquerade
as exchange OHLC data. A future public subsystem will own synthetic elements
and expose an operation result equivalent to:

```cpp
ChangeSet append(const Bar& source);       // zero-to-many inserts
ChangeSet replace_last(const Bar& source); // erase/replace the last source contribution
Snapshot snapshot() const;
void restore(const Snapshot&);
```

`ChangeSet` must own, or remain valid until the next mutation, and describe
both removed IDs and inserted elements. A bare `span` of insertions is
insufficient for live replacement because consumers also need retractions.
Only the most recent source bar may be replaced, and its timestamp must remain
unchanged so the operation cannot silently become a different source event.
Older corrections require a checkpoint restore followed by deterministic
replay.

Every synthetic element has a stable ID `(source_sequence, source_ordinal)`.
Replacement retains the source sequence and therefore retains IDs for
corresponding ordinals; extra old ordinals are retracted and extra new ordinals
are inserted. IDs, rather than timestamps, are the identity key.

All elements caused by one source bar receive that bar's close timestamp.
`source_ordinal` provides a deterministic total order when a price jump emits
multiple elements at the same timestamp. Synthetic timestamps are never
invented between observations.

## Renko prototype contract

The test-only WP9 prototype freezes the minimum semantics needed to validate
the architecture:

- positive, finite, fixed brick size;
- the first valid source close establishes the anchor and emits nothing;
- continuation requires one brick and reversal requires two bricks;
- a jump emits every completed brick, in price-path order;
- equality at a continuation or reversal threshold emits;
- finite, correctly ordered OHLC is required; an invalid bar is rejected
  atomically and does not consume a source sequence;
- prices may be negative, and session gaps receive no special interpolation;
- volume is not copied because a source bar's volume cannot be causally or
  uniquely allocated among zero or multiple synthetic elements; and
- the newest append is replaceable. The transformer stores its pre-append
  checkpoint so replacement is equivalent to restore-and-replay; a replacement
  with a different timestamp is rejected atomically.

A reversal brick spans the two-brick threshold. For an uptrend ending at `P`,
the first down brick is represented as `open=P-brick_size` and
`close=P-2*brick_size`; the skipped interval encodes the reversal threshold.
The upward rule is symmetric. Direction is `-1` or `+1`.

The prototype snapshot includes a schema version, configuration, current
anchor/direction, next source sequence, and the complete pre-last-source
checkpoint needed by `replace_last`. Restore validates the version and
configuration. Production persistence must define an endian-independent wire
format and exact floating-point encoding; the in-memory prototype snapshot is
not that wire format.

## Evaluation of the four representations

| Representation | Variable output and reversal state | Forming-bar replacement | Decision |
|---|---|---|---|
| Renko | Zero-to-many bricks; fixed or sampled-ATR size; explicit reversal threshold | Retract last source contribution and replay from checkpoint | Test-only fixed-size prototype approved |
| Kagi | Zero-to-many turns/segments; reversal amount plus yin/yang shoulder/waist state | Same checkpoint/change-set contract, with segment updates as well as inserts | Production algorithm deferred |
| Three-Line Break | Zero-or-one line normally, but replacement can retract a line; state depends on prior line extremes | Same checkpoint/change-set contract | Production algorithm deferred |
| Point & Figure | Zero-to-many boxes/columns; box size and multi-box reversal | Same checkpoint/change-set contract; column mutation must be explicit | Production algorithm deferred |

## ATR sizing, sessions, and live forming bars

Fixed sizing is the only approved prototype mode. A future ATR-sized mode must
record the size on every emitted element and sample ATR strictly before the
source price being transformed (or document another reviewed causal rule).
Recomputing old brick sizes from a newer ATR is forbidden.

Session gaps and large price jumps follow the same threshold loop and may emit
multiple elements. A future API must accept an explicit session policy
(`continuous` or `reset`); no calendar rule may be hidden in a transformer.
Negative prices are valid. Non-finite or invalid OHLC is rejected atomically.

Live feeds may repeatedly replace only their newest source bar. Consumers
apply removals before inserts from the returned `ChangeSet`. Finalizing a bar
is an ownership concern of the feed, not a synthetic-series heuristic.

## Persistence and determinism gate

Production work may start only after it provides:

1. a versioned, endian-independent snapshot format with validated numeric
   bounds and configuration;
2. deterministic append, replace-last, restore, and historical replay;
3. explicit change sets for inserts, removals, and any in-place element
   update;
4. conformance fixtures for equality, gaps, negative prices, zero/many
   emission, reversals, invalid input, and restoration; and
5. an engine-owned synthetic-series/feed integration that preserves element
   IDs and does not coerce variable output into `LineSeries` history.

The current `DataFeed`/indicator path assumes one clock advance per source bar
and cannot represent retractions or multiple same-source outputs without
information loss. Therefore all four production representations are
explicitly deferred to a follow-up engine ticket. WP9 approves this contract
and the Renko feasibility prototype only; it approves no public type.
