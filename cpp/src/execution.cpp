#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "aegisx/aegisx.h"

namespace aegisx {
namespace {

using ShadowLevel = std::pair<Side, Price>;

Quantity ceiling_divide(const Quantity numerator, const Quantity denominator) {
  if (numerator < 0 || denominator <= 0) throw std::runtime_error("invalid schedule division");
  return numerator == 0 ? 0 : 1 + (numerator - 1) / denominator;
}

Quantity bounded_child_quantity(const Quantity quantity, const ExecutionConfig& config) {
  if (config.max_child_quantity <= 0) throw std::runtime_error("max_child_quantity must be positive");
  return std::min(quantity, config.max_child_quantity);
}

Money checked_money_product(const Money lhs, const Quantity rhs, const char* context) {
  if (lhs == 0 || rhs == 0) return 0;
  if ((lhs == -1 && rhs == std::numeric_limits<Quantity>::min()) ||
      (rhs == -1 && lhs == std::numeric_limits<Money>::min())) {
    throw std::runtime_error(context);
  }
  if ((lhs > 0 && rhs > 0 && lhs > std::numeric_limits<Money>::max() / rhs) ||
      (lhs > 0 && rhs < 0 && rhs < std::numeric_limits<Money>::min() / lhs) ||
      (lhs < 0 && rhs > 0 && lhs < std::numeric_limits<Money>::min() / rhs) ||
      (lhs < 0 && rhs < 0 && lhs < std::numeric_limits<Money>::max() / rhs)) {
    throw std::runtime_error(context);
  }
  return lhs * rhs;
}

Quantity scheduled_fraction(const Quantity target, const std::size_t completed, const std::size_t intervals) {
  if (target < 0 || intervals == 0 || completed > intervals) throw std::runtime_error("invalid schedule fraction");
  const auto completed_quantity = static_cast<Quantity>(completed);
  const auto interval_quantity = static_cast<Quantity>(intervals);
  if (completed_quantity > 0 && target > std::numeric_limits<Quantity>::max() / completed_quantity) {
    throw std::runtime_error("schedule quantity overflow");
  }
  return ceiling_divide(target * completed_quantity, interval_quantity);
}

std::string risk_request_id(const ParentOrderId parent_id, const ChildOrderId child_id) {
  return "execution-" + std::to_string(parent_id) + "-" + std::to_string(child_id);
}

Timestamp saturating_add(const Timestamp lhs, const Timestamp rhs) {
  return rhs > std::numeric_limits<Timestamp>::max() - lhs ? std::numeric_limits<Timestamp>::max() : lhs + rhs;
}

std::optional<double> book_imbalance(const OrderBook& book) {
  Quantity bids = 0;
  Quantity asks = 0;
  for (const auto& level : book.top(Side::Buy, 5)) bids += level.quantity;
  for (const auto& level : book.top(Side::Sell, 5)) asks += level.quantity;
  if (bids + asks == 0) return std::nullopt;
  return static_cast<double>(bids - asks) / static_cast<double>(bids + asks);
}

ArrivalBenchmark capture_arrival(const MarketState& state, const ParentOrder& parent) {
  ArrivalBenchmark benchmark;
  benchmark.captured_at = parent.arrival_time;
  const auto* book = state.book_for(parent.stock_locate);
  if (book == nullptr) return benchmark;
  benchmark.bid = book->best_bid();
  benchmark.ask = book->best_ask();
  benchmark.mid = book->mid_price();
  benchmark.microprice = book->microprice();
  if (benchmark.bid && benchmark.ask) benchmark.spread = *benchmark.ask - *benchmark.bid;
  for (const auto& level : book->top(Side::Buy, 5)) benchmark.displayed_bid_depth += level.quantity;
  for (const auto& level : book->top(Side::Sell, 5)) benchmark.displayed_ask_depth += level.quantity;
  return benchmark;
}

}  // namespace

std::string ParentOrderState::invariant_error() const {
  if (target < 0 || scheduled < 0 || scheduled > target || cumulative_submitted < 0 || open < 0 || filled < 0 ||
      cumulative_cancelled < 0 || remaining_unsubmitted < 0 || terminal_unfilled < 0) {
    return "negative or out-of-range parent-order quantity";
  }
  if (filled + open + remaining_unsubmitted + terminal_unfilled != target) {
    return "target != filled + open + remaining_unsubmitted + terminal_unfilled";
  }
  return {};
}

ExecutionReport ExecutionSimulator::run(const std::vector<Event>& events, const ParentOrder& parent,
                                        const Strategy strategy, const ExecutionConfig& config) const {
  if (parent.id == 0 || parent.stock_locate == 0 || parent.symbol.empty() || parent.target_quantity <= 0 ||
      parent.end_time <= parent.arrival_time || config.intervals == 0 || config.pov_rate < 0.0 ||
      config.pov_rate > 1.0 || config.minimum_pov_child_quantity <= 0 || config.max_aggressive_levels == 0 ||
      config.cancellation_queue_fraction < 0.0 || config.cancellation_queue_fraction > 1.0 ||
      config.taker_fee_bps < 0.0) {
    throw std::runtime_error("invalid execution configuration");
  }

  const std::vector<double>* vwap_weights = &config.vwap_weights;
  if (config.vwap_profile) {
    if (config.vwap_profile->training_session.empty() || config.vwap_profile->evaluation_session.empty() ||
        config.vwap_profile->training_session == config.vwap_profile->evaluation_session ||
        config.vwap_profile->source_checksum.empty()) {
      throw std::runtime_error("VWAP profile must identify distinct training/evaluation sessions and checksum");
    }
    vwap_weights = &config.vwap_profile->interval_weights;
  }
  if (strategy == Strategy::Vwap && vwap_weights->size() != config.intervals) {
    throw std::runtime_error("VWAP weights must match interval count");
  }

  MarketState historical;
  MarketState visible;
  ExecutionReport report;
  report.strategy = strategy;
  report.parent_order_id = parent.id;
  report.stock_locate = parent.stock_locate;
  report.symbol = parent.symbol;
  report.side = parent.side;
  report.unfilled = parent.target_quantity;
  report.parent_state.target = parent.target_quantity;
  report.parent_state.remaining_unsubmitted = parent.target_quantity;

  if (config.risk_engine != nullptr && parent.target_quantity > config.risk_engine->limits().max_parent_quantity) {
    report.parent_state.terminal_unfilled = parent.target_quantity;
    report.parent_state.remaining_unsubmitted = 0;
    report.rejected_child_count = 1;
    report.decisions.push_back({parent.arrival_time, ExecutionAction::Wait, 0, 0, 0, parent.target_quantity,
                                std::nullopt, std::nullopt, std::nullopt, "parent_quantity_limit"});
    return report;
  }

  std::map<ShadowLevel, Quantity> shadow_consumed;
  std::map<MatchNumber, std::pair<Price, Quantity>> printable_trades;
  std::vector<std::pair<Timestamp, double>> midpoint_series;
  std::size_t visible_index = 0;
  ChildOrderId next_child_id = 1;
  double gross_notional = 0.0;
  Quantity observed_trade_volume = 0;
  bool completion_finalized = false;
  std::optional<double> end_mid;

  const auto total_open = [&]() {
    Quantity open = 0;
    for (const auto& child : report.children) {
      if (child.state == ChildOrderState::Pending || child.state == ChildOrderState::Open)
        open += child.remaining_quantity;
    }
    return open;
  };
  const auto reconcile_parent = [&](const bool terminal = false) {
    report.parent_state.open = total_open();
    report.parent_state.filled = report.filled;
    if (terminal) {
      report.parent_state.terminal_unfilled =
          parent.target_quantity - report.parent_state.filled - report.parent_state.open;
      report.parent_state.remaining_unsubmitted = 0;
    } else {
      report.parent_state.terminal_unfilled = 0;
      report.parent_state.remaining_unsubmitted =
          parent.target_quantity - report.parent_state.filled - report.parent_state.open;
    }
    const auto error = report.parent_state.invariant_error();
    if (!error.empty()) throw std::runtime_error("parent-order conservation failure: " + error);
  };
  const auto current_mid = [&]() -> std::optional<double> {
    const auto* book = historical.book_for(parent.stock_locate);
    return book == nullptr ? std::nullopt : book->mid_price();
  };
  const auto append_fill = [&](ChildOrder& child, const Timestamp timestamp, const Price price, const Quantity quantity,
                               const LiquidityRole role) {
    if (quantity <= 0 || quantity > child.remaining_quantity) throw std::runtime_error("invalid simulated fill");
    const Money fee_per_share = role == LiquidityRole::Maker ? config.maker_fee_per_share : config.taker_fee_per_share;
    const double fee_bps = role == LiquidityRole::Maker ? config.maker_fee_bps : config.taker_fee_bps;
    const Money per_share_fee = checked_money_product(fee_per_share, quantity, "execution fee overflow");
    const Money notional_fee = static_cast<Money>(
        std::llround(static_cast<double>(price) * static_cast<double>(quantity) * fee_bps / 10'000.0));
    const Money fee = per_share_fee + notional_fee;
    const Quantity before = child.remaining_quantity;
    Fill fill{child.id, child.parent_id, timestamp, price, quantity, role, fee, std::nullopt, {}};
    fill.pre_fill_mid = current_mid();
    report.fills.push_back(std::move(fill));
    child.remaining_quantity -= quantity;
    child.state = child.remaining_quantity == 0 ? ChildOrderState::Filled : ChildOrderState::Open;
    child.queue_history.push_back({timestamp, QueueEventType::Fill, quantity, child.queue_ahead, child.queue_ahead,
                                   before, child.remaining_quantity});
    report.filled += quantity;
    report.unfilled = parent.target_quantity - report.filled;
    report.fees += fee;
    if (fee >= 0)
      report.fees_paid += fee;
    else
      report.rebates += -fee;
    report.depth_consumed += role == LiquidityRole::Taker ? quantity : 0;
    gross_notional += static_cast<double>(price) * static_cast<double>(quantity);
    if (config.risk_engine != nullptr)
      config.risk_engine->fill(risk_request_id(parent.id, child.id), quantity, price, fee, timestamp);
    if (report.filled == parent.target_quantity && !report.completion_time_ns) report.completion_time_ns = timestamp;
    reconcile_parent();
  };
  const auto process_passive_flow = [&](const Event& event) {
    const auto* book = historical.book_for(parent.stock_locate);
    if (book == nullptr || event.stock_locate != parent.stock_locate) return;
    std::optional<OrderView> resting;
    Quantity flow = 0;
    bool cancellation = false;
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, Execute> || std::is_same_v<T, ExecuteWithPrice>) {
            resting = book->order(value.id);
            flow = value.quantity;
          } else if constexpr (std::is_same_v<T, Cancel>) {
            resting = book->order(value.id);
            flow = value.quantity;
            cancellation = true;
          }
        },
        event.payload);
    if (!resting || flow <= 0) return;
    Quantity shared_flow =
        cancellation ? static_cast<Quantity>(std::floor(static_cast<double>(flow) * config.cancellation_queue_fraction))
                     : flow;
    for (auto& child : report.children) {
      if (shared_flow <= 0) break;
      if (child.state != ChildOrderState::Open || child.type != OrderType::Limit || child.side != resting->side ||
          child.limit_price != resting->price)
        continue;
      const Quantity queue_before = child.queue_ahead;
      const Quantity own_before = child.remaining_quantity;
      const Quantity ahead_reduction = std::min(child.queue_ahead, shared_flow);
      child.queue_ahead -= ahead_reduction;
      shared_flow -= ahead_reduction;
      child.queue_history.push_back({event.timestamp_ns,
                                     cancellation ? QueueEventType::Cancellation : QueueEventType::Execution,
                                     ahead_reduction, queue_before, child.queue_ahead, own_before, own_before});
      if (!cancellation && child.queue_ahead == 0 && shared_flow > 0) {
        const Quantity fill_quantity = std::min(child.remaining_quantity, shared_flow);
        append_fill(child, event.timestamp_ns, child.limit_price, fill_quantity, LiquidityRole::Maker);
        shared_flow -= fill_quantity;
      }
    }
  };
  const auto activate_children = [&](const Timestamp timestamp) {
    const auto* book = historical.book_for(parent.stock_locate);
    if (book == nullptr) return;
    for (auto& child : report.children) {
      if (child.state != ChildOrderState::Pending || child.exchange_arrival_time > timestamp) continue;
      if (child.type == OrderType::Limit) {
        Quantity own_ahead = 0;
        for (const auto& earlier : report.children) {
          if (earlier.id == child.id) break;
          if (earlier.state == ChildOrderState::Open && earlier.type == OrderType::Limit &&
              earlier.side == child.side && earlier.limit_price == child.limit_price)
            own_ahead += earlier.remaining_quantity;
        }
        child.queue_ahead = book->depth_at(child.side, child.limit_price) + own_ahead;
        child.initial_queue_ahead = child.queue_ahead;
        child.state = ChildOrderState::Open;
        child.queue_history.push_back({timestamp, QueueEventType::Joined, 0, 0, child.queue_ahead,
                                       child.remaining_quantity, child.remaining_quantity});
        reconcile_parent();
        continue;
      }
      child.state = ChildOrderState::Open;
      const Side opposite = child.side == Side::Buy ? Side::Sell : Side::Buy;
      std::size_t walked = 0;
      for (const auto& level : book->top(opposite, config.max_aggressive_levels)) {
        if (child.remaining_quantity == 0) break;
        if (config.aggressive_limit_price &&
            ((child.side == Side::Buy && level.price > *config.aggressive_limit_price) ||
             (child.side == Side::Sell && level.price < *config.aggressive_limit_price)))
          break;
        ++walked;
        const ShadowLevel key{opposite, level.price};
        const Quantity available = std::max<Quantity>(0, level.quantity - shadow_consumed[key]);
        const Quantity filled = std::min(child.remaining_quantity, available);
        if (filled == 0) continue;
        shadow_consumed[key] += filled;
        append_fill(child, timestamp, level.price, filled, LiquidityRole::Taker);
      }
      static_cast<void>(walked);
      reconcile_parent();
    }
  };
  const auto submit_child = [&](const Timestamp decision_time, const OrderType type, const Quantity desired,
                                const std::string& reason) {
    const auto* decision_book = visible.book_for(parent.stock_locate);
    if (decision_book == nullptr || desired <= 0) return;
    const Quantity available = parent.target_quantity - report.filled - total_open();
    const Quantity quantity = bounded_child_quantity(std::min(desired, available), config);
    if (quantity <= 0) return;
    const auto passive_price = parent.side == Side::Buy ? decision_book->best_bid() : decision_book->best_ask();
    const auto aggressive_price = parent.side == Side::Buy ? decision_book->best_ask() : decision_book->best_bid();
    const auto selected_price = type == OrderType::Limit ? passive_price : aggressive_price;
    if (!selected_price) return;
    const ChildOrderId id = next_child_id++;
    const Timestamp submission = saturating_add(decision_time, config.decision_latency_ns);
    const Timestamp exchange_arrival = saturating_add(submission, config.transmission_latency_ns);
    const Timestamp acknowledgment = saturating_add(exchange_arrival, config.exchange_ack_latency_ns);
    ChildOrder child{id,
                     parent.id,
                     parent.side,
                     type,
                     type == OrderType::Limit ? *selected_price : 0,
                     quantity,
                     quantity,
                     submission,
                     ChildOrderState::Pending,
                     0,
                     parent.stock_locate,
                     parent.symbol,
                     decision_time,
                     exchange_arrival,
                     acknowledgment,
                     std::nullopt,
                     0,
                     {}};
    if (config.risk_engine != nullptr) {
      const auto decision =
          config.risk_engine->approve({risk_request_id(parent.id, id), parent.stock_locate, parent.symbol, parent.side,
                                       quantity, *selected_price, decision_time});
      if (!decision.approved) {
        child.state = ChildOrderState::Rejected;
        report.children.push_back(std::move(child));
        ++report.rejected_child_count;
        report.decisions.push_back({decision_time, ExecutionAction::Wait, report.parent_state.scheduled, report.filled,
                                    total_open(), quantity, decision_book->best_bid(), decision_book->best_ask(),
                                    book_imbalance(*decision_book), "risk_reject:" + to_string(decision.reason)});
        reconcile_parent();
        return;
      }
    }
    report.parent_state.cumulative_submitted += quantity;
    report.children.push_back(std::move(child));
    report.decisions.push_back(
        {decision_time, type == OrderType::Limit ? ExecutionAction::SubmitPassive : ExecutionAction::SubmitAggressive,
         report.parent_state.scheduled, report.filled, total_open(), quantity, decision_book->best_bid(),
         decision_book->best_ask(), book_imbalance(*decision_book), reason});
    reconcile_parent();
  };
  const auto cancel_open_children = [&](const Timestamp timestamp, const std::string& reason,
                                        const bool passive_only = false) {
    for (auto& child : report.children) {
      if (child.state != ChildOrderState::Pending && child.state != ChildOrderState::Open) continue;
      if (passive_only && child.type != OrderType::Limit) continue;
      const Quantity remaining = child.remaining_quantity;
      child.state = ChildOrderState::Cancelled;
      child.cancellation_time = timestamp;
      child.queue_history.push_back(
          {timestamp, QueueEventType::Cancelled, 0, child.queue_ahead, child.queue_ahead, remaining, remaining});
      report.parent_state.cumulative_cancelled += remaining;
      ++report.cancel_count;
      if (config.risk_engine != nullptr) config.risk_engine->release(risk_request_id(parent.id, child.id), timestamp);
      report.decisions.push_back({timestamp, ExecutionAction::CancelPassive, report.parent_state.scheduled,
                                  report.filled, total_open(), remaining, std::nullopt, std::nullopt, std::nullopt,
                                  reason});
    }
    reconcile_parent();
  };
  const auto scheduled_target = [&](const Timestamp timestamp) {
    const Timestamp elapsed = std::min(timestamp - parent.arrival_time, parent.end_time - parent.arrival_time);
    const Timestamp duration = parent.end_time - parent.arrival_time;
    const auto completed =
        std::min<std::size_t>(config.intervals, static_cast<std::size_t>((elapsed * config.intervals) / duration) + 1U);
    if (strategy == Strategy::Vwap) {
      double total_weight = 0.0;
      for (const double weight : *vwap_weights) {
        if (weight < 0.0) throw std::runtime_error("negative VWAP weight");
        total_weight += weight;
      }
      if (total_weight <= 0.0) throw std::runtime_error("zero VWAP weights");
      double cumulative = 0.0;
      for (std::size_t index = 0; index < completed; ++index) cumulative += (*vwap_weights)[index] / total_weight;
      return std::min(parent.target_quantity,
                      static_cast<Quantity>(std::ceil(cumulative * static_cast<double>(parent.target_quantity))));
    }
    return scheduled_fraction(parent.target_quantity, completed, config.intervals);
  };
  const auto finalize_completion = [&](const Timestamp timestamp) {
    if (completion_finalized) return;
    const auto* completion_book = historical.book_for(parent.stock_locate);
    if (completion_book != nullptr) end_mid = completion_book->mid_price();
    report.parent_state.scheduled = parent.target_quantity;
    const bool force = config.force_completion_at_end && config.completion_policy == CompletionPolicy::ForceMarket;
    if (force && report.filled < parent.target_quantity) {
      cancel_open_children(timestamp, "completion_policy");
      submit_child(timestamp, OrderType::Market, parent.target_quantity - report.filled, "forced_completion");
      activate_children(timestamp);
    }
    cancel_open_children(timestamp, "terminal_cleanup");
    reconcile_parent(true);
    completion_finalized = true;
  };

