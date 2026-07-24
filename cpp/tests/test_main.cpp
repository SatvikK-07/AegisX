#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <map>
#include <random>
#include <stdexcept>

#include "aegisx/aegisx.h"

namespace {

using aegisx::Add;
using aegisx::Event;
using aegisx::OrderBook;
using aegisx::OrderId;
using aegisx::Price;
using aegisx::Quantity;
using aegisx::Side;

Event event(const aegisx::Timestamp timestamp, aegisx::Payload payload, const aegisx::StockLocate locate = 1) {
  return {timestamp, std::move(payload), locate, 1};
}

std::vector<std::uint8_t> framed(const char type, const std::size_t body_length) {
  std::vector<std::uint8_t> bytes(body_length + 2, 0);
  bytes[0] = static_cast<std::uint8_t>(body_length >> 8U);
  bytes[1] = static_cast<std::uint8_t>(body_length & 0xffU);
  bytes[2] = static_cast<std::uint8_t>(type);
  return bytes;
}

std::vector<Event> execution_market() {
  return {
      event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      event(2, Add{1, Side::Buy, 20, 99, "AAPL", {}}),
      event(2, Add{2, Side::Sell, 5, 101, "AAPL", {}}),
      event(3, Add{3, Side::Sell, 10, 102, "AAPL", {}}),
      event(4, aegisx::Execute{1, 20, 9}),
      event(5, Add{4, Side::Buy, 10, 100, "AAPL", {}}),
      event(6, aegisx::Execute{4, 10, 10}),
  };
}

}  // namespace

