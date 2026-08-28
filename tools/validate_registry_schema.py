#!/usr/bin/env python3
"""Validate tools/market-analysis-indicator.json against the JSON schema.

Zero-dependency (stdlib only) validator for CI integration.
Exits non-zero on failure.
"""

import json
import math
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
REGISTRY_PATH = SCRIPT_DIR / "market-analysis-indicator.json"
SCHEMA_PATH = SCRIPT_DIR / "indicator-registry.schema.json"

# Valid enum values from schema
VALID_CATEGORIES = {
    "trend", "momentum", "volatility", "volume", "directional",
    "statistical", "statistic", "signal", "envelope", "helper", "adaptive",
    "advanced", "pattern", "oscillator", "utility", "overlap", "price_transform",
    "candlestick", "context"
}
VALID_DATA_INPUTS = {"single_line", "full_data", "dual_line", "close", "context_lines"}
VALID_PARAM_TYPES = {
    "std::size_t", "int", "double", "bool", "SignalOverlapPolicy", "CandleQualityWeights"
}
SIGNAL_OVERLAP_POLICIES = {"ignore", "replace", "queue_one"}


def valid_weights(value: object) -> bool:
    return (isinstance(value, list) and len(value) == 4 and
            all(isinstance(item, (int, float)) and not isinstance(item, bool) and
                math.isfinite(float(item)) and float(item) >= 0.0 for item in value))


def validate_entry(entry: dict, idx: int) -> list[str]:
    """Validate a single registry entry. Returns list of error strings."""
    errors = []
    slug = entry.get("slug", f"<index {idx}>")
    prefix = f"[{idx}] {slug}"

    # Required fields
    required = [
        "slug", "name", "aliases", "category", "include", "code_template",
        "constructor_params", "data_input", "output_lines", "output_accessors",
        "example_usage", "template_keys", "usage_modes"
    ]
    for field in required:
        if field not in entry:
            errors.append(f"{prefix}: missing required field '{field}'")

    if errors:
        return errors

    # slug pattern
    if not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', entry["slug"]):
        errors.append(f"{prefix}: slug does not match pattern")

    # display_name non-empty when present
    display_name = entry.get("display_name")
    if display_name is not None and (not isinstance(display_name, str) or not display_name):
        errors.append(f"{prefix}: display_name must be a non-empty string when present")

    # name non-empty
    if not entry["name"]:
        errors.append(f"{prefix}: name is empty")

    # aliases
    if not isinstance(entry["aliases"], list) or len(entry["aliases"]) < 1:
        errors.append(f"{prefix}: aliases must be non-empty array")

    # category enum
    if entry["category"] not in VALID_CATEGORIES:
        errors.append(f"{prefix}: invalid category '{entry['category']}'")

    # include pattern
    if not re.match(r'^stratforge/indicators/[a-z0-9_]+\.hpp$', entry["include"]):
        errors.append(f"{prefix}: include '{entry['include']}' does not match pattern")

    # code_template non-empty
    if not entry["code_template"]:
        errors.append(f"{prefix}: code_template is empty")

    # constructor_params
    if not isinstance(entry["constructor_params"], list):
        errors.append(f"{prefix}: constructor_params must be array")
    else:
        for pi, param in enumerate(entry["constructor_params"]):
            pp = f"{prefix}.constructor_params[{pi}]"
            for pf in ["name", "type", "position"]:
                if pf not in param:
                    errors.append(f"{pp}: missing '{pf}'")
            if "type" in param and param["type"] not in VALID_PARAM_TYPES:
                errors.append(f"{pp}: invalid type '{param['type']}'")
            if "position" in param and (not isinstance(param["position"], int) or param["position"] < 1):
                errors.append(f"{pp}: position must be integer >= 1")
            # Check no extra keys
            allowed_keys = {"name", "type", "position", "default", "compile_test_value"}
            extra = set(param.keys()) - allowed_keys
            if extra:
                errors.append(f"{pp}: unexpected keys {extra}")
            if param.get("type") == "SignalOverlapPolicy":
                for key in ("default", "compile_test_value"):
                    if key in param and param[key] not in SIGNAL_OVERLAP_POLICIES:
                        errors.append(f"{pp}: {key} must be a SignalOverlapPolicy spelling")
            if param.get("type") == "CandleQualityWeights":
                for key in ("default", "compile_test_value"):
                    if key in param and not valid_weights(param[key]):
                        errors.append(f"{pp}: {key} must contain four finite non-negative doubles")

    # data_input enum
    if entry["data_input"] not in VALID_DATA_INPUTS:
        errors.append(f"{prefix}: invalid data_input '{entry['data_input']}'")

    # output_lines
    if not isinstance(entry["output_lines"], list) or len(entry["output_lines"]) < 1:
        errors.append(f"{prefix}: output_lines must be non-empty array")

    # output_accessors
    if not isinstance(entry["output_accessors"], dict) or len(entry["output_accessors"]) < 1:
        errors.append(f"{prefix}: output_accessors must be non-empty object")

    # usage_modes
    um = entry.get("usage_modes", {})
    if not isinstance(um, dict):
        errors.append(f"{prefix}: usage_modes must be object")
    else:
        for k in ["standalone", "strategy_template"]:
            if k not in um:
                errors.append(f"{prefix}: usage_modes missing '{k}'")
            elif not isinstance(um[k], bool):
                errors.append(f"{prefix}: usage_modes.{k} must be boolean")

    # No extra top-level keys
    allowed_top = set(required) | {"display_name"}
    extra_top = set(entry.keys()) - allowed_top
    if extra_top:
        errors.append(f"{prefix}: unexpected top-level keys {extra_top}")

    return errors


def main() -> int:
    if not REGISTRY_PATH.exists():
        print(f"FAIL: Registry file not found: {REGISTRY_PATH}")
        return 1

    if not SCHEMA_PATH.exists():
        print(f"FAIL: Schema file not found: {SCHEMA_PATH}")
        return 1

    with open(REGISTRY_PATH) as f:
        registry = json.load(f)

    if not isinstance(registry, list):
        print("FAIL: Registry root must be a JSON array")
        return 1

    all_errors = []
    slugs_seen = set()

    for idx, entry in enumerate(registry):
        if not isinstance(entry, dict):
            all_errors.append(f"[{idx}]: entry is not an object")
            continue
        errs = validate_entry(entry, idx)
        all_errors.extend(errs)

        # Check slug uniqueness
        slug = entry.get("slug")
        if slug:
            if slug in slugs_seen:
                all_errors.append(f"[{idx}] {slug}: duplicate slug")
            slugs_seen.add(slug)

    if all_errors:
        print(f"FAIL: {len(all_errors)} validation error(s):")
        for e in all_errors:
            print(f"  - {e}")
        return 1

    print(f"PASS: {len(registry)} indicators validated against schema")
    return 0


if __name__ == "__main__":
    sys.exit(main())
