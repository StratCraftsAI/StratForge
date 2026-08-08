#pragma once

#include <stratforge/broker/commission.hpp>
#include <stratforge/broker/currency.hpp>
#include <stratforge/broker/instrument.hpp>
#include <stratforge/broker/order.hpp>
#include <stratforge/broker/position.hpp>
#include <stratforge/broker/trade.hpp>
#include <stratforge/broker/venue.hpp>
#include <stratforge/corrective/corrective_contracts.hpp>
#include <stratforge/data/data_feed.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace stratforge {

using OrderNotifyFn = std::function<void(const Order&)>;
using TradeNotifyFn = std::function<void(const Trade&, double /*original_size*/)>;
using PreOrderHookFn = std::function<corrective::PreOrderResult(OrderSide, double /*size*/, std::size_t /*data_index*/)>;

class BackBroker {
public:
    explicit BackBroker(Currency base_ccy, double initial_base_cash = 10000.0,
                        std::shared_ptr<FxRateProvider> fx = nullptr)
        : base_ccy_(base_ccy),
          fx_(std::move(fx)) {
        cash_[base_ccy_] = initial_base_cash;
        initial_cash_ = initial_base_cash;
        all_orders_.reserve(1024);
        pending_orders_.reserve(64);
        positions_.reserve(8);
        open_trades_.reserve(256);
        closed_trades_.reserve(1024);
    }

    explicit BackBroker(double cash = 10000.0)
        : BackBroker(Currency::USD, cash, nullptr) {}

    void set_cash(double cash) noexcept {
        initial_cash_ = cash;
        cash_[base_ccy_] = cash;
    }

    void deposit(Currency ccy, double amount) noexcept {
        cash_[ccy] += amount;
    }

    [[nodiscard]] double cash(Currency ccy) const noexcept {
        auto it = cash_.find(ccy);
        return it != cash_.end() ? it->second : 0.0;
    }

    [[nodiscard]] double cash() const noexcept {
        return cash(base_ccy_);
    }

    [[nodiscard]] double cash_in_base(DateTime at) const {
        double total = 0.0;
        for (const auto& [ccy, amount] : cash_) {
            if (ccy == base_ccy_) {
                total += amount;
            } else if (fx_) {
                total += amount * fx_->rate(ccy, base_ccy_, at);
            } else {
                total += amount;
            }
        }
        return total;
    }

    [[nodiscard]] double initial_cash() const noexcept { return initial_cash_; }

    [[nodiscard]] Currency base_ccy() const noexcept { return base_ccy_; }

    void set_commission(CommissionInfo info) noexcept {
        legacy_commission_ = info;
    }

    [[nodiscard]] const CommissionInfo& commission_info() const noexcept {
        return legacy_commission_;
    }

    void set_order_notify(OrderNotifyFn fn) { order_notify_ = std::move(fn); }
    void set_trade_notify(TradeNotifyFn fn) { trade_notify_ = std::move(fn); }
    void set_pre_order_hook(PreOrderHookFn fn) { pre_order_hook_ = std::move(fn); }

    void set_slippage_perc(double perc, bool slip_open = true, bool slip_limit = true,
                           bool slip_match = true, bool slip_out = false) noexcept {
        slip_perc_ = perc;
        slip_fixed_ = 0.0;
        slip_open_ = slip_open;
        slip_limit_ = slip_limit;
        slip_match_ = slip_match;
        slip_out_ = slip_out;
    }

    void set_slippage_fixed(double fixed, bool slip_open = true, bool slip_limit = true,
                            bool slip_match = true, bool slip_out = false) noexcept {
        slip_perc_ = 0.0;
        slip_fixed_ = fixed;
        slip_open_ = slip_open;
        slip_limit_ = slip_limit;
        slip_match_ = slip_match;
        slip_out_ = slip_out;
    }

