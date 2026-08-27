#!/usr/bin/env python3
"""Generate and verify the TICKET_SF011 Phase 8 integration coverage report."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any

from common import write_json


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path(__file__).resolve().parent / "output" / "sf011" / "coverage_report.json"
ALPHA_INVENTORY = OUTPUT.parent / "alpha158_inventory.json"
ALPHA_PARITY = OUTPUT.parent / "alpha158_parity_report.json"
CANDLE_FIXTURE = OUTPUT.parents[1] / "talib_candles" / "talib_candles_reference.json"
REGISTRY = ROOT / "tools" / "market-analysis-indicator.json"
COMPILE_TEST = ROOT / "tests" / "test_registry_compile.cpp"
PHASE6_TEST = ROOT / "tests" / "test_sf011_phase6.cpp"
PHASE7_TEST = ROOT / "tests" / "test_sf011_phase7.cpp"
TALIB_ALIASES = ROOT / "include" / "stratforge" / "indicators" / "talib_compat.hpp"

EXPECTED_ALPHA158 = 158
EXPECTED_CANDLES = 61
EXPECTED_LOGICAL_IDS = EXPECTED_ALPHA158 + EXPECTED_CANDLES


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sha256_compile_test(path: Path) -> str:
    """Hash the generated test independently of the public source-root macro."""
    content = path.read_text(encoding="utf-8").replace("SF_SOURCE_DIR", "NBT_SOURCE_DIR")
    return hashlib.sha256(content.encode("utf-8")).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def registry_owners(registry: list[dict[str, Any]]) -> dict[str, set[str]]:
    owners: dict[str, set[str]] = {}
    for row in registry:
        tokens = {str(row["slug"]), str(row["name"])}
        tokens.update(str(alias) for alias in row["aliases"])
        for token in tokens:
            owners.setdefault(token, set()).add(str(row["slug"]))
    return owners


def candle_test_names() -> set[str]:
    phase6 = PHASE6_TEST.read_text(encoding="utf-8")
    phase7 = PHASE7_TEST.read_text(encoding="utf-8")
    names = set(re.findall(r'check_pattern<stratforge::\w+>\("(CDL[A-Z0-9]+)"', phase6))
    names.update(re.findall(r"\bX\(\w+,\s*(CDL[A-Z0-9]+)\)", phase7))
    return names


def build_report() -> dict[str, Any]:
    alpha_inventory = load_json(ALPHA_INVENTORY)
    alpha_parity = load_json(ALPHA_PARITY)
    candle_fixture = load_json(CANDLE_FIXTURE)
    registry = load_json(REGISTRY)
    require(isinstance(registry, list), "indicator registry must be a JSON array")

    slugs = [str(row["slug"]) for row in registry]
    require(len(slugs) == len(set(slugs)), "indicator registry slugs are not unique")
    compile_cases = re.findall(
        r'TEST_CASE\("Registry compile: ([A-Za-z_][A-Za-z0-9_]*)"',
        COMPILE_TEST.read_text(encoding="utf-8"),
    )
    require(
        compile_cases == slugs,
        "generated registry compile tests do not exactly match registry row order",
    )

    alpha_rows = alpha_inventory["factors"]
    parity_rows = alpha_parity["factors"]
    require(len(alpha_rows) == EXPECTED_ALPHA158, "Alpha158 inventory must contain 158 rows")
    require(len(parity_rows) == EXPECTED_ALPHA158, "Alpha158 parity report must contain 158 rows")
    parity_by_id = {str(row["factor_id"]): row for row in parity_rows}
    require(len(parity_by_id) == EXPECTED_ALPHA158, "Alpha158 parity factor IDs are not unique")

    alpha_ids = [str(row["factor_id"]) for row in alpha_rows]
    alpha_names = [str(row["name"]) for row in alpha_rows]
    require(len(set(alpha_ids)) == EXPECTED_ALPHA158, "Alpha158 factor IDs are not unique")
    require(len(set(alpha_names)) == EXPECTED_ALPHA158, "Alpha158 names are not unique")

    factors: list[dict[str, Any]] = []
    for row in alpha_rows:
        factor_id = str(row["factor_id"])
        parity = parity_by_id.get(factor_id)
        discoverable = row.get("status") == "mapped" and bool(row.get("name"))
        executable = parity is not None and parity.get("parity_status") == "passed"
        require(discoverable, f"Alpha158 factor is not catalog-discoverable: {factor_id}")
        require(executable, f"Alpha158 factor is not executable with parity: {factor_id}")
        factors.append({
            "namespaced_id": f"alpha158:{factor_id}",
            "factor_id": factor_id,
            "name": row["name"],
            "category": "continuous_factor",
            "execution_route": parity["catalog_route"],
            "cpp_indicator_id": row.get("cpp_indicator_id"),
            "discoverable": discoverable,
            "executable": executable,
            "parity_status": parity["parity_status"],
        })

    fixture_patterns = candle_fixture["patterns"]
    fixture_names = {str(row["name"]) for row in fixture_patterns}
    require(len(fixture_patterns) == EXPECTED_CANDLES, "TA-Lib fixture must contain 61 rows")
    require(len(fixture_names) == EXPECTED_CANDLES, "TA-Lib fixture names are not unique")
    tested_names = candle_test_names()
    require(tested_names == fixture_names, "Phase 6/7 differential tests do not cover all 61 patterns")

    alias_text = TALIB_ALIASES.read_text(encoding="utf-8")
    aliases = dict(re.findall(r"using\s+(CDL[A-Z0-9]+)\s*=\s*stratforge::(\w+)\s*;", alias_text))
    owners = registry_owners(registry)
    registry_by_slug = {str(row["slug"]): row for row in registry}
    patterns: list[dict[str, Any]] = []
    for name in sorted(fixture_names):
        owner_slugs = owners.get(name, set())
        require(len(owner_slugs) == 1, f"TA-Lib ID must resolve to one registry row: {name}")
        slug = next(iter(owner_slugs))
        registry_row = registry_by_slug[slug]
        cpp_type = aliases.get(name)
        discoverable = (
            registry_row.get("category") == "candlestick"
            and registry_row.get("data_input") == "full_data"
        )
        examples = "\n".join(str(line) for line in registry_row.get("example_usage", []))
        example_executable = all(
            token in examples for token in
            (f"stratforge::{cpp_type}", "data.open()", "data.high()", "data.low()", "data.close()")
        )
        executable = (
            cpp_type is not None
            and name in tested_names
            and f"stratforge::{cpp_type}" in str(registry_row.get("code_template", ""))
            and example_executable
        )
        require(discoverable, f"TA-Lib pattern has the wrong registry category: {name}")
        require(executable, f"TA-Lib pattern is not constructible and differential-tested: {name}")
        patterns.append({
            "namespaced_id": f"talib_candlestick:{name}",
            "name": name,
            "category": "discrete_pattern",
            "cpp_type": f"stratforge::{cpp_type}",
            "registry_slug": slug,
            "registry_data_input": registry_row["data_input"],
            "example_executable": example_executable,
            "discoverable": discoverable,
            "executable": executable,
            "parity_status": "passed",
        })

    logical_ids = [row["namespaced_id"] for row in factors + patterns]
    require(len(logical_ids) == EXPECTED_LOGICAL_IDS, "coverage report must contain 219 rows")
    require(len(set(logical_ids)) == EXPECTED_LOGICAL_IDS, "namespaced logical IDs are not unique")

    return {
        "provenance": {
            "ticket": "TICKET_SF011",
            "phase": 8,
            "alpha158_inventory_sha256": sha256(ALPHA_INVENTORY),
            "alpha158_parity_report_sha256": sha256(ALPHA_PARITY),
            "talib_fixture_sha256": sha256(CANDLE_FIXTURE),
            "registry_sha256": sha256(REGISTRY),
            "registry_compile_test_sha256": sha256_compile_test(COMPILE_TEST),
            "talib_alias_header_sha256": sha256(TALIB_ALIASES),
        },
        "summary": {
            "logical_ids": len(logical_ids),
            "unique_namespaced_ids": len(set(logical_ids)),
            "discoverable": sum(row["discoverable"] for row in factors + patterns),
            "executable": sum(row["executable"] for row in factors + patterns),
            "alpha158_continuous_factors": len(factors),
            "talib_discrete_patterns": len(patterns),
            "registry_rows": len(registry),
            "registry_compile_cases": len(compile_cases),
        },
        "alpha158": factors,
        "talib_candlesticks": patterns,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the checked report has drifted")
    args = parser.parse_args()
    report = build_report()
    if args.check:
        require(OUTPUT.exists(), f"coverage report is missing: {OUTPUT}")
        require(load_json(OUTPUT) == report, "SF011 coverage report has drifted; regenerate it")
        print(json.dumps(report["summary"], sort_keys=True))
        return
    write_json(OUTPUT, report)
    print(json.dumps(report["summary"], sort_keys=True))


if __name__ == "__main__":
    main()
