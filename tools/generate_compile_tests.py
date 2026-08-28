#!/usr/bin/env python3
"""Generate tests/test_registry_compile.cpp from the indicator registry.

For each indicator, generates a Catch2 TEST_CASE that:
1. Includes the header
2. Instantiates using code_template with defaults substituted
3. Calls each output_accessor
4. Verifies operator[] access

Usage: python3 tools/generate_compile_tests.py
"""

import argparse
import json
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
REGISTRY_PATH = SCRIPT_DIR / "market-analysis-indicator.json"
OUTPUT_PATH = PROJECT_ROOT / "tests" / "test_registry_compile.cpp"
CONTEXT_LINE_NAMES = (
    "signal", "price", "confirmation_threshold", "invalidation_threshold", "trend",
    "volume", "baseline", "support", "resistance", "geometry", "confirmation",
)


def parameter_value_expression(param: dict, value_key: str) -> str:
    """Render one registry parameter as a C++ expression."""
    if value_key in param:
        value = param[value_key]
    elif "default" in param:
        value = param["default"]
    else:
        type_defaults = {
            "std::size_t": "14",
            "int": "14",
            "double": "0.5",
            "bool": "false",
        }
        return type_defaults.get(param["type"], "0")

    if param["type"] == "SignalOverlapPolicy":
        return f"stratforge::SignalOverlapPolicy::{value}"
    if param["type"] == "CandleQualityWeights":
        if not isinstance(value, list) or len(value) != 4:
            raise ValueError(f"invalid CandleQualityWeights value for {param['name']}")
        return "stratforge::CandleQualityWeights{" + ", ".join(repr(component) for component in value) + "}"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return repr(value)
    return str(value)


def substitute_defaults(code_template: str, params: list[dict],
                        value_key: str = "default") -> str:
    """Replace {param} placeholders with registered values."""
    result = code_template
    for p in params:
        placeholder = "{" + p["name"] + "}"
        result = result.replace(placeholder, parameter_value_expression(p, value_key))
    return result


def braces_to_parens(inst: str) -> str:
    """Convert brace initialization to parenthesis initialization.

    stratforge::SMA sma_{...}; -> stratforge::SMA sma_(...);
    """
    # Match: type var_name{args};
    m = re.match(r'^(.*?\s+\w+)\{(.*)\};$', inst)
    if m:
        if not m.group(2).strip():
            return f"{m.group(1)}{{}};"
        return f"{m.group(1)}({m.group(2)});"
    return inst


def provide_data_inputs(inst: str, entry: dict) -> str:
    """Fill non-placeholder line inputs encoded by registry conventions."""
    inst = inst.replace("period_line", "data.close()")
    if entry["include"] != "stratforge/indicators/candlestick.hpp":
        return inst
    match = re.match(r'^(.*?\s+\w+)\((.*)\);$', inst)
    if match:
        tail = f", {match.group(2)}" if match.group(2).strip() else ""
        return f"{match.group(1)}(data.open(), data.high(), data.low(), data.close(){tail});"
    match = re.match(r'^(.*?\s+\w+)\{(.*)\};$', inst)
    if match:
        tail = f", {match.group(2)}" if match.group(2).strip() else ""
        return f"{match.group(1)}{{data.open(), data.high(), data.low(), data.close(){tail}}};"
    return inst


def generate_dual_line_instantiation(code_template: str, params: list[dict],
                                     value_key: str) -> str:
    """For dual_line indicators, replace {data0}/{data1} with SMA lines."""
    result = substitute_defaults(code_template, params, value_key)
    result = result.replace("{data0}", "sma_a_.line()")
    result = result.replace("{data1}", "sma_b_.line()")
    result = result.replace("{line1}", "sma_a_.line()")
    result = result.replace("{line2}", "sma_b_.line()")
    return braces_to_parens(result)


def generate_template_instantiation(code_template: str, params: list[dict],
                                    value_key: str) -> str:
    """For template indicators, replace {MAType} with stratforge::SMA."""
    result = substitute_defaults(code_template, params, value_key)
    result = result.replace("{MAType}", "stratforge::SMA")
    return braces_to_parens(result)


def generate_context_line_instantiation(code_template: str, params: list[dict],
                                        value_key: str) -> str:
    result = substitute_defaults(code_template, params, value_key)
    for name in CONTEXT_LINE_NAMES:
        result = result.replace("{" + name + "}", f"context_{name}_")
    return braces_to_parens(result)


def context_line_fixture(lines: list[str], indent: str = "    ") -> None:
    for name in CONTEXT_LINE_NAMES:
        lines.append(f"{indent}stratforge::Line<double> context_{name}_;")
    for name in CONTEXT_LINE_NAMES:
        source = "data.volume()[0]" if name == "volume" else "data.close()[0]"
        lines.append(f"{indent}context_{name}_.forward({source});")


