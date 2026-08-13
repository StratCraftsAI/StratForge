#!/usr/bin/env python3
"""Generate the public indicator-history resolver from the indicator registry.

The generated resolver constructs the real indicator and calls its SDK-owned
minimum-period implementation. It contains constructor/adaptor wiring, never a
duplicated table of period formulas.
"""

import argparse
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "tools" / "market-analysis-indicator.json"
OUTPUT = ROOT / "include" / "stratforge" / "indicators" / "history_contract_generated.hpp"


def cpp_string(value: str) -> str:
    return json.dumps(value)


def default_expression(param: dict) -> str:
    if "default" not in param:
        if param["type"] == "double" and param["name"] == "alpha":
            return "std::numeric_limits<double>::quiet_NaN()"
        raise ValueError(f"missing default for {param['name']}")
    value = param["default"]
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError(f"non-finite JSON default for {param['name']}")
        return repr(value)
    return str(value)


def parameter_variant_expression(param: dict) -> str:
    expression = default_expression(param)
    wrappers = {
        "std::size_t": "std::uint64_t",
        "int": "std::int64_t",
        "double": "double",
        "bool": "bool",
    }
    return f"Parameter{{{wrappers[param['type']]}{{{expression}}}}}"


def constructor(entry: dict) -> tuple[str, str]:
    text = entry["code_template"]
    text = text.replace("{MAType}", "stratforge::SMA")
    for token in ("data0", "data1", "line1", "line2"):
        text = text.replace("{" + token + "}", "close")
    for field in ("open", "high", "low", "close", "volume"):
        text = text.replace(f"data.{field}()", field)
    for param in entry["constructor_params"]:
        text = text.replace("{" + param["name"] + "}", param["name"])
    match = re.match(r"^(.*?)\s+(\w+)_\{(.*)\};$", text)
    if not match:
        raise ValueError(f"unsupported code_template for {entry['slug']}: {text}")
    arguments = match.group(3)
    if entry["include"] == "stratforge/indicators/candlestick.hpp":
        arguments = "open, high, low, close" + (f", {arguments}" if arguments else "")
    declaration = (f"{match.group(1)} indicator({arguments});" if arguments else
                   f"{match.group(1)} indicator{{}};")
    return declaration, match.group(2) + "_"


def generate_entry(entry: dict) -> list[str]:
    identities = list(dict.fromkeys([entry["slug"], *entry["aliases"]]))
    condition = " || ".join(f"identity == {cpp_string(name)}" for name in identities)
    lines = [f"    if ({condition}) {{"]
    allowed = [param["name"] for param in entry["constructor_params"]]
    if allowed:
        condition = " && ".join(
            f"name != {cpp_string(name)}" for name in allowed
        )
        lines.extend([
            "        for (const auto& [name, value] : parameters) {",
            "            static_cast<void>(value);",
            f"            if ({condition}) {{",
            "                return std::unexpected(Error{ErrorCode::InvalidParameter,",
            "                    \"unknown parameter '\" + name + \"'\"});",
            "            }",
            "        }",
        ])
    elif not allowed:
        lines.extend([
            "        if (!parameters.empty()) {",
            "            return std::unexpected(Error{ErrorCode::InvalidParameter,",
            "                \"indicator accepts no constructor parameters\"});",
            "        }",
        ])

    readers = {
        "std::size_t": "read_size",
        "int": "read_int",
        "double": "read_double",
        "bool": "read_bool",
    }
    for param in entry["constructor_params"]:
        name = param["name"]
        lines.extend([
            f"        auto {name}_result = {readers[param['type']]}(",
            f"            parameters, {cpp_string(name)}, {default_expression(param)});",
            f"        if (!{name}_result) return std::unexpected({name}_result.error());",
            f"        const auto {name} = *{name}_result;",
        ])

    accessor_conditions = []
    for key, method in entry["output_accessors"].items():
        method_name = method.removesuffix("()")
        accessor_conditions.append(
            f"accessor == {cpp_string(key)} || accessor == {cpp_string(method)}"
        )
        if method_name != key:
            accessor_conditions.append(f"accessor == {cpp_string(method_name)}")
    lines.extend([
        f"        if (!({' || '.join(accessor_conditions)})) {{",
        "            return std::unexpected(Error{ErrorCode::UnknownAccessor,",
        "                \"unknown accessor: \" + std::string(accessor)});",
        "        }",
    ])

    declaration, _ = constructor(entry)
    lines.append(f"        {declaration}")
    for param in entry["constructor_params"]:
        if param["type"] == "std::size_t":
            name = param["name"]
            lines.extend([
                f"        if ({name} > std::numeric_limits<std::size_t>::max() / 2 &&",
                f"            indicator.minimum_period() < {name}) {{",
                "            return std::unexpected(Error{ErrorCode::Overflow,",
                "                \"runtime minimum-period arithmetic overflow\"});",
                "        }",
            ])
    for key, method in entry["output_accessors"].items():
        method_name = method.removesuffix("()")
        alternatives = [f"accessor == {cpp_string(key)}", f"accessor == {cpp_string(method)}"]
        if method_name != key:
            alternatives.append(f"accessor == {cpp_string(method_name)}")
        lines.append(
            f"        if ({' || '.join(dict.fromkeys(alternatives))}) "
            f"return accessor_period(indicator, {cpp_string(key)});"
        )
    lines.extend([
        "        return std::unexpected(Error{ErrorCode::UnknownAccessor,",
        "            \"unknown accessor: \" + std::string(accessor)});",
        "    }",
    ])
    return lines


