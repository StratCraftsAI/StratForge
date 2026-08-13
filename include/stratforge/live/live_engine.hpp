#pragma once

#include <stratforge/broker/broker.hpp>
#include <stratforge/live/live_data_feed.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace stratforge {

struct LiveRiskLimits {
    std::optional<double> max_abs_position;
    std::optional<double> max_drawdown_percent;
    std::optional<std::size_t> max_orders_per_second;
    std::optional<std::chrono::milliseconds> heartbeat_timeout;
    bool flatten_on_breach = true;
    bool auto_reconnect = false;
    std::size_t max_reconnect_attempts = 3;
    std::chrono::milliseconds reconnect_interval{1000};
};

/// Event-driven live engine that wires a strategy, live data feeds, and a connector.
class LiveEngine {
public:
    using StrategyDeleter = std::function<void(Strategy*)>;
    using StrategyInstance = std::unique_ptr<Strategy, StrategyDeleter>;
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    LiveEngine(std::shared_ptr<MarketConnector> connector,
               StrategyInstance strategy,
               LiveRiskLimits limits = {})
        : connector_(std::move(connector)),
          strategy_(std::move(strategy)),
          limits_(limits),
          callback_gate_(std::make_shared<CallbackGate>()) {
        if (!connector_) {
            throw std::invalid_argument("LiveEngine requires a connector");
        }
        if (!strategy_) {
            throw std::invalid_argument("LiveEngine requires a strategy");
        }
        callback_gate_->owner = this;
    }

    LiveEngine(std::shared_ptr<MarketConnector> connector,
               std::unique_ptr<Strategy> strategy,
               LiveRiskLimits limits = {})
        : LiveEngine(std::move(connector),
                     StrategyInstance(strategy.release(), [](Strategy* value) { delete value; }),
                     limits) {}

    ~LiveEngine() {
        stop();
        deactivate_callbacks();
    }

    LiveEngine(const LiveEngine&) = delete;
    LiveEngine& operator=(const LiveEngine&) = delete;

    void set_cash(double cash) {
        broker_.set_cash(cash);
    }

    void set_commission(CommissionInfo info) {
        broker_.set_commission(info);
    }

    void subscribe(std::string symbol) {
        auto [it, inserted] = feeds_.try_emplace(symbol, std::make_unique<LiveDataFeed>());
        if (inserted) {
            it->second->set_name(symbol);
            feed_names_.push_back(symbol);
            rebuild_feed_cache();
            strategy_->set_data_feeds(feed_ptrs());
            broker_.set_live_symbols(symbol_names());
        }

        if (started_) {
            connector_->subscribe_market_data(symbol);
        }
    }

    void start() {
        if (started_) {
            return;
        }

        rate_limit_wrapper_ = std::make_unique<RateLimitWrapper>(*this);
        broker_.set_live_connector(rate_limit_wrapper_.get());
        broker_.set_live_symbols(symbol_names());
        broker_.set_order_notify([this](const Order& order) {
            if (order_callback_) {
                order_callback_(order);
            }
        });
        broker_.set_trade_notify([this](const Trade& trade, double original_size) {
            if (trade_callback_) {
                trade_callback_(trade, original_size);
            }
        });

        strategy_->set_broker(&broker_);
        strategy_->set_data_feeds(feed_ptrs());
        strategy_->ensure_params_initialized();
        strategy_->init();
        strategy_->start();

        {
            std::lock_guard lock(callback_gate_->mutex);
            callback_gate_->active = true;
        }
        const std::weak_ptr<CallbackGate> weak_gate = callback_gate_;
        connector_->on_market_data([weak_gate](const MarketDataSnapshot& snapshot) {
            dispatch(weak_gate, [&](LiveEngine& engine) { engine.handle_market_data(snapshot); });
        });
        connector_->on_fill([weak_gate](const Trade& trade) {
            dispatch(weak_gate, [&](LiveEngine& engine) { engine.handle_fill(trade); });
        });
        connector_->on_state([weak_gate](ConnectorState state) {
            dispatch(weak_gate, [&](LiveEngine& engine) { engine.handle_connector_state(state); });
        });

        connector_->connect();
        for (const auto& symbol : feed_names_) {
            connector_->subscribe_market_data(symbol);
        }

        last_market_data_time_ = Clock::now();
        started_ = true;
    }

