#!/usr/bin/env python3
"""Generate and byte-check the frozen TICKET_SF012 WP0 inventory."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

from common import write_json

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path(__file__).resolve().parent / "output/sf012/inventory.json"
SF011 = Path(__file__).resolve().parent / "output/sf011/talib_candles_inventory.json"

SOURCES = {
    "stratforge": ("StratForge", "TICKET_SF011 baseline", "Apache-2.0", "https://github.com/stratcraft-ai/StratForge", "existing implementation/specification owner"),
    "qlib": ("Microsoft Qlib", "79633dd9506ea689e5400dea0197717b5b3d74b7", "MIT", "https://github.com/microsoft/qlib/blob/79633dd9506ea689e5400dea0197717b5b3d74b7/qlib/contrib/data/loader.py", "independent formula oracle"),
    "talib": ("TA-Lib 0.6.4", "43f9d5042ecc4bd367941846494ad907bf20ea50", "BSD-3-Clause", "https://github.com/TA-Lib/ta-lib/tree/43f9d5042ecc4bd367941846494ad907bf20ea50/src/ta_func", "canonical traditional-pattern oracle"),
    "pandas_ta": ("pandas-ta-classic 0.3.59", "8c3852d997e5f16af79b558760bddd77773d39de", "MIT", "https://github.com/xgboosted/pandas-ta-classic/tree/8c3852d997e5f16af79b558760bddd77773d39de/pandas_ta_classic/candles", "secondary reference; catalog includes TA-Lib wrappers"),
    "lean": ("QuantConnect LEAN", "07fb0182bfe229edd9445cf675ac6509d0069539", "Apache-2.0", "https://github.com/QuantConnect/Lean/tree/07fb0182bfe229edd9445cf675ac6509d0069539/Indicators/CandlestickPatterns", "secondary streaming/readiness reference"),
    "ta4j": ("ta4j 0.18", "e9918d52853cae3ee70b7d1620b46ae80c8f2b84", "MIT", "https://github.com/ta4j/ta4j/tree/e9918d52853cae3ee70b7d1620b46ae80c8f2b84", "synthetic-series specification reference"),
    "tulip": ("Tulip Indicators", "be18abb13e075ba866898dcc7cb52399603302a6", "LGPL-3.0-or-later", "https://github.com/TulipCharts/tulipindicators/tree/be18abb13e075ba866898dcc7cb52399603302a6", "formula reference only; copying not approved"),
}
RELEASES = {"stratforge": None, "qlib": "v0.9.7-32-g79633dd", "talib": "v0.6.4",
            "pandas_ta": "0.3.59", "lean": None, "ta4j": "0.18", "tulip": None}


def make(name: str, aliases: list[str], category: str, source: str, inputs: list[str],
         parameters: list[str], formula: str, output: str, lookback: str,
         matches: list[str], implementation: str, disposition: str, reason: str) -> dict[str, Any]:
    return {"canonical_name": name, "aliases": aliases, "category": category, "source": source,
            "input_fields": inputs, "parameters": parameters, "formula_or_algorithm": formula,
            "output_domain": output, "expected_lookback": lookback, "stratforge_matches": matches,
            "external_implementation": implementation, "disposition": disposition, "reason": reason}


def existing() -> list[dict[str, Any]]:
    rows = [make(n, [], "geometry", "qlib", ["open", "high", "low", "close"], [],
                 "Qlib Alpha158 current-bar K-line expression", "continuous real", "0",
                 [f"stratforge::{n}"], "native", "existing", "SF011 already provides this pinned-Qlib type")
            for n in ("KMID", "KLEN", "KMID2", "KUP", "KUP2", "KLOW", "KLOW2", "KSFT", "KSFT2")]
    specs = [
        ("CdlInside", ["InsideBar", "CDL_INSIDE"], "structure", "pandas_ta", ["high", "low"], [], "current range is strictly inside prior range", "0 or 1", "1", "stratforge::CdlInside"),
        ("CdlZ", ["CandleZScore", "CDL_Z"], "geometry", "pandas_ta", ["open", "high", "low", "close"], ["period", "full", "ddof"], "rolling candle body z-score", "continuous real", "period - 1", "stratforge::CdlZ"),
        ("HeikinAshi", ["HeikinAshiCandles"], "synthetic_series", "pandas_ta", ["open", "high", "low", "close"], [], "recursive Heikin-Ashi OHLC transform", "four OHLC lines", "0", "stratforge::HeikinAshi"),
        ("haDelta", ["HeikinAshiDelta", "HADELTA"], "geometry", "pandas_ta", ["open", "high", "low", "close"], [], "Heikin-Ashi close minus open", "continuous real", "0", "stratforge::haDelta"),
        ("QStick", ["QSTICK"], "geometry", "tulip", ["open", "close"], ["period"], "moving average of close-open", "continuous real", "period - 1", "stratforge::QStick"),
    ]
    rows += [make(n, a, c, s, i, p, f, o, l, [m], "native", "existing",
                  "Public implementation predates SF012; do not duplicate")
             for n, a, c, s, i, p, f, o, l, m in specs]
    return rows


def candidates() -> list[dict[str, Any]]:
    specs = [
        ("CandleGeometry", [], "geometry", ["open", "high", "low", "close"], [], "body=close-open; range=high-low; upper=high-max(open,close); lower=min(open,close)-low; gap=open-prior_close", "continuous component lines", "1 for gap; 0 otherwise"),
        ("NormalizedCandleGeometry", [], "geometry", ["open", "high", "low", "close"], ["atr_period"], "CandleGeometry components divided by existing ATR", "continuous component lines", "max(1, ATR minimum period)"),
        ("CandlePercentile", [], "geometry", ["selected candle measurement"], ["measurement", "period"], "rolling magnitude rank under the WP1 tie/current-window contract", "continuous percentile", "period - 1"),
        ("OutsideBar", [], "structure", ["high", "low"], ["boundary_mode"], "current range contains prior range under strict or inclusive comparisons", "0 or 1", "1"),
        ("InsideBarDepth", [], "structure", ["high", "low"], ["boundary_mode"], "active-mother-bar containment with depth, range, and breakout direction", "integer/real/-1,0,+1 components", "1"),
        ("NarrowRange", ["NR4", "NR7"], "structure", ["high", "low"], ["period", "tie_policy"], "current range is the smallest in the rolling N-bar window", "0 or 1", "period - 1"),
        ("WideRange", [], "structure", ["high", "low"], ["period", "tie_policy"], "current range is the largest in the rolling N-bar window", "0 or 1", "period - 1"),
        ("PatternConfirmation", [], "context", ["signal", "price", "confirmation_threshold", "invalidation_threshold"], ["expiry_bars", "overlap_policy"], "causal pending/confirmed/invalidated/expired event lifecycle", "state and event age", "max supplied input history"),
        ("TrendContext", [], "context", ["signal", "trend_line"], [], "classify signal against an explicitly supplied trend line", "-1/0/+1 and components", "max supplied input history"),
        ("VolumeConfirmation", [], "context", ["signal", "volume", "baseline"], [], "signal volume divided by explicitly supplied rolling baseline", "ratio and flag", "max supplied input history"),
        ("SupportResistanceContext", [], "context", ["signal", "price", "level_line"], ["level_kind"], "signed and absolute distance from supplied level", "distance and side", "max supplied input history"),
        ("MultiTimeframeContext", [], "context", ["signal", "aligned_context"], [], "sample only explicitly aligned values available on source clock", "context state", "max aligned input history"),
        ("CandleQualityScore", [], "context", ["geometry", "trend", "confirmation", "volume"], ["weights"], "transparent weighted composition with observable components", "component and aggregate scores", "max component history"),
        ("Renko", [], "synthetic_series", ["source_bar"], ["brick_size_or_atr", "reversal_rule"], "variable-cardinality brick transform; WP9 architecture/prototype only", "zero-to-many synthetic bars", "not an indicator"),
        ("Kagi", [], "synthetic_series", ["source_bar"], ["reversal_amount"], "variable-cardinality direction/reversal transform", "zero-to-many synthetic elements", "not an indicator"),
        ("ThreeLineBreak", [], "synthetic_series", ["source_bar"], ["line_count"], "variable-cardinality line-break transform", "zero-to-many synthetic elements", "not an indicator"),
        ("PointAndFigure", ["Point & Figure"], "synthetic_series", ["source_bar"], ["box_size", "reversal_boxes"], "variable-cardinality X/O-column transform", "zero-to-many synthetic elements", "not an indicator"),
    ]
    return [make(n, a, c, "ta4j" if c == "synthetic_series" else "stratforge", i, p, f, o, l, [],
                 "specification-derived", "candidate", "Distinct SF012 scope; WP1/WP9 must freeze semantics before implementation")
            for n, a, c, i, p, f, o, l in specs]


def build() -> dict[str, Any]:
    patterns = json.loads(SF011.read_text(encoding="utf-8"))["patterns"]
    candles = sorted(p["name"] for p in patterns)
    if len(candles) != 61 or len(set(candles)) != 61:
        raise SystemExit("SF011 inventory must contain exactly 61 unique TA-Lib patterns")
    rows = existing() + candidates()
    rows += [
        make("TA-Lib CDL catalog", candles, "structure", "talib", ["open", "high", "low", "close"], ["pattern-specific"], "61 adaptive CDL recognizers", "integer signals", "pattern-specific", [f"stratforge::{n}" for n in candles], "native", "duplicate", "SF011 already implements and aliases all 61"),
        make("pandas-ta candle utilities", ["cdl_inside", "cdl_z", "ha"], "structure", "pandas_ta", ["open", "high", "low", "close"], ["utility-specific"], "native utilities plus optional TA-Lib wrappers", "mixed", "utility-specific", ["CdlInside", "CdlZ", "HeikinAshi"], "mixed native/wrapper", "duplicate", "Distinct advertised utilities already have public equivalents"),
        make("LEAN candlestick catalog", [], "structure", "lean", ["trade bars"], ["candle settings"], "streaming traditional candlestick recognizers", "integer signals", "pattern-specific", ["TA-Lib-compatible CDL surface"], "native but overlapping", "duplicate", "Readiness reference only; overlaps canonical SF011 catalog"),
        make("Tulip QStick source adoption", ["qstick"], "geometry", "tulip", ["open", "close"], ["period"], "moving average of close-open", "continuous real", "period - 1", ["stratforge::QStick"], "native", "rejected", "Already exists; LGPL source copying is unnecessary and unapproved"),
        make("Heikin-Ashi as raw OHLC", [], "synthetic_series", "pandas_ta", ["open", "high", "low", "close"], [], "present recursive transformed candles as exchange bars", "OHLC bars", "recursive", ["stratforge::HeikinAshi"], "native", "divergent", "Treating synthetic candles as raw exchange data violates the ticket contract"),
    ]
    required = {"existing", "candidate", "duplicate", "divergent", "rejected"}
    if {r["disposition"] for r in rows} != required:
        raise SystemExit("inventory does not cover every required disposition")
    keys = ("canonical_name", "category", "source", "input_fields", "parameters", "formula_or_algorithm",
            "output_domain", "expected_lookback", "stratforge_matches", "external_implementation", "disposition", "reason")
    for item in rows:
        missing = [k for k in keys if k not in item or item[k] in (None, "")]
        if missing or item["source"] not in SOURCES:
            raise SystemExit(f"invalid inventory row {item.get('canonical_name')}: {missing}")
    headers = "\n".join(p.read_text(encoding="utf-8") for p in (ROOT / "include/stratforge/indicators").glob("*.hpp"))
    types = ["CdlInside", "CdlZ", "HeikinAshi", "haDelta", "QStick", "KMID", "KLEN", "KMID2",
             "KUP", "KUP2", "KLOW", "KLOW2", "KSFT", "KSFT2"]
    absent = [n for n in types if not re.search(rf"\b(?:class|using)\s+{re.escape(n)}\b", headers)]
    absent += [n for n in candles if not re.search(rf"\busing\s+{n}\b", headers)]
    if absent:
        raise SystemExit(f"existing exclusion surface is missing: {absent}")
    counts = {d: sum(r["disposition"] == d for r in rows) for d in sorted(required)}
    source_rows = {k: {"project": v[0], "revision": v[1], "release": RELEASES[k],
                       "license": v[2], "url": v[3], "role": v[4]} for k, v in SOURCES.items()}
    return {"provenance": {"ticket": "TICKET_SF012", "work_package": "WP0", "generated_on": "2026-08-27", "sources": source_rows},
            "summary": {"rows": len(rows), "dispositions": counts, "talib_aliases_excluded": 61,
                        "qlib_factors_excluded": 9, "other_existing_capabilities_excluded": 5,
                        "new_implementation_count": counts["candidate"]}, "rows": rows}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    report = build()
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != encoded:
            raise SystemExit(f"SF012 inventory has drifted; regenerate {args.output}")
    else:
        write_json(args.output, report)
    print(json.dumps(report["summary"], sort_keys=True))


if __name__ == "__main__":
    main()
