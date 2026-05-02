#pragma once

#include <stratforge/analyzers/drawdown.hpp>
#include <stratforge/analyzers/sharpe_ratio.hpp>
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/broker/commission.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/observers/cash_value.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stratforge {

/// Configuration for a backtest run.
struct BacktestConfig {
    std::string data_file;
    double initial_cash = 100000.0;
    double commission = 0.001;
    std::string symbol = "LIVE";
};

/// Minimal JSON config parser for backtest configuration.
/// Extracts string and double fields by key from a flat JSON object.
class BacktestConfigParser {
public:
    explicit BacktestConfigParser(std::string_view json) : json_(json) {}

    [[nodiscard]] std::string get_string(std::string_view key,
                                         std::string_view fallback = "") const {
        const auto pos = find_key(key);
        if (pos == std::string_view::npos) return std::string(fallback);
        const auto quote = json_.find('"', pos);
        if (quote == std::string_view::npos) return std::string(fallback);
        const auto start = quote + 1;
        const auto end = json_.find('"', start);
        if (end == std::string_view::npos) return std::string(fallback);
        return std::string(json_.substr(start, end - start));
    }

    [[nodiscard]] double get_double(std::string_view key,
                                    double fallback = 0.0) const {
        const auto pos = find_key(key);
        if (pos == std::string_view::npos) return fallback;
        const auto begin = json_.find_first_of("-0123456789", pos);
        if (begin == std::string_view::npos) return fallback;
        const auto end = json_.find_first_not_of("-+0123456789.eE", begin);
        try {
            return std::stod(std::string(json_.substr(begin, end - begin)));
        } catch (...) {
            return fallback;
        }
    }

private:
    [[nodiscard]] std::size_t find_key(std::string_view key) const {
        const std::string needle = "\"" + std::string(key) + "\"";
        const auto pos = json_.find(needle);
        if (pos == std::string_view::npos) return std::string_view::npos;
        const auto colon = json_.find(':', pos + needle.size());
        if (colon == std::string_view::npos) return std::string_view::npos;
        return colon + 1;
    }

    std::string_view json_;
};

/// Parse a JSON config string into a BacktestConfig.
[[nodiscard]] inline BacktestConfig parse_backtest_config(std::string_view json) {
    BacktestConfigParser parser(json);
    BacktestConfig cfg;
    cfg.data_file = parser.get_string("data_file");
    if (cfg.data_file.empty()) {
        cfg.data_file = parser.get_string("dataFile");
    }
    cfg.initial_cash = parser.get_double("initial_cash",
                           parser.get_double("initialCash", 100000.0));
    cfg.commission = parser.get_double("commission", 0.001);
    cfg.symbol = parser.get_string("symbol", "LIVE");
    return cfg;
}

/// Run a backtest with an externally-owned strategy and configuration.
///
/// Takes ownership of the Strategy via unique_ptr. For factory-created
/// strategies, the caller should wrap the raw pointer:
///   auto result = run_backtest(
///       std::unique_ptr<Strategy>(factory_ptr), config);
/// The strategy will be destroyed when Cerebro goes out of scope
/// (before dlclose), which is correct for .so-allocated objects.
///
/// Returns the result JSON string.
[[nodiscard]] inline std::string run_backtest(std::unique_ptr<Strategy> strategy,
                                              const BacktestConfig& config) {
    if (!strategy) {
        return R"({"success":false,"errorMessage":"null strategy pointer"})";
    }
    if (config.data_file.empty()) {
        return R"({"success":false,"errorMessage":"data_file not specified"})";
    }

    auto start_time = std::chrono::steady_clock::now();

    auto feed = std::make_unique<CsvData>(CsvData::Params{
        .filename = config.data_file,
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });

    if (!feed->load()) {
        return R"({"success":false,"errorMessage":"failed to load data file"})";
    }

    const std::size_t num_bars = feed->size();
    auto* feed_ptr = feed.get();

    Cerebro cerebro;
    cerebro.set_cash(config.initial_cash);
    cerebro.set_commission(CommissionInfo{.commission = config.commission});
    cerebro.add_data(std::move(feed), config.symbol);
    cerebro.add_strategy(std::move(strategy));

    auto& trade_analyzer = cerebro.add_analyzer<TradeAnalyzer>();
    auto& sharpe = cerebro.add_analyzer<SharpeRatio>();
    auto& drawdown = cerebro.add_analyzer<Drawdown>();
    auto& cash_value = cerebro.add_observer<CashValue>();

    cerebro.run();

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    std::vector<DataFeed*> feed_ptrs = {feed_ptr};
    SerializationContext ctx;
    ctx.broker = &cerebro.broker();
    ctx.data_feeds = &feed_ptrs;
    ctx.trade_analyzer = &trade_analyzer;
    ctx.sharpe_ratio = &sharpe;
    ctx.drawdown = &drawdown;
    ctx.cash_value = &cash_value;
    ctx.initial_cash = config.initial_cash;
    ctx.bars_processed = num_bars;
    ctx.execution_time_ms = elapsed_ms;
    ctx.symbol = config.symbol;

    return serialize_results(ctx);
}

/// Convenience overload: parse JSON config and run backtest.
[[nodiscard]] inline std::string run_backtest(std::unique_ptr<Strategy> strategy,
                                              std::string_view config_json) {
    return run_backtest(std::move(strategy), parse_backtest_config(config_json));
}

} // namespace stratforge