    void stop() {
        const auto gate = callback_gate_;
        std::lock_guard lock(gate->mutex);
        if (!started_) return;

        if (limits_.flatten_on_breach) {
            flatten_positions();
        }

        strategy_->stop();
        connector_->on_market_data({});
        connector_->on_fill({});
        connector_->on_state({});
        connector_->disconnect();
        started_ = false;
        gate->active = false;
    }

    /// Replace the strategy without exposing compilation or dynamic loading.
    /// An owning layer may create the replacement by any private mechanism.
    void replace_strategy(StrategyInstance new_strategy) {
        if (!started_) {
            throw std::logic_error("cannot reload strategy on a stopped engine");
        }
        if (!new_strategy) {
            throw std::invalid_argument("replacement strategy must not be null");
        }

        strategy_->stop();

        new_strategy->set_broker(&broker_);
        new_strategy->set_data_feeds(feed_ptrs());
        new_strategy->ensure_params_initialized();
        new_strategy->init();
        new_strategy->start();

        strategy_ = std::move(new_strategy);

        // Reset next-bar tracking so the new strategy gets a clean nextstart
        nextstart_fired_ = false;
    }

    /// Check heartbeat and trigger kill switch if stale.
    /// Call this periodically from your event loop / timer thread.
    void check_heartbeat(TimePoint now = Clock::now()) {
        if (!started_ || kill_switch_triggered_ || !limits_.heartbeat_timeout) {
            return;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_market_data_time_);
        if (elapsed >= *limits_.heartbeat_timeout) {
            trigger_kill_switch("heartbeat timeout");
        }
    }

    /// Attempt reconnection. Returns true if reconnect was initiated.
    bool try_reconnect() {
        if (!started_ || !limits_.auto_reconnect) {
            return false;
        }
        if (reconnect_attempts_ >= limits_.max_reconnect_attempts) {
            trigger_kill_switch("max reconnect attempts exceeded");
            return false;
        }

        ++reconnect_attempts_;
        connector_->connect();
        for (const auto& symbol : feed_names_) {
            connector_->subscribe_market_data(symbol);
        }
        return true;
    }

    [[nodiscard]] std::size_t reconnect_attempts() const noexcept {
        return reconnect_attempts_;
    }

    void on_state(StateCallback callback) {
        state_callback_ = std::move(callback);
    }

    void on_order(OrderNotifyFn callback) {
        order_callback_ = std::move(callback);
    }

    void on_trade(TradeNotifyFn callback) {
        trade_callback_ = std::move(callback);
    }

    [[nodiscard]] BackBroker& broker() noexcept {
        return broker_;
    }

    [[nodiscard]] const BackBroker& broker() const noexcept {
        return broker_;
    }

    [[nodiscard]] Strategy& strategy() noexcept {
        return *strategy_;
    }

    [[nodiscard]] const Strategy& strategy() const noexcept {
        return *strategy_;
    }

    [[nodiscard]] bool started() const noexcept {
        return started_;
    }

    [[nodiscard]] bool kill_switch_triggered() const noexcept {
        return kill_switch_triggered_;
    }

    [[nodiscard]] const std::string& kill_switch_reason() const noexcept {
        return kill_switch_reason_;
    }

private:
    struct CallbackGate {
        std::recursive_mutex mutex;
        LiveEngine* owner = nullptr;
        bool active = false;
    };

    template <typename Callback>
    static void dispatch(const std::weak_ptr<CallbackGate>& weak_gate,
                         Callback&& callback) {
        const auto gate = weak_gate.lock();
        if (!gate) return;
        std::lock_guard lock(gate->mutex);
        if (gate->active && gate->owner) {
            callback(*gate->owner);
        }
    }

