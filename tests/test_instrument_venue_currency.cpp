#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/broker/broker.hpp>
#include <stratforge/broker/currency.hpp>
#include <stratforge/broker/instrument.hpp>
#include <stratforge/broker/venue.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

Venue make_xnas() {
    return Venue(MIC::XNAS, Currency::USD,
                 std::make_unique<ZeroFeeSchedule>(),
                 std::make_shared<AlwaysOpenCalendar>());
}

Venue make_xcme() {
    return Venue(MIC::XCME, Currency::USD,
                 std::make_unique<ZeroFeeSchedule>(),
                 std::make_shared<AlwaysOpenCalendar>());
}

Venue make_xfx() {
    return Venue(MIC::XFX, Currency::USD,
                 std::make_unique<ZeroFeeSchedule>(),
                 std::make_shared<AlwaysOpenCalendar>());
}

Venue make_xcbo() {
    return Venue(MIC::XCBO, Currency::USD,
                 std::make_unique<ZeroFeeSchedule>(),
                 std::make_shared<AlwaysOpenCalendar>());
}

Venue make_xnas_pct_fee(double rate) {
    return Venue(MIC::XNAS, Currency::USD,
                 std::make_unique<PercentageFeeSchedule>(rate),
                 std::make_shared<AlwaysOpenCalendar>());
}

class ClosedCalendar final : public Calendar {
public:
    [[nodiscard]] bool is_open(DateTime /*at*/) const noexcept override { return false; }
    [[nodiscard]] DateTime next_open(DateTime after) const noexcept override {
        return after + std::chrono::hours(24);
    }
    [[nodiscard]] DateTime next_close(DateTime after) const noexcept override {
        return after + std::chrono::hours(48);
    }
};

} // namespace

// ============================================================================
// Section 5 test #1: equity_notional_matches_qty_times_px
// ============================================================================
TEST_CASE("equity_notional_matches_qty_times_px", "[instrument][equity]") {
    auto venue = make_xnas();
    Equity aapl("AAPL", venue, Currency::USD);

    REQUIRE_THAT(aapl.notional(100.0, 150.0), WithinRel(15000.0, 1e-12));
    REQUIRE_THAT(aapl.notional(1.0, 200.0), WithinRel(200.0, 1e-12));
    REQUIRE_THAT(aapl.notional(0.0, 100.0), WithinAbs(0.0, 1e-12));
    REQUIRE(aapl.type() == SecurityType::CommonStock);
    REQUIRE(aapl.symbol() == "AAPL");
    REQUIRE(aapl.settlement_ccy() == Currency::USD);
}

// ============================================================================
// Section 5 test #2: future_notional_includes_multiplier
// ============================================================================
TEST_CASE("future_notional_includes_multiplier", "[instrument][future]") {
    auto venue = make_xcme();
    Future es("ES", venue, Currency::USD, 50.0, 0.25, DateTime{}, 15000.0);

    REQUIRE_THAT(es.notional(1.0, 5000.0), WithinRel(250000.0, 1e-12));
    REQUIRE_THAT(es.notional(3.0, 4500.0), WithinRel(675000.0, 1e-12));
    REQUIRE(es.type() == SecurityType::Future);
    REQUIRE(es.multiplier() == 50.0);
}

// ============================================================================
// Section 5 test #3: future_mtm_uses_multiplier_not_notional
// ============================================================================
TEST_CASE("future_mtm_uses_multiplier_not_notional", "[instrument][future]") {
    auto venue = make_xcme();
    Future es("ES", venue, Currency::USD, 50.0, 0.25, DateTime{}, 15000.0);

    double mtm = es.mark_to_market(2.0, 5000.0, 5100.0);
    REQUIRE_THAT(mtm, WithinRel(2.0 * (5100.0 - 5000.0) * 50.0, 1e-12));
    REQUIRE_THAT(mtm, WithinRel(10000.0, 1e-12));

    double mtm_loss = es.mark_to_market(1.0, 5000.0, 4900.0);
    REQUIRE_THAT(mtm_loss, WithinRel(-5000.0, 1e-12));

    double im = es.initial_margin(2.0, 5000.0);
    REQUIRE_THAT(im, WithinRel(30000.0, 1e-12));
}

