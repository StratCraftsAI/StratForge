#pragma once

#include <stratforge/analyzers/drawdown.hpp>
#include <stratforge/analyzers/sharpe_ratio.hpp>
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/broker/commission.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/data/interval.hpp>
#include <stratforge/data/resampled_feed.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/observers/cash_value.hpp>
#include <stratforge/observers/increment_batcher.hpp>
#include <stratforge/observers/increment_types.hpp>
#include <stratforge/engine/detail/backtest_result_serializer.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stratforge {

/// Position sizer type for backtest configuration.
enum class SizerType { Fixed, Percent, AllIn };

///  P2: One entry in the multi-timeframe feed plan.
/// Describes how a single data feed should be loaded: either from a parquet
/// file (source == "parquet") or by resampling an already-loaded base feed
/// (source == "resample").
struct FeedSpec {
    int index = 0;
    std::string interval;     ///< canonical vocabulary token (e.g. "1h", "1M")
    std::string role;         ///< "execution" or "context"
    std::string source;       ///< "parquet" or "resample"
    std::string data_path;    ///< for source=="parquet": absolute path to data file
    std::string base;         ///< for source=="resample": the base interval token
};

/// Configuration for a backtest run.
struct BacktestConfig {
    std::string data_file;
    double initial_cash = 100000.0;
    double commission = 0.001;
    std::string symbol = "LIVE";

    SizerType sizer_type = SizerType::Percent;
    double sizer_param = 100.0;
    int symbol_count = 1;

    ///  G1: window pushdown parameters.
    /// When set, passed through to ParquetData/CsvData fromdate/todate so the
    /// data loader reads only the requested window (plus warmup extension)
    /// instead of the full file. Epoch-seconds (UTC).
    std::optional<double> start_time;
    std::optional<double> end_time;

    ///  P2: multi-timeframe feed plan.
    /// When populated (from a "feeds" JSON array in the config), the runner
    /// loads each spec in index order instead of using data_file directly.
    /// When empty (legacy config), a single-feed plan is synthesized from
    /// data_file for back-compat (AC5).
    std::vector<FeedSpec> feeds;

};

/// Minimal JSON config parser for backtest configuration.
/// Extracts string and double fields by key from a flat JSON object.
///
///  P2: extended with parse_feeds() — a contained
/// array-of-flat-objects scanner for the "feeds" key. The scanner locates
/// the top-level "feeds" array bracket, then iterates over each `{...}`
/// element, applying the existing get_string/get_double on its substring.
/// No external JSON dependency.
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

    ///  P2: parse the "feeds" JSON array into a vector of FeedSpec.
    /// Returns an empty vector if the key is absent (legacy config).
    /// Each element is a flat JSON object with keys:
    ///   index (int), interval (string), role (string),
    ///   source (string), dataPath (string), base (string).
    [[nodiscard]] std::vector<FeedSpec> parse_feeds() const {
        std::vector<FeedSpec> result;

        // Locate "feeds" key and the opening '['.
        const auto key_pos = find_key("feeds");
        if (key_pos == std::string_view::npos) return result;

        const auto bracket_open = json_.find('[', key_pos);
        if (bracket_open == std::string_view::npos) return result;

        // Find matching ']', accounting for nested brackets (though
        // feed objects have none; defensive).
        int depth = 1;
        std::size_t pos = bracket_open + 1;
        std::size_t bracket_close = std::string_view::npos;
        while (pos < json_.size() && depth > 0) {
            if (json_[pos] == '[') ++depth;
            else if (json_[pos] == ']') --depth;
            if (depth == 0) {
                bracket_close = pos;
                break;
            }
            ++pos;
        }
        if (bracket_close == std::string_view::npos) return result;

        // Iterate over each {...} element within the array.
        pos = bracket_open + 1;
        while (pos < bracket_close) {
            const auto obj_open = json_.find('{', pos);
            if (obj_open == std::string_view::npos || obj_open >= bracket_close) break;

            // Find matching '}'.
            int obj_depth = 1;
            std::size_t obj_pos = obj_open + 1;
            std::size_t obj_close = std::string_view::npos;
            while (obj_pos < bracket_close && obj_depth > 0) {
                if (json_[obj_pos] == '{') ++obj_depth;
                else if (json_[obj_pos] == '}') --obj_depth;
                if (obj_depth == 0) {
                    obj_close = obj_pos;
                    break;
                }
                ++obj_pos;
            }
            if (obj_close == std::string_view::npos) break;

            // Parse the sub-object using a scoped parser.
            const auto obj_json = json_.substr(obj_open, obj_close - obj_open + 1);
            BacktestConfigParser obj_parser(obj_json);

            FeedSpec spec;
            spec.index = static_cast<int>(obj_parser.get_double("index", 0.0));
            spec.interval = obj_parser.get_string("interval");
            spec.role = obj_parser.get_string("role");
            spec.source = obj_parser.get_string("source");
            spec.data_path = obj_parser.get_string("dataPath");
            spec.base = obj_parser.get_string("base");
            result.push_back(std::move(spec));

            pos = obj_close + 1;
        }

        return result;
    }