    void deactivate_callbacks() noexcept {
        const auto gate = callback_gate_;
        if (!gate) return;
        std::lock_guard lock(gate->mutex);
        gate->active = false;
        gate->owner = nullptr;
    }

    /// Rate-limiting connector wrapper that intercepts order submissions.
    class RateLimitWrapper final : public MarketConnector {
    public:
        explicit RateLimitWrapper(LiveEngine& engine) : engine_(engine) {}

        void connect() override { engine_.connector_->connect(); }
        void disconnect() override { engine_.connector_->disconnect(); }

        void submit_order(const Order& order) override {
            if (engine_.check_order_rate()) {
                engine_.connector_->submit_order(order);
            }
        }

        void cancel_order(OrderId id) override { engine_.connector_->cancel_order(id); }
        void subscribe_market_data(std::string_view symbol) override {
            engine_.connector_->subscribe_market_data(symbol);
        }

        void on_fill(FillCallback callback) override { engine_.connector_->on_fill(std::move(callback)); }
        void on_market_data(MarketDataCallback callback) override {
            engine_.connector_->on_market_data(std::move(callback));
        }
        void on_state(StateCallback callback) override { engine_.connector_->on_state(std::move(callback)); }

    private:
        LiveEngine& engine_;
    };

    /// Returns true if order is within rate limits, false (and triggers kill switch) otherwise.
    bool check_order_rate() {
        if (!limits_.max_orders_per_second) {
            return true;
        }
        if (kill_switch_triggered_) {
            // Allow flatten orders through even after kill switch
            return flattening_;
        }

        const auto now = Clock::now();
        const auto window = std::chrono::seconds(1);

        // Purge stale timestamps
        while (!order_timestamps_.empty() && (now - order_timestamps_.front()) > window) {
            order_timestamps_.pop_front();
        }

        if (order_timestamps_.size() >= *limits_.max_orders_per_second) {
            trigger_kill_switch("order rate limit exceeded");
            return false;
        }

        order_timestamps_.push_back(now);
        return true;
    }

    void handle_market_data(const MarketDataSnapshot& snapshot) {
        last_market_data_time_ = Clock::now();
        reconnect_attempts_ = 0;

        auto& feed = ensure_feed(snapshot.symbol);
        feed.push_snapshot(snapshot);
        broker_.set_bar_index(feed.bars_pushed() - 1);
        strategy_->set_data_feeds(feed_ptrs());

        const auto feed_index = index_of_symbol(snapshot.symbol);
        std::vector<bool> advanced(feed_cache_.size(), false);
        advanced[feed_index] = true;
        std::vector<std::size_t> delivered;
        delivered.reserve(feed_cache_.size());
        for (const auto* current_feed : feed_cache_) {
            delivered.push_back(current_feed->bars_pushed());
        }
        strategy_->set_feed_advanced(std::move(advanced));
        strategy_->set_bars_delivered(delivered);

        bool all_ready = true;
        for (std::size_t index = 0; index < delivered.size(); ++index) {
            if (delivered[index] < strategy_->minimum_period(index)) {
                all_ready = false;
                break;
            }
        }

        // Feed 0 is the execution clock. Secondary callbacks still advance
        // their generated indicators, but never run business decisions.
        if (feed_index != 0 || !all_ready) {
            strategy_->prenext();
        } else if (!nextstart_fired_) {
            strategy_->nextstart();
            nextstart_fired_ = true;
        } else {
            strategy_->next();
        }
        evaluate_risk_limits();
    }

    void handle_fill(const Trade& trade) {
        broker_.record_live_fill(trade);
        evaluate_risk_limits();
    }

    void handle_connector_state(ConnectorState state) {
        if (state_callback_) {
            state_callback_(state);
        }

        if (state == ConnectorState::Disconnected && started_ && !kill_switch_triggered_) {
            if (limits_.auto_reconnect) {
                try_reconnect();
            } else {
                trigger_kill_switch("connector disconnected");
            }
        }
    }

