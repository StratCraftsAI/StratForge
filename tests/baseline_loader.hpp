// SPDX-License-Identifier: MIT
//
// tests/baseline_loader.hpp — Load performance baselines from baselines.json.
//
// Hand-rolled parser for the fixed JSON schema in benchmarks/baselines.json.
// Avoids introducing a JSON library dependency (nlohmann::json etc.) for a
// ~40-line config file with a stable, known structure.
//
//  Issue 3: single source of truth for performance gate thresholds.

#pragma once

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace stratforge::test {

struct Baseline {
    std::string name;
    std::uint64_t p50_max_ns = 0;
    std::uint64_t p99_max_ns = 0;
    std::uint64_t total_max_ns = 0;
    std::uint64_t per_bar_max_ns = 0;
};

/// Load all baselines from baselines.json.
/// @param source_dir  Value of SF_SOURCE_DIR (project root).
/// @return Vector of parsed baselines.
/// @throws std::runtime_error on file-not-found or parse failure.
[[nodiscard]] inline std::vector<Baseline> load_baselines(const std::string& source_dir) {
    const std::string path = source_dir + "/benchmarks/baselines.json";
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("baseline_loader: cannot open " + path);
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string content = buf.str();

    std::vector<Baseline> baselines;
    // Parse each baseline object from the "baselines" array.
    // Strategy: find each {"name": ... } block inside "baselines": [ ... ].
    const auto baselines_key = content.find("\"baselines\"");
    if (baselines_key == std::string::npos) {
        throw std::runtime_error("baseline_loader: missing \"baselines\" key");
    }

    auto extract_string = [&](const std::string& block, const std::string& key) -> std::string {
        const std::string search = "\"" + key + "\"";
        auto pos = block.find(search);
        if (pos == std::string::npos) return {};
        pos = block.find('\"', pos + search.size());
        if (pos == std::string::npos) return {};
        ++pos;  // skip opening quote
        auto end = block.find('\"', pos);
        if (end == std::string::npos) return {};
        return block.substr(pos, end - pos);
    };

    auto extract_uint64 = [&](const std::string& block, const std::string& key) -> std::uint64_t {
        const std::string search = "\"" + key + "\"";
        auto pos = block.find(search);
        if (pos == std::string::npos) return 0;
        pos = block.find(':', pos + search.size());
        if (pos == std::string::npos) return 0;
        ++pos;
        // Skip whitespace
        while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t')) ++pos;
        std::uint64_t val = 0;
        while (pos < block.size() && block[pos] >= '0' && block[pos] <= '9') {
            val = val * 10 + static_cast<std::uint64_t>(block[pos] - '0');
            ++pos;
        }
        return val;
    };

    // Walk through each object in the baselines array
    std::size_t pos = content.find('[', baselines_key);
    if (pos == std::string::npos) {
        throw std::runtime_error("baseline_loader: missing '[' after baselines key");
    }

    while (true) {
        auto obj_start = content.find('{', pos);
        if (obj_start == std::string::npos) break;
        auto obj_end = content.find('}', obj_start);
        if (obj_end == std::string::npos) break;

        // Check we haven't passed the closing ']' of the baselines array
        auto arr_end = content.find(']', pos);
        if (arr_end != std::string::npos && obj_start > arr_end) break;

        const std::string block = content.substr(obj_start, obj_end - obj_start + 1);

        Baseline b;
        b.name = extract_string(block, "name");
        b.p50_max_ns = extract_uint64(block, "p50_max_ns");
        b.p99_max_ns = extract_uint64(block, "p99_max_ns");
        b.total_max_ns = extract_uint64(block, "total_max_ns");
        b.per_bar_max_ns = extract_uint64(block, "per_bar_max_ns");

        if (!b.name.empty()) {
            baselines.push_back(std::move(b));
        }

        pos = obj_end + 1;
    }

    if (baselines.empty()) {
        throw std::runtime_error("baseline_loader: no baselines found in " + path);
    }
    return baselines;
}

/// Find a baseline by name.
/// @return Baseline if found, std::nullopt otherwise.
[[nodiscard]] inline std::optional<Baseline> find_baseline(
    const std::vector<Baseline>& baselines, const std::string& name) {
    for (const auto& b : baselines) {
        if (b.name == name) return b;
    }
    return std::nullopt;
}

}  // namespace stratforge::test