private:
    /// : structural key search -- match only keys of THIS
    /// parser's top-level object (depth 1), skipping string contents and
    /// nested objects/arrays. The previous plain substring find matched the
    /// first occurrence anywhere in the document, so a nested duplicate
    /// (e.g. feedPlan.feeds inside the app config) shadowed the top-level
    /// "feeds" array and parse_feeds read source="kind" from the nested
    /// object shape.
    [[nodiscard]] std::size_t find_key(std::string_view key) const {
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        std::size_t str_start = std::string_view::npos;
        for (std::size_t i = 0; i < json_.size(); ++i) {
            const char c = json_[i];
            if (in_string) {
                if (escape) {
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == '"') {
                    in_string = false;
                    if (depth == 1) {
                        const auto token = json_.substr(str_start, i - str_start);
                        const auto after =
                            json_.find_first_not_of(" \t\r\n", i + 1);
                        if (after != std::string_view::npos &&
                            json_[after] == ':' && token == key) {
                            return after + 1;
                        }
                    }
                }
                continue;
            }
            switch (c) {
                case '"': in_string = true; str_start = i + 1; break;
                case '{': case '[': ++depth; break;
                case '}': case ']': --depth; break;
                default: break;
            }
        }
        return std::string_view::npos;
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

    const auto sizer_str = parser.get_string("sizer_type", "percent");
    if (sizer_str == "fixed") {
        cfg.sizer_type = SizerType::Fixed;
    } else if (sizer_str == "allin") {
        cfg.sizer_type = SizerType::AllIn;
    } else {
        cfg.sizer_type = SizerType::Percent;
    }
    cfg.sizer_param = parser.get_double("sizer_param", 100.0);
    cfg.symbol_count = static_cast<int>(parser.get_double("symbol_count", 1.0));
    if (cfg.symbol_count < 1) cfg.symbol_count = 1;

    //  G1: window pushdown epoch-seconds.
    const double st = parser.get_double("start_time", -1.0);
    if (st >= 0.0) cfg.start_time = st;
    const double et = parser.get_double("end_time", -1.0);
    if (et >= 0.0) cfg.end_time = et;

    //  P2: multi-timeframe feed plan.
    cfg.feeds = parser.parse_feeds();

    return cfg;
}

///  P2: helper to build a JSON error result string.
[[nodiscard]] inline std::string make_error_result(std::string_view msg) {
    std::string escaped;
    escaped.reserve(msg.size());
    for (char ch : msg) {
        if (ch == '"') escaped += "\\\"";
        else if (ch == '\\') escaped += "\\\\";
        else if (ch == '\n') escaped += "\\n";
        else escaped += ch;
    }
    return R"({"success":false,"errorMessage":")" + escaped + R"("})";
}

///  P2: validate a feed plan (fail-fast, ).
/// Returns an error string on validation failure, or empty on success.
[[nodiscard]] inline std::string validate_feed_plan(
    const std::vector<FeedSpec>& feeds) {

    if (feeds.empty()) {
        return "feed plan is empty: at least one feed is required";
    }

    // Feed 0 must have role == "execution".
    if (feeds[0].role != "execution") {
        return "feed 0 must have role \"execution\", got \"" + feeds[0].role + "\"";
    }

    // Validate all intervals are canonical and resolve feed 0's TFC.
    auto exec_tfc_result = parse_interval(feeds[0].interval);
    if (!exec_tfc_result.has_value()) {
        return "feed 0 has invalid interval token \"" + feeds[0].interval + "\"";
    }
    const auto exec_tfc = *exec_tfc_result;

    // Feed 0 must be strictly finest (no context feed finer than feed 0).
    for (std::size_t k = 1; k < feeds.size(); ++k) {
        auto ctx_tfc_result = parse_interval(feeds[k].interval);
        if (!ctx_tfc_result.has_value()) {
            return "feed " + std::to_string(k) +
                   " has invalid interval token \"" + feeds[k].interval + "\"";
        }
        const auto ctx_tfc = *ctx_tfc_result;
        if (finer_than(ctx_tfc, exec_tfc)) {
            return "feed " + std::to_string(k) + " (interval " +
                   feeds[k].interval +
                   ") is finer than feed 0 (execution, interval " +
                   feeds[0].interval +
                   "); the execution feed must be the finest";
        }
    }

    // Validate parquet feeds have non-empty data_path and that file exists.
    for (std::size_t k = 0; k < feeds.size(); ++k) {
        if (feeds[k].source == "parquet") {
            if (feeds[k].data_path.empty()) {
                return "feed " + std::to_string(k) +
                       " (source=parquet) has no dataPath";
            }
            if (!std::filesystem::exists(feeds[k].data_path)) {
                return "feed " + std::to_string(k) +
                       " data file not found: " + feeds[k].data_path;
            }
        } else if (feeds[k].source == "resample") {
            if (feeds[k].base.empty()) {
                return "feed " + std::to_string(k) +
                       " (source=resample) has no base interval";
            }
            // Validate the base interval is canonical.
            auto base_result = parse_interval(feeds[k].base);
            if (!base_result.has_value()) {
                return "feed " + std::to_string(k) +
                       " has invalid base interval token \"" + feeds[k].base + "\"";
            }
        } else {
            return "feed " + std::to_string(k) +
                   " has unknown source \"" + feeds[k].source + "\"";
        }
    }

    return {};
}