    struct OrderParams {
        OrderType type = OrderType::Market;
        double price = 0.0;
        std::optional<double> stop_price = std::nullopt;
        std::size_t data_index = 0;
        std::optional<std::size_t> valid_until_bar = std::nullopt;
        double trail_amount = 0.0;
        double trail_percent = 0.0;
        std::size_t oco_group_id = 0;
        std::size_t parent_id = 0;
        bool transmit = true;
    };

    void set_instrument(std::size_t data_index, const Instrument* instr) {
        instrument_map_[data_index] = instr;
    }

    [[nodiscard]] std::size_t buy(double size, double price = 0.0,
                                   std::optional<double> stop_price = std::nullopt,
                                   OrderType type = OrderType::Market,
                                   std::size_t data_index = 0) {
        return submit_order(OrderSide::Buy, size, price, stop_price, type, data_index);
    }

    [[nodiscard]] std::size_t sell(double size, double price = 0.0,
                                    std::optional<double> stop_price = std::nullopt,
                                    OrderType type = OrderType::Market,
                                    std::size_t data_index = 0) {
        return submit_order(OrderSide::Sell, size, price, stop_price, type, data_index);
    }

    [[nodiscard]] std::size_t buy_ext(double size, const OrderParams& params) {
        return submit_order_ext(OrderSide::Buy, size, params);
    }

    [[nodiscard]] std::size_t sell_ext(double size, const OrderParams& params) {
        return submit_order_ext(OrderSide::Sell, size, params);
    }

    struct BracketResult { std::size_t main_id; std::size_t stop_id; std::size_t limit_id; };

    [[nodiscard]] BracketResult buy_bracket(double size, double limit_price,
                                             double stop_price,
                                             std::size_t data_index = 0) {
        std::size_t oco_grp = next_oco_group_id_++;

        auto main_id = submit_order(OrderSide::Buy, size, 0.0, std::nullopt,
                                     OrderType::Market, data_index);

        OrderParams tp_params;
        tp_params.type = OrderType::Limit;
        tp_params.price = limit_price;
        tp_params.data_index = data_index;
        tp_params.parent_id = main_id;
        tp_params.oco_group_id = oco_grp;
        tp_params.transmit = false;
        auto limit_id = submit_order_ext(OrderSide::Sell, size, tp_params);

        OrderParams sl_params;
        sl_params.type = OrderType::Stop;
        sl_params.stop_price = stop_price;
        sl_params.data_index = data_index;
        sl_params.parent_id = main_id;
        sl_params.oco_group_id = oco_grp;
        sl_params.transmit = false;
        auto stop_id = submit_order_ext(OrderSide::Sell, size, sl_params);

        for (auto& o : all_orders_) {
            if (o.id == main_id) {
                o.child_ids = {limit_id, stop_id};
                break;
            }
        }
        for (auto& o : pending_orders_) {
            if (o.id == main_id) {
                o.child_ids = {limit_id, stop_id};
                break;
            }
        }

        return {main_id, stop_id, limit_id};
    }

    [[nodiscard]] BracketResult sell_bracket(double size, double limit_price,
                                              double stop_price,
                                              std::size_t data_index = 0) {
        std::size_t oco_grp = next_oco_group_id_++;

        auto main_id = submit_order(OrderSide::Sell, size, 0.0, std::nullopt,
                                     OrderType::Market, data_index);

        OrderParams tp_params;
        tp_params.type = OrderType::Limit;
        tp_params.price = limit_price;
        tp_params.data_index = data_index;
        tp_params.parent_id = main_id;
        tp_params.oco_group_id = oco_grp;
        tp_params.transmit = false;
        auto limit_id = submit_order_ext(OrderSide::Buy, size, tp_params);

        OrderParams sl_params;
        sl_params.type = OrderType::Stop;
        sl_params.stop_price = stop_price;
        sl_params.data_index = data_index;
        sl_params.parent_id = main_id;
        sl_params.oco_group_id = oco_grp;
        sl_params.transmit = false;
        auto stop_id = submit_order_ext(OrderSide::Buy, size, sl_params);

        for (auto& o : all_orders_) {
            if (o.id == main_id) {
                o.child_ids = {limit_id, stop_id};
                break;
            }
        }
        for (auto& o : pending_orders_) {
            if (o.id == main_id) {
                o.child_ids = {limit_id, stop_id};
                break;
            }
        }

        return {main_id, stop_id, limit_id};
    }