  for (std::size_t event_index = 0; event_index < events.size(); ++event_index) {
    const auto& event = events[event_index];
    if (!completion_finalized && event.timestamp_ns > parent.end_time) finalize_completion(parent.end_time);

    if (!completion_finalized) process_passive_flow(event);

    std::optional<std::pair<Price, Quantity>> execution_trade;
    MatchNumber execution_match = 0;
    if (event.stock_locate == parent.stock_locate) {
      const auto* before = historical.book_for(parent.stock_locate);
      std::visit(
          [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Execute>) {
              if (before != nullptr) {
                const auto order = before->order(value.id);
                if (order) execution_trade = std::pair<Price, Quantity>{order->price, value.quantity};
              }
              execution_match = value.match_number;
            } else if constexpr (std::is_same_v<T, ExecuteWithPrice>) {
              if (value.printable) execution_trade = std::pair<Price, Quantity>{value.execution_price, value.quantity};
              execution_match = value.match_number;
            }
          },
          event.payload);
    }

    historical.apply(event);
    if (const auto* add = std::get_if<Add>(&event.payload);
        add != nullptr && event.stock_locate == parent.stock_locate) {
      const ShadowLevel key{add->side, add->price};
      const auto found = shadow_consumed.find(key);
      if (found != shadow_consumed.end()) {
        found->second = std::max<Quantity>(0, found->second - add->quantity);
        if (found->second == 0) shadow_consumed.erase(found);
      }
    }
    if (execution_trade && event.timestamp_ns >= parent.arrival_time && event.timestamp_ns <= parent.end_time)
      printable_trades[execution_match] = *execution_trade;
    if (const auto* trade = std::get_if<Trade>(&event.payload);
        trade != nullptr && trade->symbol == parent.symbol && trade->printable &&
        event.timestamp_ns >= parent.arrival_time && event.timestamp_ns <= parent.end_time)
      printable_trades[trade->match_number] = {trade->price, trade->quantity};
    if (const auto* broken = std::get_if<BrokenTrade>(&event.payload); broken != nullptr)
      printable_trades.erase(broken->match_number);