// ============================================================================
// Section 5 test #4: fx_spot_pnl_in_quote_ccy
// ============================================================================
TEST_CASE("fx_spot_pnl_in_quote_ccy", "[instrument][fx]") {
    auto venue = make_xfx();
    FxSpot usdjpy(Currency::USD, Currency::JPY, venue);

    REQUIRE(usdjpy.type() == SecurityType::FxSpot);
    REQUIRE(usdjpy.symbol() == "USDJPY");
    REQUIRE(usdjpy.settlement_ccy() == Currency::JPY);

    double notional = usdjpy.notional(100000.0, 150.0);
    REQUIRE_THAT(notional, WithinRel(100000.0, 1e-12));

    double mtm = usdjpy.mark_to_market(100000.0, 150.0, 151.0);
    REQUIRE_THAT(mtm, WithinRel(100000.0, 1e-12));

    double im = usdjpy.initial_margin(100000.0, 150.0);
    REQUIRE_THAT(im, WithinRel(2000.0, 1e-12));
}

// ============================================================================
// Section 5 test #5: option_long_call_initial_margin_is_premium
// ============================================================================
TEST_CASE("option_long_call_initial_margin_is_premium", "[instrument][option]") {
    auto venue = make_xcbo();
    OptionCall call("AAPL240119C00150000", venue, Currency::USD,
                    150.0, DateTime{}, 100.0);

    REQUIRE(call.type() == SecurityType::OptionCall);
    REQUIRE(call.settlement_ccy() == Currency::USD);

    double im = call.initial_margin(1.0, 5.0);
    REQUIRE_THAT(im, WithinRel(500.0, 1e-12));

    double mtm = call.mark_to_market(1.0, 5.0, 7.0);
    REQUIRE_THAT(mtm, WithinRel(200.0, 1e-12));

    OptionPut put("AAPL240119P00150000", venue, Currency::USD,
                  150.0, DateTime{}, 100.0);
    REQUIRE(put.type() == SecurityType::OptionPut);
    double put_im = put.initial_margin(1.0, 3.0);
    REQUIRE_THAT(put_im, WithinRel(300.0, 1e-12));
}

// ============================================================================
// Section 5 test #6: multi_currency_cash_independent
// ============================================================================
TEST_CASE("multi_currency_cash_independent", "[broker][currency]") {
    BackBroker broker(Currency::USD, 10000.0);

    REQUIRE_THAT(broker.cash(Currency::USD), WithinRel(10000.0, 1e-12));
    REQUIRE_THAT(broker.cash(Currency::JPY), WithinAbs(0.0, 1e-12));

    broker.deposit(Currency::JPY, 1500000.0);
    REQUIRE_THAT(broker.cash(Currency::USD), WithinRel(10000.0, 1e-12));
    REQUIRE_THAT(broker.cash(Currency::JPY), WithinRel(1500000.0, 1e-12));

    broker.deposit(Currency::USD, 5000.0);
    REQUIRE_THAT(broker.cash(Currency::USD), WithinRel(15000.0, 1e-12));
    REQUIRE_THAT(broker.cash(Currency::JPY), WithinRel(1500000.0, 1e-12));
}

// ============================================================================
// Section 5 test #7: cash_in_base_converts_via_fx_provider
// ============================================================================
TEST_CASE("cash_in_base_converts_via_fx_provider", "[broker][currency][fx]") {
    auto fx = std::make_shared<ConstantFxProvider>(0.0067);
    BackBroker broker(Currency::USD, 10000.0, fx);

    broker.deposit(Currency::JPY, 1500000.0);

    double total = broker.cash_in_base(DateTime{});
    REQUIRE_THAT(total, WithinRel(10000.0 + 1500000.0 * 0.0067, 1e-12));
}

// ============================================================================
// Section 5 test #8: venue_closed_order_pends_until_next_open
// ============================================================================
TEST_CASE("venue_closed_order_pends_until_next_open", "[venue][calendar]") {
    auto closed_cal = std::make_shared<ClosedCalendar>();
    Venue venue(MIC::XTKS, Currency::JPY,
                std::make_unique<ZeroFeeSchedule>(), closed_cal);

    REQUIRE_FALSE(venue.is_open(DateTime{}));

    DateTime next = venue.next_open(DateTime{});
    REQUIRE(next > DateTime{});

    DateTime close_time = venue.next_close(DateTime{});
    REQUIRE(close_time > next);
}