///  P2: load a single parquet or CSV file into a DataFeed.
/// Applies window pushdown (fromdate/todate) and sets the feed's timeframe
/// from the canonical interval token.
[[nodiscard]] inline std::unique_ptr<DataFeed> load_native_feed(
    const FeedSpec& spec,
    std::optional<DateTime> from_dt,
    std::optional<DateTime> to_dt,
    std::string& error_out) {

    const auto& path = spec.data_path;
    const bool is_parquet = path.size() >= 8 &&
        path.compare(path.size() - 8, 8, ".parquet") == 0;

    std::unique_ptr<DataFeed> feed;
    if (is_parquet) {
    } else {
        auto csv_feed = std::make_unique<CsvData>(CsvData::Params{
            .filename = path,
            .columns = {},
            .date_format = "%Y-%m-%d",
            .separator = ',',
            .has_headers = true,
            .fromdate = from_dt,
            .todate = to_dt,
        });
        if (!csv_feed->load()) {
            error_out = "failed to load data file for feed " +
                        std::to_string(spec.index) + ": " + path;
            return nullptr;
        }
        feed = std::move(csv_feed);
    }

    // Set the feed's timeframe from the canonical interval.
    auto tfc_result = parse_interval(spec.interval);
    if (tfc_result.has_value()) {
        feed->set_timeframe(*tfc_result);
    }

    return feed;
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
/// If `on_increment` is non-null, an IncrementBatcher observer is attached
/// to the internal Cerebro and the callback receives one IncrementSnapshot
/// per flush (per the -B contract: bar-count OR wall-clock
/// threshold, plus one terminal is_final flush). Out-of-process callers
/// (e.g. stratforge-runner's [INCREMENT_V2] / [FINAL_SEQ] streaming
/// protocol,  §4) attach a JSON-emitting lambda here.
///
///  P2: supports multi-timeframe feed plans. When config.feeds
/// is populated, loads each spec in plan index order (native parquet or
/// resampled from a base feed). When config.feeds is empty, falls back to
/// single-feed loading from config.data_file (back-compat, AC5).
///
/// Returns the result JSON string.
[[nodiscard]] inline std::string run_backtest(std::unique_ptr<Strategy> strategy,
                                              const BacktestConfig& config,
                                              IncrementBatcher::FlushCallback on_increment = nullptr) {
    if (!strategy) {
        return make_error_result("null strategy pointer");
    }

    // -------------------------------------------------------------------
    //  P2: back-compat synth plan.
    // When feeds[] is absent but data_file exists, synthesize a single-feed
    // plan so the rest of the load loop is uniform.
    // -------------------------------------------------------------------
    BacktestConfig cfg = config; // mutable copy for synth
    if (cfg.feeds.empty()) {
        if (cfg.data_file.empty()) {
            return make_error_result("data_file not specified");
        }
        FeedSpec synth;
        synth.index = 0;
        // Infer interval from the existing parser's "interval" field (may be
        // absent in legacy configs; default to empty which skips set_timeframe).
        // For legacy single-feed configs the interval is not critical — Cerebro
        // skips the multi-clock validation for single-feed plans.
        synth.interval = ""; // legacy: no interval in old configs
        synth.role = "execution";
        synth.source = "parquet"; // load_native_feed handles csv extension too
        synth.data_path = cfg.data_file;
        cfg.feeds = {synth};
    }

    // -------------------------------------------------------------------
    // Validate the feed plan (fail-fast, ).
    // Skip full validation for synthesized single-feed plans with empty
    // interval (legacy configs).
    // -------------------------------------------------------------------
    if (!(cfg.feeds.size() == 1 && cfg.feeds[0].interval.empty())) {
        const auto validation_error = validate_feed_plan(cfg.feeds);
        if (!validation_error.empty()) {
            return make_error_result(validation_error);
        }
    }

    auto wall_start = std::chrono::steady_clock::now();

    // -------------------------------------------------------------------
    //  G1 + P2: convert config epoch-seconds to DateTime for
    // window pushdown. Extend start backward by the coarsest feed's warmup
    // span so warmup bars are present (NO FULL-HISTORY READ, ).
    // -------------------------------------------------------------------
    std::optional<DateTime> from_dt;
    std::optional<DateTime> to_dt;
    if (cfg.start_time.has_value()) {
        from_dt = DateTime(std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(*cfg.start_time)));
    }
    if (cfg.end_time.has_value()) {
        to_dt = DateTime(std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(*cfg.end_time)));
    }

    // Find the coarsest feed's TFC for warmup window extension.
    // The default warmup_bars is conservative; strategies may override via
    // set_minimum_period but we need extra bars at load time. Use a
    // reasonable default (e.g. 50 bars of the coarsest TF) to ensure
    // the warmup window is covered. The engine will validate feasibility.
    constexpr int kDefaultWarmupBars = 50;
    if (from_dt.has_value() && cfg.feeds.size() > 1) {
        // Find the coarsest feed in the plan.
        TimeFrameCompression coarsest_tfc{TimeFrame::Minutes, 1};
        bool have_coarsest = false;
        for (const auto& spec : cfg.feeds) {
            auto tfc_r = parse_interval(spec.interval);
            if (tfc_r.has_value()) {
                if (!have_coarsest || finer_than(coarsest_tfc, *tfc_r)) {
                    coarsest_tfc = *tfc_r;
                    have_coarsest = true;
                }
            }
        }
        if (have_coarsest) {
            from_dt = advance_periods(*from_dt, coarsest_tfc,
                                      -kDefaultWarmupBars);
        }
    }

    // -------------------------------------------------------------------
    // Load feeds in plan index order.
    // -------------------------------------------------------------------

    // Sort feeds by index to ensure correct add_data order.
    auto sorted_feeds = cfg.feeds;
    std::sort(sorted_feeds.begin(), sorted_feeds.end(),
              [](const FeedSpec& a, const FeedSpec& b) {
                  return a.index < b.index;
              });

    // Storage for loaded feeds — need stable pointers for ResampledFeed.
    std::vector<std::unique_ptr<DataFeed>> loaded_feeds;
    loaded_feeds.reserve(sorted_feeds.size());

    // Also keep ResampledFeed objects alive (they hold const& to base).
    // ResampledFeed objects are owned by loaded_feeds after preload, but
    // during construction they reference a base feed by const&, so the
    // base must remain valid. Since loaded_feeds stores them in order
    // and the base feed (by interval matching) is always loaded before
    // the resample feed (because the base is always finer = lower index),
    // the reference is valid.

    // Map from interval token to index in loaded_feeds for resample base lookup.
    std::vector<std::pair<std::string, std::size_t>> interval_to_loaded_idx;

    for (const auto& spec : sorted_feeds) {
        if (spec.source == "parquet") {
            std::string load_error;
            auto feed = load_native_feed(spec, from_dt, to_dt, load_error);
            if (!feed) {
                return make_error_result(load_error);
            }
            interval_to_loaded_idx.push_back({spec.interval, loaded_feeds.size()});
            loaded_feeds.push_back(std::move(feed));
        } else if (spec.source == "resample") {
            // Find the base feed by matching its interval to spec.base.
            const DataFeed* base_feed = nullptr;
            for (const auto& [itv, idx] : interval_to_loaded_idx) {
                if (itv == spec.base) {
                    base_feed = loaded_feeds[idx].get();
                    break;
                }
            }
            if (!base_feed) {
                return make_error_result(
                    "feed " + std::to_string(spec.index) +
                    " (source=resample) references base interval \"" +
                    spec.base + "\" which is not loaded");
            }

            auto target_tfc = parse_interval(spec.interval);
            if (!target_tfc.has_value()) {
                return make_error_result(
                    "feed " + std::to_string(spec.index) +
                    " has invalid interval \"" + spec.interval + "\"");
            }

            auto resampled = std::make_unique<ResampledFeed>(*base_feed, *target_tfc);
            resampled->preload();
            interval_to_loaded_idx.push_back({spec.interval, loaded_feeds.size()});
            loaded_feeds.push_back(std::move(resampled));
        } else {
            // Legacy single-feed synth plan with empty source is handled
            // as parquet (load_native_feed detects csv by extension).
            std::string load_error;
            auto feed = load_native_feed(spec, from_dt, to_dt, load_error);
            if (!feed) {
                return make_error_result(load_error);
            }
            loaded_feeds.push_back(std::move(feed));
        }
    }

    if (loaded_feeds.empty()) {
        return make_error_result("no feeds loaded");
    }

    const std::size_t num_bars = loaded_feeds[0]->size();

    // -------------------------------------------------------------------
    // Build Cerebro, add feeds, strategy, analyzers, observers.
    // -------------------------------------------------------------------
    Cerebro cerebro;
    cerebro.set_cash(cfg.initial_cash);
    cerebro.set_commission(CommissionInfo{.commission = cfg.commission});

    switch (cfg.sizer_type) {
    case SizerType::Fixed:
        strategy->setsizer(std::make_unique<FixedSize>(cfg.sizer_param));
        break;
    case SizerType::AllIn:
        strategy->setsizer(std::make_unique<AllInSizer>());
        break;
    case SizerType::Percent:
    default: {
        const double per_symbol_pct = cfg.sizer_param /
                                      static_cast<double>(cfg.symbol_count);
        strategy->setsizer(std::make_unique<PercentSizer>(per_symbol_pct));
        break;
    }
    }

    // Collect feed pointers for serialization (before ownership transfers).
    std::vector<DataFeed*> feed_ptrs;
    feed_ptrs.reserve(loaded_feeds.size());
    for (auto& f : loaded_feeds) {
        feed_ptrs.push_back(f.get());
    }

    // Feed 0 gets the symbol name; context feeds get symbol + "_" + interval.
    for (std::size_t i = 0; i < loaded_feeds.size(); ++i) {
        std::string feed_name = (i == 0)
            ? cfg.symbol
            : (cfg.symbol + "_" + sorted_feeds[i].interval);
        cerebro.add_data(std::move(loaded_feeds[i]), std::move(feed_name));
    }

    cerebro.add_strategy(std::move(strategy));

    auto& trade_analyzer = cerebro.add_analyzer<TradeAnalyzer>();
    auto& sharpe = cerebro.add_analyzer<SharpeRatio>();
    auto& drawdown = cerebro.add_analyzer<Drawdown>();
    auto& cash_value = cerebro.add_observer<CashValue>();

    if (on_increment) {
        cerebro.add_observer(std::make_unique<IncrementBatcher>(
            IncrementBatcher::Config{}, std::move(on_increment)));
    }

    // Run backtest. Catch warmup-infeasibility throws from Cerebro
    // and format them into structured error results.
    try {
        cerebro.run();
    } catch (const std::invalid_argument& e) {
        return make_error_result(e.what());
    }

    auto wall_end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        wall_end - wall_start).count();

    // Serialization context uses feed 0 only (execution feed).
    std::vector<DataFeed*> exec_feed_ptrs = {feed_ptrs[0]};
    SerializationContext ctx;
    ctx.broker = &cerebro.broker();
    ctx.data_feeds = &exec_feed_ptrs;
    ctx.trade_analyzer = &trade_analyzer;
    ctx.sharpe_ratio = &sharpe;
    ctx.drawdown = &drawdown;
    ctx.cash_value = &cash_value;
    ctx.initial_cash = cfg.initial_cash;
    ctx.bars_processed = num_bars;
    ctx.execution_time_ms = elapsed_ms;
    ctx.symbol = cfg.symbol;

    //  P5: warmup end timestamp and per-feed bar counts.
    ctx.warmup_end_timestamp_ms = cerebro.warmup_end_timestamp_ms();
    const auto& delivered = cerebro.final_bars_delivered();
    ctx.feed_bar_counts.assign(delivered.begin(), delivered.end());
    ctx.feed_intervals.reserve(sorted_feeds.size());
    for (const auto& spec : sorted_feeds) {
        ctx.feed_intervals.push_back(spec.interval);
    }

    return serialize_results(ctx);
}

/// Convenience overload: parse JSON config and run backtest.
[[nodiscard]] inline std::string run_backtest(std::unique_ptr<Strategy> strategy,
                                              std::string_view config_json,
                                              IncrementBatcher::FlushCallback on_increment = nullptr) {
    return run_backtest(std::move(strategy),
                        parse_backtest_config(config_json),
                        std::move(on_increment));
}

} // namespace stratforge