    if (event.stock_locate == parent.stock_locate) {
      const auto symbol = historical.symbol_for(parent.stock_locate);
      if (symbol && *symbol != parent.symbol)
        throw std::runtime_error("parent symbol conflicts with stock locate directory");
      const auto* book = historical.book_for(parent.stock_locate);
      if (book != nullptr) {
        if (!report.arrival_benchmark && event.timestamp_ns >= parent.arrival_time && book->mid_price())
          report.arrival_benchmark = capture_arrival(historical, parent);
        if (const auto mid = book->mid_price()) midpoint_series.emplace_back(event.timestamp_ns, *mid);
      }
    }

    while (visible_index <= event_index) {
      const auto& candidate = events[visible_index];
      if (saturating_add(candidate.timestamp_ns, config.market_data_latency_ns) > event.timestamp_ns) break;
      visible.apply(candidate);
      ++visible_index;
    }
    if (config.risk_engine != nullptr) {
      const auto* visible_book = visible.book_for(parent.stock_locate);
      if (visible_book != nullptr) {
        if (const auto mid = visible_book->mid_price())
          config.risk_engine->mark(parent.stock_locate, parent.symbol, static_cast<Price>(std::llround(*mid)),
                                   event.timestamp_ns);
      }
    }

    if (completion_finalized || event.timestamp_ns < parent.arrival_time || event.timestamp_ns > parent.end_time)
      continue;
    activate_children(event.timestamp_ns);
    if (event.stock_locate != parent.stock_locate) continue;
    const auto* decision_book = visible.book_for(parent.stock_locate);
    if (decision_book == nullptr) continue;
    const bool adaptive_urgent = strategy == Strategy::Adaptive &&
                                 parent.end_time - event.timestamp_ns <= (parent.end_time - parent.arrival_time) / 4U;
    if (adaptive_urgent && config.adaptive_cancel_replace_on_urgency)
      cancel_open_children(event.timestamp_ns, "adaptive_urgency_cancel_replace", true);