    [[nodiscard]] std::size_t next_oco_group() noexcept { return next_oco_group_id_++; }

    void cancel_order(std::size_t order_id) {
        for (auto& order : pending_orders_) {
            if (order.id == order_id && order.is_alive()) {
                order.cancel();
                sync_order(order);
                if (order_notify_) order_notify_(order);
            }
        }
    }

    [[nodiscard]] std::size_t close(std::size_t data_index = 0) {
        auto& pos = get_position(data_index);
        if (pos.is_flat()) return 0;

        if (pos.is_long()) {
            return sell(std::abs(pos.size), 0.0, std::nullopt, OrderType::Market, data_index);
        } else {
            return buy(std::abs(pos.size), 0.0, std::nullopt, OrderType::Market, data_index);
        }
    }

    void process_orders(const std::vector<DataFeed*>& data_feeds) {
        std::vector<Order> still_pending;
        std::vector<std::size_t> filled_oco_groups;

        for (auto& order : pending_orders_) {
            if (!order.is_alive()) [[unlikely]] continue;

            if (!order.transmit && order.parent_id != 0) {
                if (!is_parent_filled(order.parent_id)) {
                    still_pending.push_back(order);
                    continue;
                }
                order.transmit = true;
            }

            if (order.data_index >= data_feeds.size()) [[unlikely]] {
                order.reject();
                sync_order(order);
                if (order_notify_) order_notify_(order);
                continue;
            }

            auto* feed = data_feeds[order.data_index];
            if (feed->size() == 0 || feed->buflen() == 0) {
                still_pending.push_back(order);
                continue;
            }

            if (order.valid_until_bar.has_value() && data_bar_index_ > *order.valid_until_bar) {
                order.expire();
                sync_order(order);
                if (order_notify_) order_notify_(order);
                continue;
            }

            if (order.status == OrderStatus::Created) {
                order.submit();
                sync_order(order);
                if (order_notify_) order_notify_(order);
                order.accept();
                sync_order(order);
                if (order_notify_) order_notify_(order);
            }

            bool is_trail = (order.type == OrderType::StopTrail ||
                             order.type == OrderType::StopTrailLimit);

            bool filled = try_fill_order(order, *feed);
            if (!filled && is_trail) {
                update_trail_stop(order, *feed);
            }
            if (filled) {
                if (order_notify_) order_notify_(order);
                if (order.oco_group_id != 0 && order.status == OrderStatus::Completed) {
                    filled_oco_groups.push_back(order.oco_group_id);
                }
                if (order.status == OrderStatus::Completed && !order.child_ids.empty()) {
                    activate_bracket_children(order.id);
                }
                if (order.parent_id == 0 && !order.child_ids.empty() &&
                    (order.status == OrderStatus::Canceled ||
                     order.status == OrderStatus::Rejected ||
                     order.status == OrderStatus::Margin ||
                     order.status == OrderStatus::Expired)) {
                    cancel_bracket_children(order.id);
                }
            } else {
                still_pending.push_back(order);
            }
        }

        if (!filled_oco_groups.empty()) {
            for (auto& order : still_pending) {
                if (order.oco_group_id != 0 && order.is_alive()) {
                    for (auto group_id : filled_oco_groups) {
                        if (order.oco_group_id == group_id) {
                            order.cancel();
                            sync_order(order);
                            if (order_notify_) order_notify_(order);
                            break;
                        }
                    }
                }
            }
            std::erase_if(still_pending, [](const Order& o) { return !o.is_alive(); });
        }

        pending_orders_ = std::move(still_pending);
    }

