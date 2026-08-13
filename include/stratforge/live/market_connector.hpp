#pragma once

#include <stratforge/broker/order.hpp>
#include <stratforge/broker/trade.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace stratforge {

using OrderId = std::size_t;

/// Live connector state.
enum class ConnectorState : std::uint8_t {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error,
};

/// Normalized market data payload emitted by live connectors.
struct MarketDataSnapshot {
    std::string symbol;
    double bid_price = 0.0;
    double ask_price = 0.0;
    double last_price = 0.0;
    double bid_size = 0.0;
    double ask_size = 0.0;
    double last_size = 0.0;
    std::size_t data_index = 0;
};

using FillCallback = std::function<void(const Trade&)>;
using MarketDataCallback = std::function<void(const MarketDataSnapshot&)>;
using StateCallback = std::function<void(ConnectorState)>;

/// Shared live-trading connector abstraction.
///
/// All exchange/broker integrations in  through
/// should implement this interface so the engine can route orders and data
/// through a consistent surface.
class MarketConnector {
public:
    virtual ~MarketConnector() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual void submit_order(const Order& order) = 0;
    virtual void cancel_order(OrderId id) = 0;
    virtual void subscribe_market_data(std::string_view symbol) = 0;

    virtual void on_fill(FillCallback callback) = 0;
    virtual void on_market_data(MarketDataCallback callback) = 0;
    virtual void on_state(StateCallback callback) = 0;
};

} // namespace stratforge