def generate_test_case(entry: dict, value_key: str = "default",
                       label: str = "") -> str:
    """Generate a single TEST_CASE for an indicator."""
    slug = entry["slug"]
    data_input = entry["data_input"]
    code_template = entry["code_template"]
    params = entry["constructor_params"]
    accessors = entry["output_accessors"]

    lines = []
    lines.append(f'TEST_CASE("Registry compile{label}: {slug}", "[registry][compile]") {{')
    lines.append(f'    stratforge::CsvData data(stratforge::CsvData::Params{{')
    lines.append(f'        .filename = std::string(SF_SOURCE_DIR) + "/tools/golden_extract/datas/2006-day-001.txt",')
    lines.append(f'    }});')
    lines.append(f'    data.preload();')
    lines.append(f'')

    if data_input == "dual_line":
        lines.append(f'    stratforge::SMA sma_a_(data.close(), 10);')
        lines.append(f'    stratforge::SMA sma_b_(data.close(), 30);')
        inst = generate_dual_line_instantiation(code_template, params, value_key)
    elif data_input == "context_lines":
        context_line_fixture(lines)
        lines.append(f'')
        inst = generate_context_line_instantiation(code_template, params, value_key)
    elif entry.get("template_keys") or "{MAType}" in code_template:
        inst = generate_template_instantiation(code_template, params, value_key)
    else:
        inst = braces_to_parens(substitute_defaults(code_template, params, value_key))

    inst = provide_data_inputs(inst, entry)

    # Extract variable name from instantiation
    var_match = re.search(r'(?:stratforge::\w+(?:<[^>]+>)?)\s+(\w+)\(', inst)
    if not var_match:
        var_match = re.search(r'(?:stratforge::\w+(?:<[^>]+>)?)\s+(\w+)\{', inst)
    var_name = var_match.group(1) if var_match else slug.lower() + "_"

    lines.append(f'    {inst}')
    lines.append(f'')
    lines.append(f'    // Process enough bars (preload sets index to 0)')
    if data_input == "dual_line":
        lines.append(f'    sma_a_.next(); sma_b_.next();')
    lines.append(f'    {var_name}.next();')
    lines.append(f'    for (std::size_t i = 0; i < 59; ++i) {{')
    lines.append(f'        data.advance();')
    if data_input == "dual_line":
        lines.append(f'        sma_a_.next(); sma_b_.next();')
    if data_input == "context_lines":
        for name in CONTEXT_LINE_NAMES:
            source = "data.volume()[0]" if name == "volume" else "data.close()[0]"
            lines.append(f'        context_{name}_.forward({source});')
    lines.append(f'        {var_name}.next();')
    lines.append(f'    }}')
    lines.append(f'')

    # Test each accessor - compile verification only
    for line_name, accessor in accessors.items():
        lines.append(f'    [[maybe_unused]] const auto& ref_{line_name} = {var_name}.{accessor};')

    # Test operator[] (compile check - value may be NaN)
    lines.append(f'    [[maybe_unused]] auto op0 = {var_name}[0];')

    lines.append(f'}}')
    return '\n'.join(lines)


def render(registry: list[dict]) -> str:
    """Render one compile test for every registry row."""
    # Collect unique includes
    includes = sorted(set(entry["include"] for entry in registry))

    parts = []
    parts.append("// AUTO-GENERATED by tools/generate_compile_tests.py")
    parts.append("// DO NOT EDIT MANUALLY - regenerate with:")
    parts.append("//   python3 tools/generate_compile_tests.py")
    parts.append("")
    parts.append("#include <catch2/catch_test_macros.hpp>")
    parts.append("#include <stratforge/data/csv_data.hpp>")
    parts.append("#include <stratforge/core/line.hpp>")
    parts.append("#include <stratforge/indicators/sma.hpp>  // for dual_line fixtures")
    parts.append("")
    for inc in includes:
        parts.append(f"#include <{inc}>")
    parts.append("")
    parts.append("#include <string>")
    parts.append("")

    for entry in registry:
        parts.append(generate_test_case(entry))
        parts.append("")
        if any("compile_test_value" in parameter for parameter in entry["constructor_params"]):
            parts.append(generate_test_case(entry, "compile_test_value", " non-default"))
            parts.append("")

    return '\n'.join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true",
        help="fail instead of writing when the generated compile test has drifted",
    )
    args = parser.parse_args()

    if not REGISTRY_PATH.exists():
        print(f"ERROR: Registry not found: {REGISTRY_PATH}", file=sys.stderr)
        return 1

    with open(REGISTRY_PATH) as f:
        registry = json.load(f)

    content = render(registry)
    if args.check:
        if not OUTPUT_PATH.exists() or OUTPUT_PATH.read_text() != content:
            print(
                f"ERROR: {OUTPUT_PATH} has drifted; run tools/generate_compile_tests.py",
                file=sys.stderr,
            )
            return 1
        print(f"Verified {OUTPUT_PATH} covers all {len(registry)} registry rows")
        return 0
    OUTPUT_PATH.write_text(content)
    test_cases = len(registry) + sum(
        any("compile_test_value" in parameter for parameter in entry["constructor_params"])
        for entry in registry
    )
    print(f"Generated {OUTPUT_PATH} with {test_cases} test cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