    [[nodiscard]] double portfolio_value(const std::vector<DataFeed*>& data_feeds) const noexcept {
        double value = cash(base_ccy_);
        for (const auto& pos : positions_) {
            if (!pos.is_flat() && pos.data_index < data_feeds.size()) {
                const double mark_px = data_feeds[pos.data_index]->close()[0];
                if (pos.instrument) {
                    value += pos.instrument->mark_to_market(pos.size, pos.avg_price, mark_px);
                } else {
                    value += pos.size * mark_px;
                }
            }
        }
        return value;
    }

    [[nodiscard]] double portfolio_value(DateTime at, const std::vector<DataFeed*>& data_feeds) const {
        double value = cash_in_base(at);
        for (const auto& pos : positions_) {
            if (!pos.is_flat() && pos.data_index < data_feeds.size()) {
                const double mark_px = data_feeds[pos.data_index]->close()[0];
                if (pos.instrument) {
                    double mtm = pos.instrument->mark_to_market(pos.size, pos.avg_price, mark_px);
                    Currency settle = pos.instrument->settlement_ccy();
                    if (settle != base_ccy_ && fx_) {
                        mtm *= fx_->rate(settle, base_ccy_, at);
                    }
                    value += mtm;
                } else {
                    value += pos.size * mark_px;
                }
            }
        }
        return value;
    }

    [[nodiscard]] const Position& position(std::size_t data_index = 0) const {
        for (const auto& pos : positions_) {
            if (pos.data_index == data_index) return pos;
        }
        static const Position empty{};
        return empty;
    }

    [[nodiscard]] const std::vector<Order>& orders() const noexcept { return all_orders_; }

    [[nodiscard]] const std::vector<std::pair<Trade, double>>& closed_trades() const noexcept {
        return closed_trades_;
    }

    [[nodiscard]] const std::vector<std::pair<Trade, double>>& open_trades() const noexcept {
        return open_trades_;
    }

    [[nodiscard]] const std::vector<Order>& pending_orders() const noexcept {
        return pending_orders_;
    }