    Quantity desired = 0;
    if (strategy == Strategy::Pov) {
      Quantity new_volume = 0;
      if (const auto* trade = std::get_if<Trade>(&event.payload);
          trade != nullptr && trade->symbol == parent.symbol && trade->printable)
        new_volume = trade->quantity;
      if (const auto* execution = std::get_if<Execute>(&event.payload);
          execution != nullptr && event.stock_locate == parent.stock_locate)
        new_volume = execution->quantity;
      if (const auto* execution = std::get_if<ExecuteWithPrice>(&event.payload);
          execution != nullptr && event.stock_locate == parent.stock_locate && execution->printable)
        new_volume = execution->quantity;
      observed_trade_volume += new_volume;
      const auto participation_target =
          static_cast<Quantity>(std::floor(static_cast<double>(observed_trade_volume) * config.pov_rate));
      desired = std::max<Quantity>(0, participation_target - report.filled - total_open());
      if (desired < config.minimum_pov_child_quantity) desired = 0;
      report.parent_state.scheduled = std::min(parent.target_quantity, participation_target);
    } else {
      report.parent_state.scheduled = scheduled_target(event.timestamp_ns);
      desired = std::max<Quantity>(0, report.parent_state.scheduled - report.filled - total_open());
    }
    report.maximum_schedule_deviation =
        std::max(report.maximum_schedule_deviation,
                 std::abs(static_cast<double>(report.filled + total_open() - report.parent_state.scheduled)));
    if (desired <= 0) {
      report.decisions.push_back({event.timestamp_ns, ExecutionAction::Wait, report.parent_state.scheduled,
                                  report.filled, total_open(), 0, decision_book->best_bid(), decision_book->best_ask(),
                                  book_imbalance(*decision_book), "on_schedule"});
      continue;
    }

