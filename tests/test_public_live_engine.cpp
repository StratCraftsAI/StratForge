#include <catch2/catch_test_macros.hpp>

#include <stratforge/indicators/williams.hpp>
#include <stratforge/live/live_engine.hpp>
#include <stratforge/strategy/signal_entry_strategy.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace stratforge;

namespace {

class PublicFakeConnector final : public MarketConnector {
public:
    void connect() override { connected = true; }
    void disconnect() override { connected = false; }
    void submit_order(const Order& order) override { submitted.push_back(order); }
    void cancel_order(OrderId) override {}
    void subscribe_market_data(std::string_view symbol) override {
        subscriptions.emplace_back(symbol);
    }
    void on_fill(FillCallback callback) override { fill = std::move(callback); }
    void on_market_data(MarketDataCallback callback) override { market = std::move(callback); }
    void on_state(StateCallback callback) override { state = std::move(callback); }

    void emit(std::string symbol, double price) const {
        if (market) {
            market(MarketDataSnapshot{
                .symbol = std::move(symbol),
                .last_price = price,
                .last_size = 1.0,
            });
        }
    }

    bool connected = false;
    std::vector<Order> submitted;
    std::vector<std::string> subscriptions;
    FillCallback fill;
    MarketDataCallback market;
    StateCallback state;
};

class BuyOnceStrategy final : public Strategy {
public:
    void next() override {
        if (!bought_) {
            bought_ = true;
            static_cast<void>(buy(1.0));
        }
    }

private:
    bool bought_ = false;
};

class SecondaryHistoryStrategy final : public SignalEntryStrategy {
public:
    void initialize_indicators() override {
        indicator_ = std::make_unique<WilliamsR>(
            data(1).high(), data(1).low(), data(1).close(), 2);
    }

    [[nodiscard]] IndicatorHistoryRequirements
    indicator_history_requirements() const override {
        return {{.feed_index = 1, .bars = 3}};
    }

    void update_indicators(std::size_t feed_index) override {
        if (feed_index == 1) {
            indicator_->next();
            ++secondary_updates;
        }
    }

    [[nodiscard]] EntrySignal check_open_conditions() override {
        static_cast<void>(indicator_->line()[-1]);
        ++decisions;
        return {};
    }

    [[nodiscard]] bool check_close_conditions() override { return false; }

    int secondary_updates = 0;
    int decisions = 0;

private:
    std::unique_ptr<WilliamsR> indicator_;
};

class SerializationProbeStrategy final : public Strategy {
public:
    void prenext() override { enter(); }
    void next() override { enter(); }

    static inline std::atomic<int> active{0};
    static inline std::atomic<int> violations{0};
    static inline std::atomic<int> calls{0};

private:
    static void enter() {
        if (active.fetch_add(1, std::memory_order_acq_rel) != 0) {
            violations.fetch_add(1, std::memory_order_relaxed);
        }
        for (int spin = 0; spin < 10000; ++spin) {
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
        calls.fetch_add(1, std::memory_order_relaxed);
        active.fetch_sub(1, std::memory_order_acq_rel);
    }
};

} // namespace

TEST_CASE("public LiveEngine owns a strategy without plugin dependencies",
          "[live][public]") {
    auto connector = std::make_shared<PublicFakeConnector>();
    LiveEngine engine(connector, std::make_unique<BuyOnceStrategy>());
    engine.subscribe("PRIMARY");
    engine.start();
    connector->emit("PRIMARY", 100.0);
    CHECK(connector->submitted.size() == 1);
    engine.stop();
}

TEST_CASE("public LiveEngine honors independently advancing per-feed warmup",
          "[live][public][multi_feed]") {
    auto connector = std::make_shared<PublicFakeConnector>();
    auto strategy = std::make_unique<SecondaryHistoryStrategy>();
    auto* observed = strategy.get();
    LiveEngine engine(connector, std::move(strategy));
    engine.subscribe("PRIMARY");
    engine.subscribe("CONTEXT");
    engine.start();

    connector->emit("PRIMARY", 100.0);
    connector->emit("CONTEXT", 200.0);
    connector->emit("CONTEXT", 201.0);
    connector->emit("CONTEXT", 202.0);
    CHECK(observed->secondary_updates == 3);
    CHECK(observed->decisions == 0);

    connector->emit("PRIMARY", 101.0);
    CHECK(observed->secondary_updates == 3);
    CHECK(observed->decisions == 1);
    engine.stop();
}

TEST_CASE("public LiveEngine serializes connector callbacks", "[live][public][concurrency]") {
    SerializationProbeStrategy::active = 0;
    SerializationProbeStrategy::violations = 0;
    SerializationProbeStrategy::calls = 0;

    auto connector = std::make_shared<PublicFakeConnector>();
    LiveEngine engine(connector, std::make_unique<SerializationProbeStrategy>());
    engine.subscribe("PRIMARY");
    engine.start();

    std::thread first([&] { connector->emit("PRIMARY", 100.0); });
    std::thread second([&] { connector->emit("PRIMARY", 101.0); });
    first.join();
    second.join();

    CHECK(SerializationProbeStrategy::calls == 2);
    CHECK(SerializationProbeStrategy::violations == 0);
    engine.stop();
}

TEST_CASE("public LiveEngine drains and invalidates callbacks on destruction",
          "[live][public][shutdown]") {
    auto connector = std::make_shared<PublicFakeConnector>();
    MarketDataCallback stale_callback;
    {
        LiveEngine engine(connector, std::make_unique<BuyOnceStrategy>());
        engine.subscribe("PRIMARY");
        engine.start();
        stale_callback = connector->market;
    }

    REQUIRE(stale_callback);
    stale_callback(MarketDataSnapshot{
        .symbol = "PRIMARY",
        .last_price = 100.0,
        .last_size = 1.0,
    });
    CHECK(connector->submitted.empty());
    CHECK_FALSE(connector->market);
    CHECK_FALSE(connector->fill);
    CHECK_FALSE(connector->state);
}