def generate_aggregate_entry(entry: dict) -> list[str]:
    identities = list(dict.fromkeys([entry["slug"], *entry["aliases"]]))
    condition = " || ".join(f"identity == {cpp_string(name)}" for name in identities)
    lines = [f"    if ({condition}) {{"]
    allowed = [param["name"] for param in entry["constructor_params"]]
    if allowed:
        unknown = " && ".join(f"name != {cpp_string(name)}" for name in allowed)
        lines.extend([
            "        for (const auto& [name, value] : parameters) {",
            "            static_cast<void>(value);",
            f"            if ({unknown}) {{",
            "                return std::unexpected(Error{ErrorCode::InvalidParameter,",
            "                    \"unknown parameter '\" + name + \"'\"});",
            "            }",
            "        }",
        ])
    else:
        lines.extend([
            "        if (!parameters.empty()) {",
            "            return std::unexpected(Error{ErrorCode::InvalidParameter,",
            "                \"indicator accepts no constructor parameters\"});",
            "        }",
        ])

    readers = {
        "std::size_t": "read_size",
        "int": "read_int",
        "double": "read_double",
        "bool": "read_bool",
    }
    for param in entry["constructor_params"]:
        name = param["name"]
        lines.extend([
            f"        auto {name}_result = {readers[param['type']]}(",
            f"            parameters, {cpp_string(name)}, {default_expression(param)});",
            f"        if (!{name}_result) return std::unexpected({name}_result.error());",
            f"        const auto {name} = *{name}_result;",
        ])
    declaration, _ = constructor(entry)
    lines.extend([
        f"        {declaration}",
        "        return indicator.minimum_period();",
        "    }",
    ])
    return lines


def generate_observed_entry(entry: dict) -> list[str]:
    identities = list(dict.fromkeys([entry["slug"], *entry["aliases"]]))
    condition = " || ".join(f"identity == {cpp_string(name)}" for name in identities)
    lines = [f"    if ({condition}) {{"]
    allowed = [param["name"] for param in entry["constructor_params"]]
    if allowed:
        unknown = " && ".join(f"name != {cpp_string(name)}" for name in allowed)
        lines.extend([
            "        for (const auto& [name, value] : parameters) {",
            "            static_cast<void>(value);",
            f"            if ({unknown}) {{",
            "                return std::unexpected(Error{ErrorCode::InvalidParameter,",
            "                    \"unknown parameter '\" + name + \"'\"});",
            "            }",
            "        }",
        ])
    else:
        lines.extend([
            "        if (!parameters.empty()) {",
            "            return std::unexpected(Error{ErrorCode::InvalidParameter,",
            "                \"indicator accepts no constructor parameters\"});",
            "        }",
        ])
    readers = {
        "std::size_t": "read_size",
        "int": "read_int",
        "double": "read_double",
        "bool": "read_bool",
    }
    for param in entry["constructor_params"]:
        name = param["name"]
        lines.extend([
            f"        auto {name}_result = {readers[param['type']]}(",
            f"            parameters, {cpp_string(name)}, {default_expression(param)});",
            f"        if (!{name}_result) return std::unexpected({name}_result.error());",
            f"        const auto {name} = *{name}_result;",
        ])
    declaration, _ = constructor(entry)
    lines.extend([
        f"        {declaration}",
        "        const auto aggregate = indicator.minimum_period();",
        "        if (aggregate > 100000) {",
        "            return std::unexpected(Error{ErrorCode::UnsupportedPeriodContract,",
        "                \"runtime readiness probe is bounded to 100000 bars\"});",
        "        }",
        "        for (std::size_t bar = 0; bar < aggregate + 2; ++bar) {",
        "            const double step = static_cast<double>(bar);",
        "            const double base = 100.0 + step * 0.1 + std::sin(step * 0.37) * 3.0;",
        "            open.forward(base - 0.25);",
        "            high.forward(base + 2.0);",
        "            low.forward(base - 2.0);",
        "            close.forward(base + std::cos(step * 0.19));",
        "            volume.forward(1000.0 + static_cast<double>((bar * 17) % 101));",
        "            period_line.forward(14.0);",
        "            indicator.next();",
    ])
    for key, method in entry["output_accessors"].items():
        method_name = method.removesuffix("()")
        alternatives = [f"accessor == {cpp_string(key)}", f"accessor == {cpp_string(method)}"]
        if method_name != key:
            alternatives.append(f"accessor == {cpp_string(method_name)}")
        lines.extend([
            f"            if ({' || '.join(dict.fromkeys(alternatives))}) {{",
            f"                const auto& output = indicator.{method};",
            f"                const auto ready_at = accessor_period(indicator, {cpp_string(key)});",
            "                if (bar + 1 == ready_at && !output.empty()) {",
            "                    static_cast<void>(output[0]);",
            "                    return bar + 1;",
            "                }",
            "            }",
        ])
    lines.extend([
        "        }",
        "        return std::unexpected(Error{ErrorCode::UnsupportedPeriodContract,",
        "            \"deterministic runtime probe did not observe structural readiness\"});",
        "    }",
    ])
    return lines