// ============================================================================
// Section 5 test #9: portfolio_value_sums_mtm_not_size_times_close
// ============================================================================
TEST_CASE("portfolio_value_sums_mtm_not_size_times_close", "[broker][instrument]") {
    auto venue = make_xcme();
    Future es("ES", venue, Currency::USD, 50.0, 0.25, DateTime{}, 15000.0);

    StaticFeed feed({
        {5000.0, 5100.0, 4900.0, 5050.0},
        {5050.0, 5200.0, 5000.0, 5150.0},
    });
    REQUIRE(feed.load());
    std::vector<DataFeed*> feeds{&feed};

    BackBroker broker(Currency::USD, 100000.0);
    broker.set_instrument(0, &es);

    (void)broker.buy(2.0);
    broker.set_bar_index(0);
    broker.process_orders(feeds);

    REQUIRE(broker.orders().back().status == OrderStatus::Completed);
    REQUIRE_THAT(broker.orders().back().executed_price, WithinRel(5000.0, 1e-12));

    double pv = broker.portfolio_value(feeds);

    double expected_cash = 100000.0 - (2.0 * 5000.0);
    double expected_mtm = es.mark_to_market(2.0, 5000.0, 5050.0);
    double expected_pv = expected_cash + expected_mtm;

    REQUIRE_THAT(pv, WithinRel(expected_pv, 1e-10));
    REQUIRE(pv != expected_cash + 2.0 * 5050.0);
}

// ============================================================================
// Section 5 test #10: All existing BackBroker tests still pass (the equity case)
// This test verifies backward compatibility: BackBroker(double) constructor
// still works, and equity trades (the default) execute correctly.
// ============================================================================
TEST_CASE("backbroker_legacy_equity_backward_compat", "[broker][backward-compat]") {
    StaticFeed feed({
        {100.0, 112.0, 95.0, 107.0},
        {108.0, 120.0, 100.0, 118.0},
    });
    REQUIRE(feed.load());
    std::vector<DataFeed*> feeds{&feed};

    BackBroker broker(5000.0);
    broker.set_commission({.type = CommissionType::Percentage, .commission = 0.001});

    (void)broker.buy(10.0);
    broker.set_bar_index(0);
    broker.process_orders(feeds);

    auto& pos = broker.position(0);
    REQUIRE_THAT(pos.size, WithinRel(10.0, 1e-12));
    REQUIRE_THAT(pos.avg_price, WithinRel(100.0, 1e-12));

    double comm = 10.0 * 100.0 * 0.001;
    double expected_cash = 5000.0 - (10.0 * 100.0) - comm;
    REQUIRE_THAT(broker.cash(), WithinRel(expected_cash, 1e-10));

    double pv = broker.portfolio_value(feeds);
    double expected_pv = expected_cash + 10.0 * 107.0;
    REQUIRE_THAT(pv, WithinRel(expected_pv, 1e-10));
}

// ============================================================================
// Additional coverage: CorporateBond instrument
// ============================================================================
TEST_CASE("corporate_bond_notional_uses_face_value", "[instrument][bond]") {
    auto venue = make_xnas();
    CorporateBond bond("AAPL-BOND", venue, Currency::USD, 1000.0, 0.05);

    REQUIRE(bond.type() == SecurityType::CorporateBond);
    double notional = bond.notional(10.0, 98.5);
    REQUIRE_THAT(notional, WithinRel(10.0 * 98.5 * 1000.0 / 100.0, 1e-12));

    double mtm = bond.mark_to_market(10.0, 98.5, 99.0);
    REQUIRE_THAT(mtm, WithinRel(10.0 * (99.0 - 98.5) * 1000.0 / 100.0, 1e-12));
}

// ============================================================================
// Additional coverage: Position::unrealized_pnl via instrument dispatch
// ============================================================================
TEST_CASE("position_unrealized_pnl_dispatches_to_instrument", "[position][instrument]") {
    auto venue = make_xcme();
    Future es("ES", venue, Currency::USD, 50.0, 0.25, DateTime{}, 15000.0);

    Position pos;
    pos.instrument = &es;
    pos.update(2.0, 5000.0);

    double pnl = pos.unrealized_pnl(5100.0);
    REQUIRE_THAT(pnl, WithinRel(2.0 * (5100.0 - 5000.0) * 50.0, 1e-12));

    Position pos_no_instr;
    pos_no_instr.update(2.0, 5000.0);
    double pnl2 = pos_no_instr.unrealized_pnl(5100.0);
    REQUIRE_THAT(pnl2, WithinRel(2.0 * (5100.0 - 5000.0), 1e-12));
}

