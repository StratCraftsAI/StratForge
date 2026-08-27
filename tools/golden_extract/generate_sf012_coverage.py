#!/usr/bin/env python3
"""Generate and verify the TICKET_SF012 WP10 integration coverage report."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from common import write_json

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path(__file__).resolve().parent / "output" / "sf012" / "coverage_report.json"
INVENTORY = OUTPUT.parent / "inventory.json"
REGISTRY = ROOT / "tools" / "market-analysis-indicator.json"
AGGREGATE = ROOT / "include" / "stratforge" / "stratforge.hpp"
STRUCTURE = ROOT / "include" / "stratforge" / "indicators" / "candle_structure.hpp"
STRUCTURE_COMMON = ROOT / "include" / "stratforge" / "indicators" / "candle_structure_common.hpp"
CONTEXT = ROOT / "include" / "stratforge" / "indicators" / "pattern_context.hpp"
ARCHITECTURE = ROOT / "docs" / "synthetic-series-architecture.md"
API_REFERENCE = ROOT / "docs" / "api_reference.md"

PUBLIC_TYPES = ("CandleGeometry", "NormalizedCandleGeometry", "CandlePercentile",
                "OutsideBar", "InsideBarDepth", "NarrowRange", "WideRange")
CONTEXT_TYPES = ("PatternConfirmation", "TrendContext", "VolumeConfirmation",
                 "SupportResistanceContext", "CandleQualityScore")
SYNTHETIC_TYPES = ("Renko", "Kagi", "ThreeLineBreak", "PointAndFigure")
TESTS = {
    "geometry": ROOT / "tests" / "test_sf012_geometry.cpp",
    "structure": ROOT / "tests" / "test_sf012_structure.cpp",
    "context": ROOT / "tests" / "test_sf012_context.cpp",
    "multi_clock": ROOT / "tests" / "test_sf012_multiclock.cpp",
    "synthetic_series": ROOT / "tests" / "test_sf012_synthetic_series.cpp",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_report() -> dict[str, Any]:
    inventory = load_json(INVENTORY)
    candidates = [row for row in inventory["rows"] if row["disposition"] == "candidate"]
    require(len(candidates) == 17, "SF012 inventory must contain exactly 17 candidates")
    names = {str(row["canonical_name"]) for row in candidates}
    expected = set(PUBLIC_TYPES + CONTEXT_TYPES + SYNTHETIC_TYPES + ("MultiTimeframeContext",))
    require(names == expected, "SF012 candidate set does not match the frozen contract")

    registry = load_json(REGISTRY)
    registry_slugs = {str(row["slug"]) for row in registry}
    for name in PUBLIC_TYPES:
        require(name in registry_slugs, f"public Priority 1 type is not registered: {name}")
    require({"NR4", "NR7"}.issubset(registry_slugs), "NR4/NR7 registry presets are missing")

    aggregate = AGGREGATE.read_text(encoding="utf-8")
    require("indicators/candle_structure.hpp" in aggregate, "aggregate header omits candle structure")
    require("indicators/pattern_context.hpp" in aggregate, "aggregate header omits pattern context")
    structure = (STRUCTURE_COMMON.read_text(encoding="utf-8") + "\n" +
                 STRUCTURE.read_text(encoding="utf-8"))
    context = CONTEXT.read_text(encoding="utf-8")
    api = API_REFERENCE.read_text(encoding="utf-8")
    architecture = ARCHITECTURE.read_text(encoding="utf-8")
    for name in PUBLIC_TYPES:
        require(f"class {name}" in structure or f"using {name}" in structure,
                f"public type is missing: {name}")
        require(name in api, f"API reference omits public type: {name}")
    for name in CONTEXT_TYPES:
        require(f"class {name}" in context, f"context wrapper is missing: {name}")
    architecture_names = {"Renko": "Renko", "Kagi": "Kagi",
                          "ThreeLineBreak": "Three-Line Break", "PointAndFigure": "Point & Figure"}
    for name in SYNTHETIC_TYPES:
        require(architecture_names[name] in architecture, f"synthetic architecture omits: {name}")
        require(name not in registry_slugs, f"deferred synthetic type was registered: {name}")
    for label, path in TESTS.items():
        require(path.is_file() and path.stat().st_size > 0, f"missing {label} integration test")

    rows: list[dict[str, Any]] = []
    for candidate in candidates:
        name = str(candidate["canonical_name"])
        if name in PUBLIC_TYPES:
            outcome, surface = "implemented_public", "indicator_registry"
        elif name in CONTEXT_TYPES:
            outcome, surface = "implemented_composable", "aggregate_header"
        elif name == "MultiTimeframeContext":
            outcome, surface = "implemented_engine_contract", "multi_feed_execution"
        else:
            outcome, surface = "architecture_approved_production_deferred", "series_transform_contract"
        rows.append({"name": name, "category": candidate["category"],
                     "outcome": outcome, "surface": surface, "covered": True})

    counts: dict[str, int] = {}
    for row in rows:
        counts[row["outcome"]] = counts.get(row["outcome"], 0) + 1
    return {
        "provenance": {
            "ticket": "TICKET_SF012", "work_package": 10,
            "inventory_sha256": sha256(INVENTORY), "registry_sha256": sha256(REGISTRY),
            "test_sha256": {label: sha256(path) for label, path in TESTS.items()},
        },
        "summary": {"inventory_candidates": len(rows),
                    "covered_candidates": sum(bool(row["covered"]) for row in rows),
                    "outcomes": counts, "priority1_registry_types": len(PUBLIC_TYPES),
                    "registry_presets": 2},
        "candidates": rows,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the checked report has drifted")
    args = parser.parse_args()
    report = build_report()
    if args.check:
        require(OUTPUT.exists(), f"coverage report is missing: {OUTPUT}")
        require(load_json(OUTPUT) == report, "SF012 coverage report has drifted; regenerate it")
    else:
        write_json(OUTPUT, report)
    print(json.dumps(report["summary"], sort_keys=True))


if __name__ == "__main__":
    main()