def render(entries: list[dict]) -> str:
    includes = sorted({entry["include"] for entry in entries})
    lines = [
        "#pragma once",
        "// AUTO-GENERATED by tools/generate_indicator_history_contract.py",
        "// DO NOT EDIT MANUALLY.",
        "",
        "#include <stratforge/core/line.hpp>",
        "#include <stratforge/indicators/sma.hpp>",
        "#include <cmath>",
    ]
    lines.extend(f"#include <{include}>" for include in includes)
    lines.extend([
        "",
        "namespace stratforge::indicator_history::detail {",
        "",
        "inline Result resolve_generated(std::string_view identity,",
        "                                const Parameters& parameters,",
        "                                std::string_view accessor) {",
        "    Line<double> open;",
        "    Line<double> high;",
        "    Line<double> low;",
        "    Line<double> close;",
        "    Line<double> volume;",
        "    Line<double> period_line;",
        "",
    ])
    for entry in entries:
        lines.extend(generate_entry(entry))
        lines.append("")
    lines.extend([
        "    return std::unexpected(Error{ErrorCode::UnknownIndicator,",
        "        \"unknown indicator: \" + std::string(identity)});",
        "}",
        "",
        "inline Result aggregate_generated(std::string_view identity,",
        "                                  const Parameters& parameters) {",
        "    Line<double> open;",
        "    Line<double> high;",
        "    Line<double> low;",
        "    Line<double> close;",
        "    Line<double> volume;",
        "    Line<double> period_line;",
        "",
    ])
    for entry in entries:
        lines.extend(generate_aggregate_entry(entry))
        lines.append("")
    lines.extend([
        "    return std::unexpected(Error{ErrorCode::UnknownIndicator,",
        "        \"unknown indicator: \" + std::string(identity)});",
        "}",
        "",
        "inline Result observed_readiness_generated(std::string_view identity,",
        "                                           const Parameters& parameters,",
        "                                           std::string_view accessor) {",
        "    Line<double> open;",
        "    Line<double> high;",
        "    Line<double> low;",
        "    Line<double> close;",
        "    Line<double> volume;",
        "    Line<double> period_line;",
        "",
    ])
    for entry in entries:
        lines.extend(generate_observed_entry(entry))
        lines.append("")
    lines.extend([
        "    return std::unexpected(Error{ErrorCode::UnknownIndicator,",
        "        \"unknown indicator: \" + std::string(identity)});",
        "}",
        "",
        "inline std::span<const IndicatorDescriptor> descriptors_generated() {",
        "    static const std::vector<IndicatorDescriptor> descriptors{",
    ])
    parameter_types = {
        "std::size_t": "ParameterType::Size",
        "int": "ParameterType::Int",
        "double": "ParameterType::Double",
        "bool": "ParameterType::Bool",
    }
    for entry in entries:
        aliases = ", ".join(cpp_string(alias) for alias in entry["aliases"])
        parameters = ", ".join(
            "ParameterDescriptor{" + cpp_string(param["name"]) + ", " +
            parameter_types[param["type"]] + ", " + parameter_variant_expression(param) +
            ", \"runtime constructor normalization\", \"runtime constructor constraints\"}"
            for param in entry["constructor_params"]
        )
        accessors = ", ".join(cpp_string(name) for name in entry["output_accessors"])
        lines.append(
            "        IndicatorDescriptor{" + cpp_string(entry["slug"]) +
            f", {{{aliases}}}, {{{parameters}}}, {{{accessors}}}}},"
        )
    lines.extend([
        "    };",
        "    return descriptors;",
        "}",
        "",
        "} // namespace stratforge::indicator_history::detail",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="fail if the checked-in generated header is stale")
    args = parser.parse_args()
    entries = json.loads(REGISTRY.read_text())
    content = render(entries)
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != content:
            print(f"ERROR: stale generated contract: {OUTPUT}")
            return 1
        print(f"Contract is current for {len(entries)} indicators")
        return 0
    OUTPUT.write_text(content)
    print(f"Generated {OUTPUT} for {len(entries)} indicators")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
