#!/usr/bin/env python3
"""Generate and verify the TICKET_SF012 WP10 integration coverage report."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
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
HISTORY_DOCUMENTATION = ROOT / "docs" / "indicator-history-contract.md"
REGISTRY_COMPILE = ROOT / "tests" / "test_registry_compile.cpp"
HISTORY_CONTRACT = ROOT / "include" / "stratforge" / "indicators" / "history_contract_generated.hpp"
COMPILE_GENERATOR = ROOT / "tools" / "generate_compile_tests.py"
HISTORY_GENERATOR = ROOT / "tools" / "generate_indicator_history_contract.py"

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
CONTEXT_PARAMETERS = {
    "PatternConfirmation": {
        "expiry_bars": {"type": "std::size_t", "default": 3},
        "overlap_policy": {
            "type": "SignalOverlapPolicy", "default": "replace",
            "compile_test_value": "queue_one",
        },
    },
    "CandleQualityScore": {
        "weights": {
            "type": "CandleQualityWeights", "default": [1.0, 1.0, 1.0, 1.0],
            "compile_test_value": [1.0, 2.0, 3.0, 4.0],
        },
    },
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_generator(path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(path.stem, path)
    require(spec is not None and spec.loader is not None,
            f"unable to load generator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def markdown_table_identities(document: str) -> set[str]:
    identities: set[str] = set()
    for line in document.splitlines():
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) < 3:
            continue
        first = cells[1]
        if first.startswith("`") and first.endswith("`"):
            identities.add(first.removeprefix("`").removesuffix("`"))
    return identities


def markdown_code_identities(document: str) -> set[str]:
    return set(re.findall(r"`([A-Za-z_][A-Za-z0-9_]*)`", document))


def public_type_is_declared(document: str, name: str) -> bool:
    return re.search(rf"^(?:class|struct)\s+{re.escape(name)}\b", document,
                     re.MULTILINE) is not None


def registry_parameters_match(entry: dict[str, Any], expected: dict[str, dict[str, Any]]) -> bool:
    parameters = {str(parameter["name"]): parameter for parameter in entry["constructor_params"]}
    return all(parameters.get(name) == {"name": name, "position": position, **metadata}
               for position, (name, metadata) in enumerate(expected.items(), start=1))


def build_report() -> dict[str, Any]:
    inventory = load_json(INVENTORY)
    candidates = [row for row in inventory["rows"] if row["disposition"] == "candidate"]
    require(len(candidates) == 17, "SF012 inventory must contain exactly 17 candidates")
    names = {str(row["canonical_name"]) for row in candidates}
    expected = set(PUBLIC_TYPES + CONTEXT_TYPES + SYNTHETIC_TYPES + ("MultiTimeframeContext",))
    require(names == expected, "SF012 candidate set does not match the frozen contract")

    registry = load_json(REGISTRY)
    registry_by_slug = {str(row["slug"]): row for row in registry}
    registry_slugs = set(registry_by_slug)
    for name in PUBLIC_TYPES:
        require(name in registry_slugs, f"public Priority 1 type is not registered: {name}")
    require({"NR4", "NR7"}.issubset(registry_slugs), "NR4/NR7 registry presets are missing")

    compile_generator = load_generator(COMPILE_GENERATOR)
    history_generator = load_generator(HISTORY_GENERATOR)
    generated_compile_is_current = (REGISTRY_COMPILE.is_file() and
                                    REGISTRY_COMPILE.read_text(encoding="utf-8") ==
                                    compile_generator.render(registry))
    generated_history_is_current = (HISTORY_CONTRACT.is_file() and
                                    HISTORY_CONTRACT.read_text(encoding="utf-8") ==
                                    history_generator.render(registry))

    aggregate = AGGREGATE.read_text(encoding="utf-8")
    require("indicators/candle_structure.hpp" in aggregate, "aggregate header omits candle structure")
    require("indicators/pattern_context.hpp" in aggregate, "aggregate header omits pattern context")
    structure = (STRUCTURE_COMMON.read_text(encoding="utf-8") + "\n" +
                 STRUCTURE.read_text(encoding="utf-8"))
    context = CONTEXT.read_text(encoding="utf-8")
    api = API_REFERENCE.read_text(encoding="utf-8")
    history_documentation = HISTORY_DOCUMENTATION.read_text(encoding="utf-8")
    architecture = ARCHITECTURE.read_text(encoding="utf-8")
    api_identities = markdown_table_identities(api)
    history_identities = markdown_code_identities(history_documentation)
    for name in PUBLIC_TYPES:
        require(public_type_is_declared(structure, name) or
                re.search(rf"^using\s+{re.escape(name)}\b", structure, re.MULTILINE) is not None,
                f"public type is missing: {name}")
        require(name in api_identities, f"API reference omits public type: {name}")
    for name in CONTEXT_TYPES:
        checks = {
            "declared": public_type_is_declared(context, name),
            "registered": name in registry_by_slug,
            "api_documented": name in api_identities,
            "history_documented": name in history_identities,
            "compile_generated": generated_compile_is_current,
            "history_generated": generated_history_is_current,
        }
        if name in CONTEXT_PARAMETERS:
            checks["parameters_registered"] = registry_parameters_match(
                registry_by_slug[name], CONTEXT_PARAMETERS[name])
        require(all(checks.values()),
                f"context surface is incomplete for {name}: " +
                ", ".join(label for label, passed in checks.items() if not passed))
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
            outcome, surface = "implemented_composable", "indicator_registry"
        elif name == "MultiTimeframeContext":
            outcome, surface = "implemented_engine_contract", "multi_feed_execution"
        else:
            outcome, surface = "architecture_approved_production_deferred", "series_transform_contract"
        if name in PUBLIC_TYPES:
            checks = {
                "declared": public_type_is_declared(structure, name) or
                            re.search(rf"^using\s+{re.escape(name)}\b", structure,
                                      re.MULTILINE) is not None,
                "registered": name in registry_by_slug,
                "api_documented": name in api_identities,
                "compile_generated": generated_compile_is_current,
                "history_generated": generated_history_is_current,
            }
        elif name in CONTEXT_TYPES:
            checks = {
                "declared": public_type_is_declared(context, name),
                "registered": name in registry_by_slug,
                "api_documented": name in api_identities,
                "history_documented": name in history_identities,
                "compile_generated": generated_compile_is_current,
                "history_generated": generated_history_is_current,
            }
            if name in CONTEXT_PARAMETERS:
                checks["parameters_registered"] = registry_parameters_match(
                    registry_by_slug[name], CONTEXT_PARAMETERS[name])
        elif name == "MultiTimeframeContext":
            checks = {"integration_test": TESTS["multi_clock"].is_file()}
        else:
            checks = {"architecture_documented": architecture_names[name] in architecture,
                      "not_registered": name not in registry_by_slug}
        covered = all(checks.values())
        require(covered, f"SF012 candidate is not covered: {name}")
        rows.append({"name": name, "category": candidate["category"],
                     "outcome": outcome, "surface": surface,
                     "checks": checks, "covered": covered})

    counts: dict[str, int] = {}
    for row in rows:
        counts[row["outcome"]] = counts.get(row["outcome"], 0) + 1
    return {
        "provenance": {
            "ticket": "TICKET_SF012", "work_package": 10,
            "inventory_sha256": sha256(INVENTORY), "registry_sha256": sha256(REGISTRY),
            "test_sha256": {label: sha256(path) for label, path in TESTS.items()},
            "source_sha256": {
                "aggregate": sha256(AGGREGATE), "structure": sha256(STRUCTURE),
                "structure_common": sha256(STRUCTURE_COMMON), "context": sha256(CONTEXT),
                "architecture": sha256(ARCHITECTURE), "api_reference": sha256(API_REFERENCE),
                "history_documentation": sha256(HISTORY_DOCUMENTATION),
            },
            "generator_sha256": {
                "registry_compile": sha256(COMPILE_GENERATOR),
                "history_contract": sha256(HISTORY_GENERATOR),
            },
            "generated_artifact_sha256": {
                "registry_compile": sha256(REGISTRY_COMPILE),
                "history_contract": sha256(HISTORY_CONTRACT),
            },
        },
        "summary": {"inventory_candidates": len(rows),
                    "covered_candidates": sum(bool(row["covered"]) for row in rows),
                    "outcomes": counts, "priority1_registry_types": len(PUBLIC_TYPES),
                    "context_registry_types": len(CONTEXT_TYPES), "registry_presets": 2},
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
