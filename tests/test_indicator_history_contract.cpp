#include <catch2/catch_test_macros.hpp>

#include <stratforge/indicators/history_contract.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <limits>

namespace history = stratforge::indicator_history;

TEST_CASE("indicator history contract resolves accessor-specific periods",
          "[indicator][history_contract]") {
    const auto williams = history::effective_minimum_period(
        "WilliamsR", {{"period", std::uint64_t{14}}}, "line()");
    REQUIRE(williams);
    CHECK(*williams == 14);

    const auto sma = history::effective_minimum_period(
        "SIMPLEMOVINGAVERAGE", {{"period", std::uint64_t{50}}}, "line");
    REQUIRE(sma);
    CHECK(*sma == 50);

    const history::Parameters macd_params{
        {"fast_period", std::uint64_t{12}},
        {"slow_period", std::uint64_t{26}},
        {"signal_period", std::uint64_t{9}},
    };
    const auto macd = history::effective_minimum_period("MACD", macd_params, "macd()");
    const auto signal = history::effective_minimum_period("MACD", macd_params, "signal");
    const auto histogram = history::effective_minimum_period(
        "MACD", macd_params, "histogram()");
    REQUIRE(macd);
    REQUIRE(signal);
    REQUIRE(histogram);
    CHECK(*macd == 26);
    CHECK(*signal == 34);
    CHECK(*histogram == 34);
}

TEST_CASE("indicator history contract follows runtime normalization",
          "[indicator][history_contract]") {
    const auto result = history::effective_minimum_period(
        "SMA", {{"period", std::uint64_t{0}}}, "line");
    REQUIRE(result);
    CHECK(*result == 1);
}

TEST_CASE("indicator history contract distinguishes composite secondary outputs",
          "[indicator][history_contract]") {
    const history::Parameters stochastic_params{
        {"period", std::uint64_t{14}},
        {"period_dfast", std::uint64_t{3}},
        {"period_dslow", std::uint64_t{3}},
    };
    CHECK(*history::effective_minimum_period(
        "StochasticFull", stochastic_params, "percK") == 14);
    CHECK(*history::effective_minimum_period(
        "StochasticFull", stochastic_params, "percD") == 16);
    CHECK(*history::effective_minimum_period(
        "StochasticFull", stochastic_params, "percDSlow") == 18);

    const history::Parameters directional_params{{"period", std::uint64_t{14}}};
    CHECK(*history::effective_minimum_period(
        "DirectionalMovement", directional_params, "plus_di") == 15);
    CHECK(*history::effective_minimum_period(
        "DirectionalMovement", directional_params, "adx") == 28);
    CHECK(*history::effective_minimum_period(
        "DirectionalMovement", directional_params, "adxr") == 42);

    const history::Parameters ichimoku_params{
        {"tenkan", std::uint64_t{9}},
        {"kijun", std::uint64_t{26}},
        {"senkou", std::uint64_t{52}},
        {"senkou_lead", std::uint64_t{26}},
        {"chikou", std::uint64_t{26}},
    };
    CHECK(*history::effective_minimum_period(
        "Ichimoku", ichimoku_params, "tenkan_sen") == 9);
    CHECK(*history::effective_minimum_period(
        "Ichimoku", ichimoku_params, "kijun_sen") == 26);
    CHECK(*history::effective_minimum_period(
        "Ichimoku", ichimoku_params, "senkou_span_a") == 52);
    CHECK(*history::effective_minimum_period(
        "Ichimoku", ichimoku_params, "senkou_span_b") == 78);
}