    [[nodiscard]] const std::shared_ptr<FxRateProvider>& fx_provider() const noexcept {
        return fx_;
    }

private:
    [[nodiscard]] Order* find_order(std::size_t order_id) {
        for (auto& order : all_orders_) {
            if (order.id == order_id) {
                return &order;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Order* find_order(std::size_t order_id) const {
        for (const auto& order : all_orders_) {
            if (order.id == order_id) {
                return &order;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t submit_order(OrderSide side, double size, double price,
                                            std::optional<double> stop_price,
                                            OrderType type, std::size_t data_index) {
        const auto por = pre_order_hook_
            ? pre_order_hook_(side, size, data_index)
            : corrective::PreOrderResult{0, size, false};
        if (por.rejected) return 0;

        Order order;
        order.id = next_order_id_++;
        order.type = type;
        order.side = side;
        order.size = por.final_size;
        order.price = price;
        order.stop_price = stop_price;
        order.data_index = data_index;
        order.candidate_id = por.candidate_id;

        all_orders_.push_back(order);
        pending_orders_.push_back(order);
        return order.id;
    }

    [[nodiscard]] std::size_t submit_order_ext(OrderSide side, double size,
                                               const OrderParams& params) {
        const auto por = pre_order_hook_
            ? pre_order_hook_(side, size, params.data_index)
            : corrective::PreOrderResult{0, size, false};
        if (por.rejected) return 0;

        Order order;
        order.id = next_order_id_++;
        order.type = params.type;
        order.side = side;
        order.size = por.final_size;
        order.price = params.price;
        order.stop_price = params.stop_price;
        order.data_index = params.data_index;
        order.valid_until_bar = params.valid_until_bar;
        order.trail_amount = params.trail_amount;
        order.trail_percent = params.trail_percent;
        order.oco_group_id = params.oco_group_id;
        order.parent_id = params.parent_id;
        order.transmit = params.transmit;
        order.candidate_id = por.candidate_id;

        all_orders_.push_back(order);
        pending_orders_.push_back(order);
        return order.id;
    }

    void update_trail_stop(Order& order, const DataFeed& feed) {
        double current_price = (order.side == OrderSide::Buy) ? feed.low()[0] : feed.high()[0];
        double trail_dist = order.trail_amount;
        if (order.trail_percent > 0.0) {
            trail_dist = current_price * order.trail_percent;
        }

        if (order.side == OrderSide::Sell) {
            double new_stop = current_price - trail_dist;
            if (order.trail_stop_price == 0.0 || new_stop > order.trail_stop_price) {
                order.trail_stop_price = new_stop;
                sync_order(order);
            }
        } else {
            double new_stop = current_price + trail_dist;
            if (order.trail_stop_price == 0.0 || new_stop < order.trail_stop_price) {
                order.trail_stop_price = new_stop;
                sync_order(order);
            }
        }
    }

    [[nodiscard]] bool is_parent_filled(std::size_t parent_id) const {
        for (const auto& order : all_orders_) {
            if (order.id == parent_id) {
                return order.status == OrderStatus::Completed;
            }
        }
        return false;
    }

    void activate_bracket_children(std::size_t parent_id) {
        for (auto& order : pending_orders_) {
            if (order.parent_id == parent_id && !order.transmit) {
                order.transmit = true;
            }
        }
    }

    void cancel_bracket_children(std::size_t parent_id) {
        for (auto& order : pending_orders_) {
            if (order.parent_id == parent_id && order.is_alive()) {
                order.cancel();
                sync_order(order);
                if (order_notify_) order_notify_(order);
            }
        }
    }

    [[nodiscard]] const Instrument* instrument_for(std::size_t data_index) const {
        auto it = instrument_map_.find(data_index);
        return it != instrument_map_.end() ? it->second : nullptr;
    }

    [[nodiscard]] bool try_fill_order(Order& order, const DataFeed& feed) {
        double fill_price = 0.0;
        const bool apply_open_slippage =
            order.type == OrderType::Market || order.type == OrderType::Stop ||
            order.type == OrderType::StopTrail;
        const bool apply_limit_slippage =
            order.type == OrderType::Limit || order.type == OrderType::StopLimit ||
            order.type == OrderType::StopTrailLimit;

        switch (order.type) {
            case OrderType::Market: [[likely]]
                fill_price = feed.open()[0];
                break;

            case OrderType::Limit:
                if (order.side == OrderSide::Buy) {
                    if (feed.low()[0] <= order.price) {
                        fill_price = std::min(order.price, feed.open()[0]);
                    } else {
                        return false;
                    }
                } else {
                    if (feed.high()[0] >= order.price) {
                        fill_price = std::max(order.price, feed.open()[0]);
                    } else {
                        return false;
                    }
                }
                break;

            case OrderType::Stop:
                if (!order.stop_price) {
                    order.reject();
                    sync_order(order);
                    return true;
                }
                if (order.side == OrderSide::Buy) {
                    if (feed.high()[0] >= *order.stop_price) {
                        fill_price = std::max(*order.stop_price, feed.open()[0]);
                    } else {
                        return false;
                    }
                } else {
                    if (feed.low()[0] <= *order.stop_price) {
                        fill_price = std::min(*order.stop_price, feed.open()[0]);
                    } else {
                        return false;
                    }
                }
                break;

            case OrderType::StopLimit:
                if (!order.stop_price) {
                    order.reject();
                    sync_order(order);
                    return true;
                }
                if (order.side == OrderSide::Buy) {
                    if (feed.high()[0] >= *order.stop_price) {
                        if (feed.low()[0] <= order.price) {
                            fill_price = std::min(order.price, feed.open()[0]);
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                } else {
                    if (feed.low()[0] <= *order.stop_price) {
                        if (feed.high()[0] >= order.price) {
                            fill_price = std::max(order.price, feed.open()[0]);
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                }
                break;

            case OrderType::StopTrail:
                if (order.trail_stop_price == 0.0) return false;
                if (order.side == OrderSide::Sell) {
                    if (feed.low()[0] <= order.trail_stop_price) {
                        fill_price = std::min(order.trail_stop_price, feed.open()[0]);
                    } else {
                        return false;
                    }
                } else {
                    if (feed.high()[0] >= order.trail_stop_price) {
                        fill_price = std::max(order.trail_stop_price, feed.open()[0]);
                    } else {
                        return false;
                    }
                }
                break;

            case OrderType::StopTrailLimit:
                if (order.trail_stop_price == 0.0) return false;
                if (order.side == OrderSide::Sell) {
                    if (feed.low()[0] <= order.trail_stop_price) {
                        if (feed.high()[0] >= order.price) {
                            fill_price = std::max(order.price, feed.open()[0]);
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                } else {
                    if (feed.high()[0] >= order.trail_stop_price) {
                        if (feed.low()[0] <= order.price) {
                            fill_price = std::min(order.price, feed.open()[0]);
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                }
                break;
        }

        if ((apply_open_slippage && slip_open_) || (apply_limit_slippage && slip_limit_)) {
            const auto slipped = apply_slippage(fill_price, order.side, feed);
            if (!slipped.has_value()) {
                return false;
            }
            fill_price = *slipped;
        }

        double fill_size = order.size;
        if (order.side == OrderSide::Sell) fill_size = -fill_size;

        const Instrument* instr = instrument_for(order.data_index);
        double comm = 0.0;
        if (instr) {
            comm = instr->venue().fees().commission(order.size, fill_price,
                                                     instr->settlement_ccy());
        } else {
            comm = legacy_commission_.calculate(order.size, fill_price);
        }

        if (order.side == OrderSide::Buy) {
            if (instr) {
                const double im = instr->initial_margin(order.size, fill_price);
                const double need = im + comm;
                const Currency settle = instr->settlement_ccy();
                double have = cash(settle);
                if (settle != base_ccy_ && fx_) {
                    const DateTime bar_dt = (feed.size() > 0) ? feed.datetime()[0] : DateTime{};
                    have += fx_->rate(base_ccy_, settle, bar_dt) * cash(base_ccy_);
                }
                if (need > have) {
                    order.margin();
                    sync_order(order);
                    return true;
                }
            } else {
                double cost = fill_size * fill_price + comm;
                if (cost > cash(base_ccy_)) {
                    order.margin();
                    sync_order(order);
                    return true;
                }
            }
        }

        order.execute(fill_price, order.size, comm);

        Currency settle = instr ? instr->settlement_ccy() : base_ccy_;
        cash_[settle] -= fill_size * fill_price + comm;

        auto& pos = get_position(order.data_index);
        double old_size = pos.size;
        pos.update(fill_size, fill_price);

        const DateTime bar_dt = (feed.size() > 0) ? feed.datetime()[0] : DateTime{};
        update_trades(order.data_index, old_size, fill_size, fill_price, comm,
                      data_bar_index_, bar_dt, order.candidate_id);

        sync_order(order);

        return true;
    }

    [[nodiscard]] std::optional<double> apply_slippage(double raw_price, OrderSide side,
                                                       const DataFeed& feed) const noexcept {
        double adjusted = raw_price;
        if (slip_perc_ != 0.0) {
            adjusted *= (side == OrderSide::Buy) ? (1.0 + slip_perc_) : (1.0 - slip_perc_);
        } else if (slip_fixed_ != 0.0) {
            adjusted += (side == OrderSide::Buy) ? slip_fixed_ : -slip_fixed_;
        }

        if (slip_out_) {
            return adjusted;
        }

        const double low = feed.low()[0];
        const double high = feed.high()[0];
        if (adjusted >= low && adjusted <= high) {
            return adjusted;
        }

        if (!slip_match_) {
            return std::nullopt;
        }

        return std::clamp(adjusted, low, high);
    }

    void sync_order(const Order& order) {
        for (auto& stored : all_orders_) {
            if (stored.id == order.id) {
                stored = order;
                break;
            }
        }
    }

    Position& get_position(std::size_t data_index) {
        for (auto& pos : positions_) {
            if (pos.data_index == data_index) return pos;
        }
        const Instrument* instr = instrument_for(data_index);
        positions_.push_back(Position{.instrument = instr, .data_index = data_index});
        return positions_.back();
    }

    void update_trades(std::size_t data_index, double old_size, double fill_size,
                       double fill_price, double comm, std::size_t bar_index,
                       DateTime bar_dt = DateTime{},
                       std::uint64_t candidate_id = 0) {
        if (std::abs(old_size) < 1e-10) {
            Trade trade;
            trade.id = next_trade_id_++;
            trade.data_index = data_index;
            trade.candidate_id = candidate_id;
            trade.update(fill_size, fill_price, comm, bar_index, bar_dt);
            open_trades_.push_back({trade, fill_size});
        } else if ((old_size > 0 && fill_size > 0) || (old_size < 0 && fill_size < 0)) {
            for (auto& [trade, orig_size] : open_trades_) {
                if (trade.data_index == data_index && trade.is_open()) {
                    orig_size += fill_size;
                    trade.update(fill_size, fill_price, comm, bar_index, bar_dt);
                    break;
                }
            }
        } else {
            for (auto it = open_trades_.begin(); it != open_trades_.end();) {
                auto& [trade, orig_size] = *it;
                if (trade.data_index == data_index && trade.is_open()) {
                    if (std::abs(fill_size) > std::abs(trade.size)) {
                        const double total_abs = std::abs(fill_size);
                        const double close_abs = std::abs(trade.size);
                        const double close_size = -trade.size;
                        const double close_comm = (close_abs / total_abs) * comm;
                        const double open_comm = comm - close_comm;
                        const double remaining = fill_size + trade.size;

                        trade.update(close_size, fill_price, close_comm, bar_index, bar_dt);
                        closed_trades_.push_back({trade, orig_size});
                        if (trade_notify_) trade_notify_(trade, orig_size);
                        it = open_trades_.erase(it);

                        Trade next_trade;
                        next_trade.id = next_trade_id_++;
                        next_trade.data_index = data_index;
                        next_trade.candidate_id = candidate_id;
                        next_trade.update(remaining, fill_price, open_comm, bar_index, bar_dt);
                        open_trades_.push_back({next_trade, remaining});
                        break;
                    } else {
                        trade.update(fill_size, fill_price, comm, bar_index, bar_dt);
                        if (trade.is_closed()) {
                            closed_trades_.push_back({trade, orig_size});
                            if (trade_notify_) trade_notify_(trade, orig_size);
                            it = open_trades_.erase(it);
                        } else {
                            ++it;
                        }
                        break;
                    }
                } else {
                    ++it;
                }
            }
        }
    }

    Currency base_ccy_ = Currency::USD;
    double initial_cash_ = 10000.0;
    std::unordered_map<Currency, double> cash_;
    std::shared_ptr<FxRateProvider> fx_;
    CommissionInfo legacy_commission_;
    double slip_perc_ = 0.0;
    double slip_fixed_ = 0.0;
    bool slip_open_ = true;
    bool slip_limit_ = true;
    bool slip_match_ = true;
    bool slip_out_ = false;
    std::size_t next_order_id_ = 1;
    std::size_t next_trade_id_ = 1;
    std::size_t next_oco_group_id_ = 1;
    std::size_t data_bar_index_ = 0;
    std::unordered_map<std::size_t, const Instrument*> instrument_map_;

    std::vector<Order> all_orders_;
    std::vector<Order> pending_orders_;
    std::vector<Position> positions_;
    std::vector<std::pair<Trade, double>> open_trades_;
    std::vector<std::pair<Trade, double>> closed_trades_;

    OrderNotifyFn order_notify_;
    TradeNotifyFn trade_notify_;
    PreOrderHookFn pre_order_hook_;

public:
    void set_bar_index(std::size_t idx) noexcept { data_bar_index_ = idx; }
};

} // namespace stratforge