    OrderType type = OrderType::Market;
    std::string reason = strategy == Strategy::Twap   ? "twap_slice"
                         : strategy == Strategy::Vwap ? "prior_session_volume_profile"
                         : strategy == Strategy::Pov  ? "instrument_printable_volume"
                                                      : "adaptive_urgency";
    if (strategy == Strategy::Adaptive) {
      const Quantity expected = scheduled_target(event.timestamp_ns);
      const bool materially_behind = report.filled + total_open() + config.max_child_quantity < expected;
      const auto bid = decision_book->best_bid();
      const auto ask = decision_book->best_ask();
      const bool wide_spread = bid && ask && *ask - *bid > 1;
      const auto imbalance = book_imbalance(*decision_book);
      const bool favorable_queue = !imbalance || (parent.side == Side::Buy ? *imbalance >= -0.5 : *imbalance <= 0.5);
      type = !materially_behind && !adaptive_urgent && wide_spread && favorable_queue ? OrderType::Limit
                                                                                      : OrderType::Market;
      reason = type == OrderType::Limit ? "adaptive_passive:wide_spread_favorable_queue"
                                        : "adaptive_aggressive:urgency_or_schedule_deficit";
    }
    submit_child(event.timestamp_ns, type, desired, reason);
    activate_children(event.timestamp_ns);
  }

  if (!report.arrival_benchmark) report.arrival_benchmark = capture_arrival(historical, parent);
  finalize_completion(parent.end_time);

  report.fill_rate = static_cast<double>(report.filled) / static_cast<double>(parent.target_quantity);
  report.child_order_count = report.children.size();
  if (report.filled > 0) report.average_price = gross_notional / static_cast<double>(report.filled);
  const auto arrival_mid = report.arrival_benchmark ? report.arrival_benchmark->mid : std::nullopt;
  if (arrival_mid && report.average_price) {
    report.implementation_shortfall_ticks =
        parent.side == Side::Buy ? *report.average_price - *arrival_mid : *arrival_mid - *report.average_price;
    if (*arrival_mid != 0.0)
      report.implementation_shortfall_bps = *report.implementation_shortfall_ticks / *arrival_mid * 10'000.0;
  }
  if (end_mid && report.unfilled > 0 && arrival_mid) {
    const double missed_cost = parent.side == Side::Buy ? *end_mid - *arrival_mid : *arrival_mid - *end_mid;
    report.opportunity_cost_ticks =
        static_cast<Money>(std::llround(std::max(0.0, missed_cost) * static_cast<double>(report.unfilled)));
  }
  if (report.filled > 0) {
    Quantity passive = 0;
    double spread_cost = 0.0;
    for (const auto& fill : report.fills) {
      if (fill.liquidity_role == LiquidityRole::Maker) passive += fill.quantity;
      if (fill.pre_fill_mid) {
        const double signed_cost = parent.side == Side::Buy ? static_cast<double>(fill.price) - *fill.pre_fill_mid
                                                            : *fill.pre_fill_mid - static_cast<double>(fill.price);
        spread_cost += signed_cost * static_cast<double>(fill.quantity);
      }
    }
    report.passive_ratio = static_cast<double>(passive) / static_cast<double>(report.filled);
    report.aggressive_ratio = 1.0 - report.passive_ratio;
    report.spread_cost_ticks = spread_cost / static_cast<double>(report.filled);
    report.net_execution_cost_ticks =
        *report.spread_cost_ticks + static_cast<double>(report.fees) / static_cast<double>(report.filled);
  }

  for (auto& fill : report.fills) {
    if (!fill.pre_fill_mid) continue;
    for (const Timestamp horizon : config.adverse_selection_horizons_ns) {
      const Timestamp target = saturating_add(fill.timestamp, horizon);
      const auto future = std::lower_bound(
          midpoint_series.begin(), midpoint_series.end(), target,
          [](const std::pair<Timestamp, double>& point, const Timestamp value) { return point.first < value; });
      if (future == midpoint_series.end()) continue;
      const double adverse =
          parent.side == Side::Buy ? future->second - *fill.pre_fill_mid : *fill.pre_fill_mid - future->second;
      fill.adverse_selection_ticks[horizon] = adverse;
    }
  }
  if (!config.adverse_selection_horizons_ns.empty() && report.filled > 0) {
    double weighted_impact = 0.0;
    Quantity impact_quantity = 0;
    const Timestamp first_horizon = config.adverse_selection_horizons_ns.front();
    for (const auto& fill : report.fills) {
      const auto found = fill.adverse_selection_ticks.find(first_horizon);
      if (found == fill.adverse_selection_ticks.end()) continue;
      weighted_impact += found->second * static_cast<double>(fill.quantity);
      impact_quantity += fill.quantity;
    }
    if (impact_quantity > 0) report.impact_proxy_ticks = weighted_impact / static_cast<double>(impact_quantity);
  }

  double market_notional = 0.0;
  Quantity market_volume = 0;
  for (const auto& [match, trade] : printable_trades) {
    static_cast<void>(match);
    market_notional += static_cast<double>(trade.first) * static_cast<double>(trade.second);
    market_volume += trade.second;
  }
  if (market_volume > 0) report.market_vwap = market_notional / static_cast<double>(market_volume);
  if (report.implementation_shortfall_ticks) {
    report.gross_execution_cost_ticks = *report.implementation_shortfall_ticks * static_cast<double>(report.filled) +
                                        static_cast<double>(report.opportunity_cost_ticks);
    report.implementation_shortfall_currency = *report.gross_execution_cost_ticks + static_cast<double>(report.fees);
  }
  if (const auto error = report.parent_state.invariant_error(); !error.empty())
    throw std::runtime_error("terminal parent-order conservation failure: " + error);
  return report;
}