TEST_CASE("indicator history contract rejects unknown and invalid input",
          "[indicator][history_contract]") {
    const auto unknown_indicator = history::effective_minimum_period(
        "DoesNotExist", {}, "line");
    REQUIRE_FALSE(unknown_indicator);
    CHECK(unknown_indicator.error().code == history::ErrorCode::UnknownIndicator);

    const auto unknown_accessor = history::effective_minimum_period(
        "SMA", {{"period", std::uint64_t{14}}}, "signal");
    REQUIRE_FALSE(unknown_accessor);
    CHECK(unknown_accessor.error().code == history::ErrorCode::UnknownAccessor);

    const auto unknown_parameter = history::effective_minimum_period(
        "SMA", {{"window", std::uint64_t{14}}}, "line");
    REQUIRE_FALSE(unknown_parameter);
    CHECK(unknown_parameter.error().code == history::ErrorCode::InvalidParameter);

    const auto invalid_type = history::effective_minimum_period(
        "SMA", {{"period", true}}, "line");
    REQUIRE_FALSE(invalid_type);
    CHECK(invalid_type.error().code == history::ErrorCode::InvalidParameter);

    const auto large_sma = history::effective_minimum_period(
        "SMA", {{"period", std::uint64_t{std::numeric_limits<std::uint64_t>::max()}}},
        "line");
    REQUIRE(large_sma);
    CHECK(*large_sma == std::numeric_limits<std::size_t>::max());

    const auto composite_overflow = history::effective_minimum_period(
        "MACD",
        {{"fast_period", std::uint64_t{12}},
         {"slow_period", std::uint64_t{std::numeric_limits<std::uint64_t>::max()}},
         {"signal_period", std::uint64_t{9}}},
        "signal");
    REQUIRE_FALSE(composite_overflow);
    CHECK(composite_overflow.error().code == history::ErrorCode::Overflow);
}

TEST_CASE("indicator history contract exposes a stable compatibility identity",
          "[indicator][history_contract]") {
    CHECK(history::contract_version == std::string_view{"1.0.0"});
}

TEST_CASE("indicator history contract checks lookback arithmetic",
          "[indicator][history_contract]") {
    const auto warmup = history::required_warmup(14, 2);
    REQUIRE(warmup);
    CHECK(*warmup == 16);

    const auto overflow = history::required_warmup(
        std::numeric_limits<std::size_t>::max(), 1);
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error().code == history::ErrorCode::Overflow);
}

TEST_CASE("every registered indicator accessor has a resolvable default contract",
          "[indicator][history_contract][drift]") {
    const auto descriptors = history::descriptors();
    REQUIRE(descriptors.size() == 205);

    for (const auto& descriptor : descriptors) {
        history::Parameters parameters;
        for (const auto& parameter : descriptor.parameters) {
            parameters.emplace(std::string(parameter.name), parameter.default_value);
        }
        for (const auto accessor : descriptor.accessors) {
            CAPTURE(descriptor.canonical_identity, accessor);
            const auto result = history::effective_minimum_period(
                descriptor.canonical_identity, parameters, accessor);
            REQUIRE(result);
            CHECK(*result > 0);
        }
        if (descriptor.accessors.size() > 1) {
            const auto aggregate = history::aggregate_minimum_period(
                descriptor.canonical_identity, parameters);
            REQUIRE(aggregate);
            std::size_t maximum_accessor_period = 0;
            for (const auto accessor : descriptor.accessors) {
                const auto result = history::effective_minimum_period(
                    descriptor.canonical_identity, parameters, accessor);
                REQUIRE(result);
                maximum_accessor_period = std::max(maximum_accessor_period, *result);
            }
            CAPTURE(descriptor.canonical_identity, maximum_accessor_period, *aggregate);
            CHECK(maximum_accessor_period == *aggregate);
        }
        for (const auto alias : descriptor.aliases) {
            CAPTURE(descriptor.canonical_identity, alias);
            const auto result = history::effective_minimum_period(
                alias, parameters, descriptor.accessors.front());
            REQUIRE(result);
        }
    }
}

TEST_CASE("every registered accessor matches deterministic runtime readiness",
          "[indicator][history_contract][runtime_drift]") {
    for (const auto& descriptor : history::descriptors()) {
        history::Parameters parameters;
        for (const auto& parameter : descriptor.parameters) {
            parameters.emplace(std::string(parameter.name), parameter.default_value);
        }
        for (const auto accessor : descriptor.accessors) {
            CAPTURE(descriptor.canonical_identity, accessor);
            const auto contract = history::effective_minimum_period(
                descriptor.canonical_identity, parameters, accessor);
            const auto observed = history::detail::observed_readiness_generated(
                descriptor.canonical_identity, parameters, accessor);
            REQUIRE(contract);
            REQUIRE(observed);
            CHECK(*contract == *observed);
        }
    }
}