    LiveDataFeed& ensure_feed(const std::string& symbol) {
        auto [it, inserted] = feeds_.try_emplace(symbol, std::make_unique<LiveDataFeed>());
        if (inserted) {
            it->second->set_name(symbol);
            feed_names_.push_back(symbol);
            rebuild_feed_cache();
            broker_.set_live_symbols(symbol_names());
            if (started_) {
                connector_->subscribe_market_data(symbol);
            }
        }
        return *it->second;
    }

    void rebuild_feed_cache() {
        feed_cache_.clear();
        feed_cache_.reserve(feeds_.size());
        for (const auto& symbol : feed_names_) {
            auto it = feeds_.find(symbol);
            if (it != feeds_.end()) {
                feed_cache_.push_back(it->second.get());
            }
        }
    }

    [[nodiscard]] std::size_t index_of_symbol(std::string_view symbol) const {
        const auto it = std::find(feed_names_.begin(), feed_names_.end(), symbol);
        if (it == feed_names_.end()) {
            throw std::logic_error("live feed cache is inconsistent");
        }
        return static_cast<std::size_t>(std::distance(feed_names_.begin(), it));
    }

    [[nodiscard]] std::vector<DataFeed*> feed_ptrs() const {
        std::vector<DataFeed*> feeds;
        feeds.reserve(feed_cache_.size());
        for (auto* feed : feed_cache_) {
            feeds.push_back(feed);
        }
        return feeds;
    }

    [[nodiscard]] std::vector<std::string> symbol_names() const {
        return feed_names_;
    }

    void evaluate_risk_limits() {
        if (kill_switch_triggered_) {
            return;
        }

        const auto feeds = feed_ptrs();
        const double value = broker_.portfolio_value(feeds);
        if (value > peak_portfolio_value_) {
            peak_portfolio_value_ = value;
        }

        if (limits_.max_drawdown_percent && peak_portfolio_value_ > 0.0) {
            const double drawdown = (peak_portfolio_value_ - value) / peak_portfolio_value_ * 100.0;
            if (drawdown >= *limits_.max_drawdown_percent) {
                trigger_kill_switch("max drawdown breached");
                return;
            }
        }

        if (limits_.max_abs_position) {
            for (std::size_t i = 0; i < feeds.size(); ++i) {
                if (std::abs(broker_.position(i).size) > *limits_.max_abs_position) {
                    trigger_kill_switch("max position breached");
                    return;
                }
            }
        }
    }

    void trigger_kill_switch(std::string reason) {
        if (kill_switch_triggered_) {
            return;
        }
        kill_switch_triggered_ = true;
        kill_switch_reason_ = std::move(reason);

        if (limits_.flatten_on_breach) {
            flatten_positions();
        }
    }

    void flatten_positions() {
        flattening_ = true;
        for (std::size_t data_index = 0; data_index < feed_cache_.size(); ++data_index) {
            const auto& position = broker_.position(data_index);
            if (!position.is_flat()) {
                static_cast<void>(broker_.close(data_index));
            }
        }
        flattening_ = false;
    }

    std::shared_ptr<MarketConnector> connector_;
    StrategyInstance strategy_;
    LiveRiskLimits limits_;
    BackBroker broker_;
    bool started_ = false;
    bool nextstart_fired_ = false;
    bool kill_switch_triggered_ = false;
    bool flattening_ = false;
    std::string kill_switch_reason_;
    double peak_portfolio_value_ = 0.0;

    std::unordered_map<std::string, std::unique_ptr<LiveDataFeed>> feeds_;
    std::vector<LiveDataFeed*> feed_cache_;
    std::vector<std::string> feed_names_;

    StateCallback state_callback_;
    OrderNotifyFn order_callback_;
    TradeNotifyFn trade_callback_;

    // Rate limiting
    std::unique_ptr<RateLimitWrapper> rate_limit_wrapper_;
    std::deque<TimePoint> order_timestamps_;

    // Heartbeat monitoring
    TimePoint last_market_data_time_{};

    // Reconnect state
    std::size_t reconnect_attempts_ = 0;
    std::shared_ptr<CallbackGate> callback_gate_;
};

} // namespace stratforge