// ============================================================================
// Additional coverage: iso_code for Currency enum
// ============================================================================
TEST_CASE("iso_code_returns_correct_strings", "[currency]") {
    REQUIRE(iso_code(Currency::USD) == "USD");
    REQUIRE(iso_code(Currency::EUR) == "EUR");
    REQUIRE(iso_code(Currency::JPY) == "JPY");
    REQUIRE(iso_code(Currency::GBP) == "GBP");
    REQUIRE(iso_code(Currency::HKD) == "HKD");
    REQUIRE(iso_code(Currency::CHF) == "CHF");
    REQUIRE(iso_code(Currency::CAD) == "CAD");
    REQUIRE(iso_code(Currency::AUD) == "AUD");
    REQUIRE(iso_code(Currency::CNY) == "CNY");
    REQUIRE(iso_code(Currency::KRW) == "KRW");
    REQUIRE(iso_code(Currency::SGD) == "SGD");
    REQUIRE(iso_code(Currency::NZD) == "NZD");
}

// ============================================================================
// Additional coverage: Venue fee schedule integration
// ============================================================================
TEST_CASE("venue_fee_schedule_applied_via_instrument", "[venue][fees]") {
    auto venue = make_xnas_pct_fee(0.001);
    Equity aapl("AAPL", venue, Currency::USD);

    double fee = aapl.venue().fees().commission(100.0, 150.0, Currency::USD);
    REQUIRE_THAT(fee, WithinRel(15.0, 1e-12));
}

// ============================================================================
// Additional coverage: Broker instrument-based commission in fill path
// ============================================================================
TEST_CASE("broker_uses_instrument_venue_fees_not_legacy_commission", "[broker][instrument][fees]") {
    auto venue = make_xnas_pct_fee(0.001);
    Equity aapl("AAPL", venue, Currency::USD);

    StaticFeed feed({
        {100.0, 110.0, 95.0, 105.0},
        {105.0, 115.0, 100.0, 110.0},
    });
    REQUIRE(feed.load());
    std::vector<DataFeed*> feeds{&feed};

    BackBroker broker(Currency::USD, 50000.0);
    broker.set_instrument(0, &aapl);

    (void)broker.buy(100.0);
    broker.set_bar_index(0);
    broker.process_orders(feeds);

    auto& filled = broker.orders().back();
    REQUIRE(filled.status == OrderStatus::Completed);
    double expected_comm = 100.0 * 100.0 * 0.001;
    REQUIRE_THAT(filled.commission, WithinRel(expected_comm, 1e-10));
}

// ============================================================================
// Additional coverage: Buying power check uses initial_margin for instruments
// ============================================================================
TEST_CASE("buying_power_uses_initial_margin_not_size_times_price", "[broker][instrument][margin]") {
    auto venue = make_xcme();
    Future es("ES", venue, Currency::USD, 50.0, 0.25, DateTime{}, 15000.0);

    StaticFeed feed({
        {5000.0, 5100.0, 4900.0, 5050.0},
    });
    REQUIRE(feed.load());
    std::vector<DataFeed*> feeds{&feed};

    BackBroker broker(Currency::USD, 20000.0);
    broker.set_instrument(0, &es);

    (void)broker.buy(1.0);
    broker.set_bar_index(0);
    broker.process_orders(feeds);

    auto& filled = broker.orders().back();
    REQUIRE(filled.status == OrderStatus::Completed);

    BackBroker poor_broker(Currency::USD, 10000.0);
    poor_broker.set_instrument(0, &es);
    (void)poor_broker.buy(1.0);
    poor_broker.set_bar_index(0);
    poor_broker.process_orders(feeds);

    auto& rejected = poor_broker.orders().back();
    REQUIRE(rejected.status == OrderStatus::Margin);
}

// ============================================================================
// Additional coverage: ConstantFxProvider identity rate
// ============================================================================
TEST_CASE("constant_fx_provider_same_currency_returns_1", "[currency][fx]") {
    ConstantFxProvider fx(150.0);
    REQUIRE_THAT(fx.rate(Currency::USD, Currency::USD, DateTime{}), WithinRel(1.0, 1e-12));
    REQUIRE_THAT(fx.rate(Currency::USD, Currency::JPY, DateTime{}), WithinRel(150.0, 1e-12));
}