void ExecutionSimulator::write(const std::vector<ExecutionReport>& reports, const std::filesystem::path& out) const {
  std::filesystem::create_directories(out);
  std::ofstream csv(out / "execution_comparison.csv");
  if (!csv) throw std::runtime_error("could not open execution output");
  csv << "strategy,parent_order_id,stock_locate,symbol,side,filled_quantity,unfilled_quantity,fill_rate,"
         "average_execution_price_ticks,"
         "implementation_shortfall_ticks,implementation_shortfall_bps,implementation_shortfall_currency_ticks,"
         "market_vwap_ticks,spread_cost_ticks,impact_proxy_ticks,gross_execution_cost_ticks,"
         "net_execution_cost_ticks,fees_ticks,fees_paid_ticks,rebates_ticks,"
         "opportunity_cost_ticks,passive_fill_ratio,aggressive_fill_ratio,child_order_count,cancel_count,"
         "rejected_child_count,depth_consumed,maximum_schedule_deviation,completion_time_ns\n";
  for (const auto& report : reports) {
    csv << to_string(report.strategy) << ',' << report.parent_order_id << ',' << report.stock_locate << ','
        << report.symbol << ',' << to_string(report.side) << ',' << report.filled << ',' << report.unfilled << ','
        << report.fill_rate << ',' << (report.average_price ? std::to_string(*report.average_price) : "") << ','
        << (report.implementation_shortfall_ticks ? std::to_string(*report.implementation_shortfall_ticks) : "") << ','
        << (report.implementation_shortfall_bps ? std::to_string(*report.implementation_shortfall_bps) : "") << ','
        << (report.implementation_shortfall_currency ? std::to_string(*report.implementation_shortfall_currency) : "")
        << ',' << (report.market_vwap ? std::to_string(*report.market_vwap) : "") << ','
        << (report.spread_cost_ticks ? std::to_string(*report.spread_cost_ticks) : "") << ','
        << (report.impact_proxy_ticks ? std::to_string(*report.impact_proxy_ticks) : "") << ','
        << (report.gross_execution_cost_ticks ? std::to_string(*report.gross_execution_cost_ticks) : "") << ','
        << (report.net_execution_cost_ticks ? std::to_string(*report.net_execution_cost_ticks) : "") << ','
        << report.fees << ',' << report.fees_paid << ',' << report.rebates << ',' << report.opportunity_cost_ticks
        << ',' << report.passive_ratio << ',' << report.aggressive_ratio << ',' << report.child_order_count << ','
        << report.cancel_count << ',' << report.rejected_child_count << ',' << report.depth_consumed << ','
        << report.maximum_schedule_deviation << ','
        << (report.completion_time_ns ? std::to_string(*report.completion_time_ns) : "") << '\n';
  }

  std::ofstream children(out / "child_orders.csv");
  if (!children) throw std::runtime_error("could not open child-order output");
  children << "strategy,stock_locate,symbol,child_id,parent_id,type,side,limit_price,submitted,remaining,state,"
              "decision_time,submission_time,exchange_arrival_time,acknowledgment_time,cancellation_time,"
              "initial_queue_ahead,final_queue_ahead\n";
  for (const auto& report : reports) {
    for (const auto& child : report.children) {
      children << to_string(report.strategy) << ',' << child.stock_locate << ',' << child.symbol << ',' << child.id
               << ',' << child.parent_id << ',' << (child.type == OrderType::Market ? "market" : "limit") << ','
               << to_string(child.side) << ',' << child.limit_price << ',' << child.submitted_quantity << ','
               << child.remaining_quantity << ',' << static_cast<int>(child.state) << ',' << child.decision_time << ','
               << child.submission_time << ',' << child.exchange_arrival_time << ',' << child.acknowledgment_time << ','
               << (child.cancellation_time ? std::to_string(*child.cancellation_time) : "") << ','
               << child.initial_queue_ahead << ',' << child.queue_ahead << '\n';
    }
  }

  std::ofstream fills(out / "execution_fills.csv");
  if (!fills) throw std::runtime_error("could not open fill output");
  fills << "strategy,stock_locate,symbol,parent_id,child_id,timestamp_ns,price_ticks,quantity,liquidity_role,fee_ticks,"
           "pre_fill_mid_ticks\n";
  for (const auto& report : reports) {
    for (const auto& fill : report.fills) {
      fills << to_string(report.strategy) << ',' << report.stock_locate << ',' << report.symbol << ','
            << fill.parent_order_id << ',' << fill.child_order_id << ',' << fill.timestamp << ',' << fill.price << ','
            << fill.quantity << ',' << to_string(fill.liquidity_role) << ',' << fill.fee << ','
            << (fill.pre_fill_mid ? std::to_string(*fill.pre_fill_mid) : "") << '\n';
    }
  }

  std::ofstream decisions(out / "execution_decisions.csv");
  if (!decisions) throw std::runtime_error("could not open decision output");
  decisions << "strategy,timestamp_ns,action,scheduled_target,filled,open,requested,bid,ask,imbalance,reason\n";
  for (const auto& report : reports) {
    for (const auto& decision : report.decisions) {
      decisions << to_string(report.strategy) << ',' << decision.timestamp << ',' << to_string(decision.action) << ','
                << decision.scheduled_target << ',' << decision.filled << ',' << decision.open << ','
                << decision.requested_quantity << ',' << (decision.bid ? std::to_string(*decision.bid) : "") << ','
                << (decision.ask ? std::to_string(*decision.ask) : "") << ','
                << (decision.imbalance ? std::to_string(*decision.imbalance) : "") << ',' << decision.reason << '\n';
    }
  }
}

}  // namespace aegisx