TEST_CASE("ITCH parser validates known definitions, policy, fields, and streaming") {
  aegisx::NasdaqItchParser strict;
  const auto fixture = std::string(AEGISX_SOURCE_DIR) + "/data/fixtures/aegisx_itch_sample.itch";
  const auto parsed = strict.parse_file(fixture);
  REQUIRE(parsed);
  REQUIRE(parsed.events.size() == 11);
  CHECK(parsed.statistics.framed_messages == 11);
  CHECK(parsed.statistics.decoded_messages == 11);
  CHECK(parsed.statistics.bytes_processed == 349);
  REQUIRE(std::holds_alternative<aegisx::ExecuteWithPrice>(parsed.events[8].payload));
  const auto& execution = std::get<aegisx::ExecuteWithPrice>(parsed.events[8].payload);
  CHECK(execution.execution_price == 100'200);
  CHECK(execution.match_number == 3);
  CHECK(execution.printable);
  REQUIRE(std::holds_alternative<aegisx::Trade>(parsed.events[6].payload));
  const auto& trade = std::get<aegisx::Trade>(parsed.events[6].payload);
  CHECK(trade.order_id == 300);
  CHECK(trade.match_number == 2);

  const auto known_skipped = strict.parse(framed('Y', 20));
  REQUIRE(known_skipped);
  CHECK(known_skipped.statistics.skipped_messages == 1);
  CHECK(known_skipped.statistics.skipped_by_type.at('Y') == 1);
  CHECK_FALSE(strict.parse(framed('Y', 19)));
  CHECK_FALSE(strict.parse(framed('Z', 20)));
  aegisx::NasdaqItchParser permissive({aegisx::UnknownMessagePolicy::Permissive});
  const auto unknown = permissive.parse(framed('Z', 20));
  REQUIRE(unknown);
  CHECK(unknown.statistics.unknown_messages == 1);
  CHECK(unknown.statistics.skipped_messages == 1);
  CHECK_FALSE(strict.parse({0}));
  CHECK_FALSE(strict.parse({0, 1, 'S'}));

  std::size_t streamed_events = 0;
  const auto streamed = strict.parse_file(fixture, [&](const Event&) { ++streamed_events; });
  REQUIRE(streamed);
  CHECK(streamed.events.empty());
  CHECK(streamed_events == 11);
  CHECK(aegisx::sha256_file(fixture).size() == 64);
}

TEST_CASE("independent specification-derived ITCH reference segment spot checks decoded fields") {
  const auto fixture = std::string(AEGISX_SOURCE_DIR) + "/data/fixtures/itch_reference_segment.itch";
  aegisx::NasdaqItchParser parser;
  const auto parsed = parser.parse_file(fixture);
  REQUIRE(parsed);
  CHECK(aegisx::sha256_file(fixture) == "ccd343057636251f03de9a2986ec30d0ceadbe1aaa8bc70e8f3e301555021ffa");
  CHECK(parsed.statistics.framed_messages == 11);
  CHECK(parsed.statistics.decoded_messages == 10);
  CHECK(parsed.statistics.skipped_by_type.at('Y') == 1);
  REQUIRE(std::holds_alternative<aegisx::StockTradingAction>(parsed.events[2].payload));
  CHECK(std::get<aegisx::StockTradingAction>(parsed.events[2].payload).state == 'T');
  REQUIRE(std::holds_alternative<aegisx::Trade>(parsed.events[6].payload));
  CHECK(std::get<aegisx::Trade>(parsed.events[6].payload).match_number == 77);
  REQUIRE(std::holds_alternative<aegisx::CrossTrade>(parsed.events[7].payload));
  CHECK(std::get<aegisx::CrossTrade>(parsed.events[7].payload).quantity == 500);
  REQUIRE(std::holds_alternative<aegisx::BrokenTrade>(parsed.events[8].payload));
  CHECK(std::get<aegisx::BrokenTrade>(parsed.events[8].payload).match_number == 77);
}

TEST_CASE("market state rejects global duplicates, locate mismatches, and symbol conflicts") {
  aegisx::MarketState market;
  market.apply(event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}, 1));
  market.apply(event(1, aegisx::StockDirectory{"MSFT", 'Q', 'N', false, 100}, 2));
  market.apply(event(2, Add{10, Side::Buy, 100, 200'000, "AAPL", {}}, 1));
  CHECK_THROWS_WITH(market.apply(event(2, Add{10, Side::Sell, 100, 100'000, "MSFT", {}}, 2)),
                    "duplicate active order ID across instruments");
  CHECK_THROWS_WITH(market.apply(event(3, aegisx::Cancel{10, 1}, 2)), "stock locate does not match routed order");
  REQUIRE(market.book_for(1));
  CHECK(market.book_for(1)->depth_at(Side::Buy, 200'000) == 100);
  CHECK_THROWS_WITH(market.apply(event(4, Add{11, Side::Buy, 1, 1, "WRONG", {}}, 1)),
                    "stock locate symbol conflicts with existing directory mapping");
  CHECK(market.route_for(10) == 1);
  CHECK(market.invariant_error().empty());
}

TEST_CASE("order book replace preserves original state on invalid input and checks exact locators") {
  OrderBook book;
  book.add(1, Side::Buy, 10, 100);
  const auto before = book.full_order_state_checksum();
  CHECK_THROWS_WITH(book.replace(1, 2, 0, 101), "invalid replacement fields");
  REQUIRE(book.order(1));
  CHECK(book.order(1)->quantity == 10);
  CHECK(book.full_order_state_checksum() == before);
  book.replace(1, 2, 12, 99);
  CHECK_FALSE(book.order(1));
  REQUIRE(book.order(2));
  CHECK(book.fifo_at(Side::Buy, 99) == std::vector<OrderId>{2});
  CHECK(book.invariant_error().empty());
  CHECK(book.full_order_state_checksum() != book.aggregate_depth_checksum());
  CHECK(book.fifo_state_checksum() != 0);
}

TEST_CASE("randomized multi-mutation reference model validates FIFO and full state") {
  struct Reference {
    Side side;
    Quantity quantity;
    Price price;
  };
  for (const unsigned seed : {17U, 20260724U}) {
    std::map<OrderId, Reference> reference;
    std::map<Price, std::vector<OrderId>, std::greater<>> bids_fifo;
    std::map<Price, std::vector<OrderId>> asks_fifo;
    std::mt19937 random(seed);
    std::uniform_int_distribution<int> operation_distribution(0, 99);
    std::uniform_int_distribution<int> quantity_distribution(1, 200);
    std::uniform_int_distribution<int> price_distribution(99, 103);
    OrderBook book;
    OrderId next_id = 1;
    const auto queue_for = [&](const Side side, const Price price) -> std::vector<OrderId>& {
      return side == Side::Buy ? bids_fifo[price] : asks_fifo[price];
    };
    const auto remove_from_queue = [&](const OrderId id, const Reference& reference_order) {
      auto& queue = queue_for(reference_order.side, reference_order.price);
      const auto found = std::find(queue.begin(), queue.end(), id);
      REQUIRE(found != queue.end());
      queue.erase(found);
      if (queue.empty()) {
        if (reference_order.side == Side::Buy)
          bids_fifo.erase(reference_order.price);
        else
          asks_fifo.erase(reference_order.price);
      }
    };
    for (int iteration = 0; iteration < 10'000; ++iteration) {
      const int operation = operation_distribution(random);
      if (reference.empty() || operation < 45) {
        const Side side = operation_distribution(random) < 50 ? Side::Buy : Side::Sell;
        const Quantity quantity = quantity_distribution(random);
        const Price price = price_distribution(random);
        book.add(next_id, side, quantity, price);
        reference.emplace(next_id, Reference{side, quantity, price});
        queue_for(side, price).push_back(next_id++);
      } else {
        std::uniform_int_distribution<std::size_t> selection(0, reference.size() - 1);
        auto selected = reference.begin();
        std::advance(selected, static_cast<long>(selection(random)));
        if (operation < 70) {
          std::uniform_int_distribution<Quantity> amount(1, selected->second.quantity);
          const Quantity quantity = amount(random);
          if (operation < 58)
            book.cancel(selected->first, quantity);
          else
            book.execute(selected->first, quantity);
          selected->second.quantity -= quantity;
          if (selected->second.quantity == 0) {
            remove_from_queue(selected->first, selected->second);
            reference.erase(selected);
          }
        } else if (operation < 80) {
          remove_from_queue(selected->first, selected->second);
          book.erase(selected->first);
          reference.erase(selected);
        } else {
          const Reference previous = selected->second;
          const OrderId old_id = selected->first;
          const Quantity quantity = quantity_distribution(random);
          const Price price = price_distribution(random);
          book.replace(old_id, next_id, quantity, price);
          remove_from_queue(old_id, previous);
          reference.erase(selected);
          reference.emplace(next_id, Reference{previous.side, quantity, price});
          queue_for(previous.side, price).push_back(next_id++);
        }
      }
      REQUIRE(book.invariant_error().empty());
      CHECK(book.active_orders() == reference.size());
      for (const auto& [price, queue] : bids_fifo) CHECK(book.fifo_at(Side::Buy, price) == queue);
      for (const auto& [price, queue] : asks_fifo) CHECK(book.fifo_at(Side::Sell, price) == queue);
    }
  }
}

TEST_CASE("streaming replay is deterministic and keeps instruments isolated") {
  const std::vector<Event> events{
      event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}, 1),
      event(1, aegisx::StockDirectory{"MSFT", 'Q', 'N', false, 100}, 2),
      event(2, Add{1, Side::Buy, 10, 200, "AAPL", {}}, 1),
      event(2, Add{2, Side::Sell, 10, 100, "MSFT", {}}, 2),
  };
  aegisx::ReplayConfig config;
  config.stock_locate = 1;
  aegisx::MarketReplayEngine replay;
  const auto first = replay.run(events, config);
  const auto second = replay.run(events, config);
  REQUIRE_FALSE(first.snapshots.empty());
  CHECK(first.snapshots.back().symbol == "AAPL");
  CHECK(first.snapshots.back().bid == 200);
  CHECK_FALSE(first.snapshots.back().ask);
  CHECK(first.logical_checksum == second.logical_checksum);
  CHECK(first.full_state_checksum == second.full_state_checksum);
}

TEST_CASE("execution strategies use isolated liquidity, shadow consumption, and arrival benchmarks", "[execution]") {
  const auto market = execution_market();
  const aegisx::ParentOrder parent{1, 1, "AAPL", Side::Buy, 8, 2, 7};
  aegisx::ExecutionConfig config;
  config.intervals = 2;
  config.max_child_quantity = 8;
  config.vwap_weights = {0.25, 0.75};
  config.taker_fee_per_share = 1;
  aegisx::ExecutionSimulator simulator;
  const auto twap = simulator.run(market, parent, aegisx::Strategy::Twap, config);
  const auto vwap = simulator.run(market, parent, aegisx::Strategy::Vwap, config);
  REQUIRE(twap.filled <= parent.target_quantity);
  REQUIRE(vwap.filled <= parent.target_quantity);
  CHECK(twap.fees >= 0);
  CHECK(twap.average_price);
  CHECK(twap.implementation_shortfall_ticks);
  CHECK(twap.child_order_count > 0);
  CHECK(twap.depth_consumed <= 15);
  REQUIRE_FALSE(twap.children.empty());
  REQUIRE_FALSE(vwap.children.empty());
  CHECK(twap.children.front().submitted_quantity != vwap.children.front().submitted_quantity);
}

TEST_CASE("adaptive execution can choose passive children in a wide displayed market", "[execution]") {
  const std::vector<Event> market{
      event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      event(1, Add{1, Side::Buy, 10, 99, "AAPL", {}}),
      event(1, Add{3, Side::Sell, 20, 101, "AAPL", {}}),
      event(2, aegisx::System{'O'}),
      event(5, aegisx::System{'Q'}),
      event(6, aegisx::Execute{1, 10, 1}),
      event(7, Add{2, Side::Buy, 10, 99, "AAPL", {}}),
      event(8, aegisx::Execute{2, 10, 2}),
  };
  const aegisx::ParentOrder parent{1, 1, "AAPL", Side::Buy, 10, 2, 8};
  aegisx::ExecutionConfig config;
  config.intervals = 1;
  config.max_child_quantity = 10;
  config.force_completion_at_end = false;
  const auto adaptive = aegisx::ExecutionSimulator{}.run(market, parent, aegisx::Strategy::Adaptive, config);
  CHECK(adaptive.filled == parent.target_quantity);
  CHECK(adaptive.passive_ratio == 1.0);
  CHECK(adaptive.average_price == 99.0);
}

TEST_CASE("passive queue flow is conserved across multiple simulated children", "[execution]") {
  const std::vector<Event> market{
      event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      event(1, Add{1, Side::Buy, 20, 99, "AAPL", {}}),
      event(1, Add{2, Side::Sell, 20, 101, "AAPL", {}}),
      event(2, aegisx::System{'O'}),
      event(5, aegisx::System{'Q'}),
      event(6, aegisx::Execute{1, 20, 1}),
  };
  const aegisx::ParentOrder parent{7, 1, "AAPL", Side::Buy, 10, 2, 7};
  aegisx::ExecutionConfig config;
  config.intervals = 2;
  config.max_child_quantity = 5;
  config.force_completion_at_end = false;
  const auto report = aegisx::ExecutionSimulator{}.run(market, parent, aegisx::Strategy::Adaptive, config);
  REQUIRE(report.children.size() == 2);
  CHECK(report.children[0].initial_queue_ahead == 20);
  CHECK(report.children[0].queue_ahead == 0);
  CHECK(report.children[1].initial_queue_ahead == 25);
  CHECK(report.children[1].queue_ahead == 25);
  CHECK(report.filled == 0);
  CHECK(report.parent_state.filled + report.parent_state.open + report.parent_state.remaining_unsubmitted +
            report.parent_state.terminal_unfilled ==
        parent.target_quantity);
  CHECK(report.parent_state.terminal_unfilled == parent.target_quantity);
}

TEST_CASE("passive queue depletes ahead quantity before a partial maker fill", "[execution]") {
  const auto scenario = [](const bool include_second_execution) {
    std::vector<Event> events{
        event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
        event(1, Add{1, Side::Buy, 110, 99, "AAPL", {}}),
        event(1, Add{2, Side::Sell, 200, 101, "AAPL", {}}),
        event(2, aegisx::Cancel{1, 10}),
        event(3, aegisx::Execute{1, 80, 1}),
    };
    if (include_second_execution) {
      events.push_back(event(4, Add{3, Side::Buy, 30, 99, "AAPL", {}}));
      events.push_back(event(5, aegisx::Execute{3, 30, 2}));
    }
    return events;
  };
  aegisx::ExecutionConfig config;
  config.intervals = 1;
  config.max_child_quantity = 50;
  config.force_completion_at_end = false;
  const auto first = aegisx::ExecutionSimulator{}.run(scenario(false), {20, 1, "AAPL", Side::Buy, 50, 2, 3},
                                                      aegisx::Strategy::Adaptive, config);
  REQUIRE(first.children.size() == 1);
  CHECK(first.children.front().initial_queue_ahead == 100);
  CHECK(first.children.front().queue_ahead == 20);
  CHECK(first.filled == 0);
  const auto second = aegisx::ExecutionSimulator{}.run(scenario(true), {21, 1, "AAPL", Side::Buy, 50, 2, 5},
                                                       aegisx::Strategy::Adaptive, config);
  REQUIRE(second.children.size() == 1);
  CHECK(second.children.front().queue_ahead == 0);
  CHECK(second.filled == 10);
  CHECK(second.children.front().remaining_quantity == 40);
}

TEST_CASE("passive cancellation allocation supports zero half and full fractions", "[execution]") {
  const std::vector<Event> market{
      event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      event(1, Add{1, Side::Buy, 100, 99, "AAPL", {}}),
      event(1, Add{2, Side::Sell, 100, 101, "AAPL", {}}),
      event(2, aegisx::System{'O'}),
      event(3, aegisx::Cancel{1, 40}),
  };
  const auto queue_after = [&](const double fraction) {
    aegisx::ExecutionConfig config;
    config.intervals = 1;
    config.max_child_quantity = 50;
    config.force_completion_at_end = false;
    config.cancellation_queue_fraction = fraction;
    const auto report = aegisx::ExecutionSimulator{}.run(market, {22, 1, "AAPL", Side::Buy, 50, 2, 3},
                                                         aegisx::Strategy::Adaptive, config);
    REQUIRE(report.children.size() == 1);
    return report.children.front().queue_ahead;
  };
  CHECK(queue_after(0.0) == 100);
  CHECK(queue_after(0.5) == 80);
  CHECK(queue_after(1.0) == 60);
  aegisx::ExecutionConfig invalid;
  invalid.cancellation_queue_fraction = -0.1;
  CHECK_THROWS(aegisx::ExecutionSimulator{}.run(market, {23, 1, "AAPL", Side::Buy, 50, 2, 3},
                                                aegisx::Strategy::Adaptive, invalid));
  invalid.cancellation_queue_fraction = 1.1;
  CHECK_THROWS(aegisx::ExecutionSimulator{}.run(market, {24, 1, "AAPL", Side::Buy, 50, 2, 3},
                                                aegisx::Strategy::Adaptive, invalid));
}

TEST_CASE("execution records exact arrival state and explicit latency stages", "[execution]") {
  const auto market = execution_market();
  const aegisx::ParentOrder parent{9, 1, "AAPL", Side::Buy, 4, 2, 7};
  aegisx::ExecutionConfig config;
  config.intervals = 1;
  config.max_child_quantity = 4;
  config.decision_latency_ns = 1;
  config.transmission_latency_ns = 1;
  config.exchange_ack_latency_ns = 2;
  const auto report = aegisx::ExecutionSimulator{}.run(market, parent, aegisx::Strategy::Twap, config);
  REQUIRE(report.arrival_benchmark);
  CHECK(report.arrival_benchmark->bid == 99);
  CHECK(report.arrival_benchmark->ask == 101);
  CHECK(report.arrival_benchmark->mid == 100.0);
  REQUIRE_FALSE(report.children.empty());
  CHECK(report.children.front().submission_time == report.children.front().decision_time + 1);
  CHECK(report.children.front().exchange_arrival_time == report.children.front().submission_time + 1);
  CHECK(report.children.front().acknowledgment_time == report.children.front().exchange_arrival_time + 2);
  CHECK(report.parent_state.invariant_error().empty());
}

TEST_CASE("maker rebates and taker fees remain separate from gross execution cost", "[execution]") {
  const std::vector<Event> market{
      event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      event(1, Add{1, Side::Buy, 10, 99, "AAPL", {}}),
      event(1, Add{2, Side::Sell, 20, 101, "AAPL", {}}),
      event(2, aegisx::System{'O'}),
      event(3, aegisx::Execute{1, 10, 1}),
      event(4, Add{3, Side::Buy, 10, 99, "AAPL", {}}),
      event(5, aegisx::Execute{3, 10, 2}),
  };
  const aegisx::ParentOrder parent{25, 1, "AAPL", Side::Buy, 10, 2, 5};
  aegisx::ExecutionConfig maker_config;
  maker_config.intervals = 1;
  maker_config.max_child_quantity = 10;
  maker_config.force_completion_at_end = false;
  maker_config.maker_fee_per_share = -1;
  const auto maker = aegisx::ExecutionSimulator{}.run(market, parent, aegisx::Strategy::Adaptive, maker_config);
  CHECK(maker.filled == 10);
  CHECK(maker.fees == -10);
  CHECK(maker.fees_paid == 0);
  CHECK(maker.rebates == 10);
  REQUIRE(maker.gross_execution_cost_ticks);
  REQUIRE(maker.implementation_shortfall_currency);
  CHECK(*maker.implementation_shortfall_currency == *maker.gross_execution_cost_ticks - 10.0);

  aegisx::ExecutionConfig taker_config;
  taker_config.intervals = 1;
  taker_config.max_child_quantity = 10;
  taker_config.taker_fee_per_share = 2;
  const auto taker = aegisx::ExecutionSimulator{}.run(market, parent, aegisx::Strategy::Twap, taker_config);
  CHECK(taker.filled == 10);
  CHECK(taker.fees == 20);
  CHECK(taker.fees_paid == 20);
  CHECK(taker.rebates == 0);
}

TEST_CASE("aggressive child executes against activation-time liquidity after transmission latency", "[execution]") {
  const std::vector<Event> market{
      event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      event(1, Add{1, Side::Buy, 10, 99, "AAPL", {}}),
      event(1, Add{2, Side::Sell, 10, 101, "AAPL", {}}),
      event(2, aegisx::System{'O'}),
      event(3, aegisx::Replace{2, 3, 10, 105}),
      event(4, aegisx::System{'Q'}),
  };
  aegisx::ExecutionConfig config;
  config.intervals = 1;
  config.max_child_quantity = 5;
  config.transmission_latency_ns = 2;
  config.force_completion_at_end = false;
  const auto report =
      aegisx::ExecutionSimulator{}.run(market, {26, 1, "AAPL", Side::Buy, 5, 2, 4}, aegisx::Strategy::Twap, config);
  REQUIRE(report.average_price);
  CHECK(*report.average_price == 105.0);
  REQUIRE_FALSE(report.fills.empty());
  CHECK(report.fills.front().timestamp == 4);
  CHECK(report.fills.front().timestamp >= report.children.front().exchange_arrival_time);
}

TEST_CASE("sell execution is side symmetric and walks displayed bids", "[execution]") {
  const std::vector<Event> market{
      event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      event(1, Add{1, Side::Buy, 3, 100, "AAPL", {}}),
      event(1, Add{2, Side::Buy, 4, 99, "AAPL", {}}),
      event(1, Add{3, Side::Sell, 10, 102, "AAPL", {}}),
      event(2, aegisx::System{'O'}),
  };
  aegisx::ExecutionConfig config;
  config.intervals = 1;
  config.max_child_quantity = 5;
  const auto report =
      aegisx::ExecutionSimulator{}.run(market, {27, 1, "AAPL", Side::Sell, 5, 2, 3}, aegisx::Strategy::Twap, config);
  REQUIRE(report.average_price);
  CHECK(*report.average_price == 99.6);
  CHECK(report.filled == 5);
  CHECK(report.depth_consumed == 5);
}

TEST_CASE("risk reservations, marked PnL, rate limits, and kill switch are enforced", "[risk]") {
  aegisx::RiskLimits limits;
  limits.max_order_quantity = 10;
  limits.max_open_order_exposure_ticks = 1'500;
  limits.max_orders_per_window = 1;
  limits.max_drawdown_ticks = 20;
  aegisx::RiskEngine risk(limits);
  risk.mark(1, "AAPL", 100, 1);
  const auto accepted = risk.approve({"one", 1, "AAPL", Side::Buy, 10, 100, 2});
  REQUIRE(accepted.approved);
  CHECK(risk.reserved_exposure() == 1'000);
  const auto rate_limited = risk.approve({"two", 1, "AAPL", Side::Buy, 1, 100, 3});
  CHECK(rate_limited.reason == aegisx::RiskRejectReason::OrderRateLimit);
  risk.fill("one", 10, 100, 5);
  CHECK(risk.reserved_exposure() == 0);
  risk.mark(1, "AAPL", 90, 4);
  REQUIRE(risk.position(1));
  CHECK(risk.position(1)->unrealized_pnl == -100);
  risk.kill(true);
  CHECK(risk.approve({"three", 1, "AAPL", Side::Buy, 1, 90, 5}).reason == aegisx::RiskRejectReason::KillSwitchActive);
  CHECK_FALSE(risk.audit_log().empty());
  CHECK(risk.position(1)->total_pnl == risk.position(1)->realized_pnl + risk.position(1)->unrealized_pnl);
}

TEST_CASE("risk includes reservations in gross and net exposure and caps open order count", "[risk]") {
  aegisx::RiskLimits limits;
  limits.max_orders_per_window = 100;
  limits.max_open_orders = 1;
  limits.max_order_quantity = 100;
  limits.max_position = 1'000;
  limits.max_open_order_exposure_ticks = 1'000'000;
  limits.max_gross_exposure_ticks = 1'000'000;
  limits.max_net_exposure_ticks = 1'000'000;
  limits.max_symbol_exposure_ticks = 1'000'000;
  aegisx::RiskEngine risk(limits);
  risk.mark(1, "AAPL", 100, 1);
  REQUIRE(risk.approve({"one", 1, "AAPL", Side::Buy, 10, 100, 2}).approved);
  CHECK(risk.net_exposure() == 1'000);
  CHECK(risk.open_order_count() == 1);
  const auto rejected = risk.approve({"two", 1, "AAPL", Side::Sell, 1, 100, 3});
  CHECK(rejected.reason == aegisx::RiskRejectReason::OpenOrderExposureLimit);
  CHECK(risk.open_order_count() == 1);
}

TEST_CASE("execution risk rejection leaves parent conservation intact", "[execution][risk]") {
  const auto market = execution_market();
  aegisx::RiskLimits limits;
  limits.max_order_quantity = 2;
  limits.max_parent_quantity = 10;
  limits.max_orders_per_window = 100;
  aegisx::RiskEngine risk(limits);
  aegisx::ExecutionConfig config;
  config.intervals = 1;
  config.max_child_quantity = 4;
  config.force_completion_at_end = false;
  config.risk_engine = &risk;
  const aegisx::ParentOrder parent{11, 1, "AAPL", Side::Buy, 4, 2, 7};
  const auto report = aegisx::ExecutionSimulator{}.run(market, parent, aegisx::Strategy::Twap, config);
  CHECK(report.filled == 0);
  CHECK(report.rejected_child_count > 0);
  CHECK(report.parent_state.terminal_unfilled == parent.target_quantity);
  CHECK(report.parent_state.invariant_error().empty());
  CHECK(risk.open_order_count() == 0);
}

TEST_CASE("position accounting handles increase reduction reversal and close", "[risk]") {
  aegisx::RiskEngine risk;
  risk.mark(1, "AAPL", 100, 1);
  risk.fill(1, "AAPL", Side::Buy, 10, 100);
  risk.fill(1, "AAPL", Side::Sell, 4, 110);
  REQUIRE(risk.position(1));
  CHECK(risk.position(1)->net_quantity == 6);
  CHECK(risk.position(1)->average_open_price == 100);
  CHECK(risk.position(1)->realized_pnl == 40);
  risk.fill(1, "AAPL", Side::Sell, 10, 90);
  CHECK(risk.position(1)->net_quantity == -4);
  CHECK(risk.position(1)->average_open_price == 90);
  CHECK(risk.position(1)->realized_pnl == -20);
  risk.fill(1, "AAPL", Side::Buy, 4, 80);
  CHECK(risk.position(1)->net_quantity == 0);
  CHECK(risk.position(1)->average_open_price == 0);
  CHECK(risk.position(1)->realized_pnl == 20);
  CHECK(risk.position(1)->total_pnl == 20);
}
