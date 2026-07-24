#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>

#include "aegisx/aegisx.h"

namespace aegisx {
namespace {

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
  const Quantity completed_quantity = static_cast<Quantity>(completed);
  const Quantity interval_quantity = static_cast<Quantity>(intervals);
  if (completed_quantity > 0 && target > std::numeric_limits<Quantity>::max() / completed_quantity) {
    throw std::runtime_error("schedule quantity overflow");
  }
  return ceiling_divide(target * completed_quantity, interval_quantity);
}

}  // namespace

ExecutionReport ExecutionSimulator::run(const std::vector<Event>& events, const ParentOrder& parent,
                                        const Strategy strategy, const ExecutionConfig& config) const {
  if (parent.id == 0 || parent.stock_locate == 0 || parent.symbol.empty() || parent.target_quantity <= 0 ||
      parent.end_time <= parent.arrival_time || config.intervals == 0 || config.pov_rate < 0.0 ||
      config.cancellation_queue_fraction < 0.0 || config.cancellation_queue_fraction > 1.0) {
    throw std::runtime_error("invalid execution configuration");
  }

  MarketState historical;
  ExecutionReport report;
  report.strategy = strategy;
  report.stock_locate = parent.stock_locate;
  report.symbol = parent.symbol;
  report.unfilled = parent.target_quantity;
  std::map<Price, Quantity> shadow_consumed;
  std::optional<double> arrival_mid;
  std::optional<double> end_mid;
  std::size_t next_child_id = 1;
  double gross_notional = 0.0;
  std::uint64_t observed_trade_volume = 0;

  const auto total_open = [&]() {
    Quantity open = 0;
    for (const auto& child : report.children) {
      if (child.state == ChildOrderState::Pending || child.state == ChildOrderState::Open)
        open += child.remaining_quantity;
    }
    return open;
  };
  const auto append_fill = [&](ChildOrder& child, const Timestamp timestamp, const Price price, const Quantity quantity,
                               const LiquidityRole role) {
    if (quantity <= 0 || quantity > child.remaining_quantity) throw std::runtime_error("invalid simulated fill");
    const Money fee_per_share = role == LiquidityRole::Maker ? config.maker_fee_per_share : config.taker_fee_per_share;
    const Money fee = checked_money_product(fee_per_share, quantity, "execution fee overflow");
    report.fills.push_back({child.id, timestamp, price, quantity, role, fee});
    child.remaining_quantity -= quantity;
    child.state = child.remaining_quantity == 0 ? ChildOrderState::Filled : ChildOrderState::Open;
    report.filled += quantity;
    report.unfilled -= quantity;
    report.fees += fee;
    report.depth_consumed += role == LiquidityRole::Taker ? quantity : 0;
    gross_notional += static_cast<double>(price) * static_cast<double>(quantity);
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
    for (auto& child : report.children) {
      if (child.state != ChildOrderState::Open || child.type != OrderType::Limit || child.side != resting->side ||
          child.limit_price != resting->price)
        continue;
      Quantity eligible_flow = flow;
      if (cancellation)
        eligible_flow =
            static_cast<Quantity>(std::floor(static_cast<double>(flow) * config.cancellation_queue_fraction));
      const Quantity ahead_reduction = std::min(child.queue_ahead, eligible_flow);
      child.queue_ahead -= ahead_reduction;
      eligible_flow -= ahead_reduction;
      if (!cancellation && child.queue_ahead == 0 && eligible_flow > 0) {
        append_fill(child, event.timestamp_ns, child.limit_price, std::min(child.remaining_quantity, eligible_flow),
                    LiquidityRole::Maker);
      }
    }
  };
  const auto activate_children = [&](const Timestamp timestamp) {
    const auto* book = historical.book_for(parent.stock_locate);
    if (book == nullptr) return;
    for (auto& child : report.children) {
      if (child.state != ChildOrderState::Pending || child.submission_time > timestamp) continue;
      if (child.type == OrderType::Limit) {
        child.queue_ahead = book->depth_at(child.side, child.limit_price);
        child.state = ChildOrderState::Open;
        continue;
      }
      child.state = ChildOrderState::Open;
      const Side opposite = child.side == Side::Buy ? Side::Sell : Side::Buy;
      for (const auto& level : book->top(opposite, 100)) {
        if (child.remaining_quantity == 0) break;
        const Quantity available = std::max<Quantity>(0, level.quantity - shadow_consumed[level.price]);
        const Quantity filled = std::min(child.remaining_quantity, available);
        if (filled == 0) continue;
        shadow_consumed[level.price] += filled;
        append_fill(child, timestamp, level.price, filled, LiquidityRole::Taker);
      }
    }
  };
  const auto submit_child = [&](const Timestamp timestamp, const OrderType type, const Quantity desired) {
    const auto* book = historical.book_for(parent.stock_locate);
    if (book == nullptr || desired <= 0) return;
    const Quantity available = parent.target_quantity - report.filled - total_open();
    const Quantity quantity = bounded_child_quantity(std::min(desired, available), config);
    if (quantity <= 0) return;
    const Side passive_side = parent.side;
    const auto passive_price = passive_side == Side::Buy ? book->best_bid() : book->best_ask();
    if (type == OrderType::Limit && !passive_price) return;
    report.children.push_back({static_cast<ChildOrderId>(next_child_id++), parent.id, parent.side, type,
                               passive_price.value_or(0), quantity, quantity,
                               timestamp + config.decision_latency_ns + config.transmission_latency_ns,
                               ChildOrderState::Pending, 0});
  };
  const auto scheduled_target = [&](const Timestamp timestamp) {
    const Timestamp elapsed = std::min(timestamp - parent.arrival_time, parent.end_time - parent.arrival_time);
    const Timestamp duration = parent.end_time - parent.arrival_time;
    const std::size_t completed =
        std::min<std::size_t>(config.intervals, static_cast<std::size_t>((elapsed * config.intervals) / duration) + 1U);
    if (strategy == Strategy::Vwap && !config.vwap_weights.empty()) {
      if (config.vwap_weights.size() != config.intervals)
        throw std::runtime_error("VWAP weights must match interval count");
      double total_weight = 0.0;
      for (const double weight : config.vwap_weights) {
        if (weight < 0.0) throw std::runtime_error("negative VWAP weight");
        total_weight += weight;
      }
      if (total_weight <= 0.0) throw std::runtime_error("zero VWAP weights");
      double cumulative = 0.0;
      for (std::size_t index = 0; index < completed; ++index) cumulative += config.vwap_weights[index] / total_weight;
      return static_cast<Quantity>(std::ceil(cumulative * static_cast<double>(parent.target_quantity)));
    }
    return scheduled_fraction(parent.target_quantity, completed, config.intervals);
  };

  for (const auto& event : events) {
    process_passive_flow(event);
    historical.apply(event);
    if (event.stock_locate != parent.stock_locate) continue;
    const auto symbol = historical.symbol_for(parent.stock_locate);
    if (symbol && *symbol != parent.symbol)
      throw std::runtime_error("parent symbol conflicts with stock locate directory");
    const auto* book = historical.book_for(parent.stock_locate);
    if (book == nullptr) continue;
    if (!arrival_mid && event.timestamp_ns >= parent.arrival_time) arrival_mid = book->mid_price();
    activate_children(event.timestamp_ns);
    if (event.timestamp_ns < parent.arrival_time || event.timestamp_ns > parent.end_time) continue;

    Quantity desired = 0;
    if (strategy == Strategy::Pov) {
      if (const auto* trade = std::get_if<Trade>(&event.payload); trade != nullptr && trade->symbol == parent.symbol) {
        observed_trade_volume += static_cast<std::uint64_t>(trade->quantity);
        desired = static_cast<Quantity>(std::floor(static_cast<double>(trade->quantity) * config.pov_rate));
      }
    } else {
      desired = std::max<Quantity>(0, scheduled_target(event.timestamp_ns) - report.filled - total_open());
    }
    if (desired > 0) {
      OrderType type = OrderType::Market;
      if (strategy == Strategy::Adaptive) {
        const Quantity expected = scheduled_target(event.timestamp_ns);
        const bool materially_behind = report.filled + total_open() + config.max_child_quantity < expected;
        const bool urgent = parent.end_time - event.timestamp_ns <= (parent.end_time - parent.arrival_time) / 4U;
        const auto bid = book->best_bid();
        const auto ask = book->best_ask();
        const bool wide_spread = bid && ask && *ask - *bid > 1;
        type = !materially_behind && !urgent && wide_spread ? OrderType::Limit : OrderType::Market;
      }
      submit_child(event.timestamp_ns, type, desired);
      activate_children(event.timestamp_ns);
    }
    end_mid = book->mid_price();
  }

  if (config.force_completion_at_end && report.filled < parent.target_quantity) {
    for (auto& child : report.children) {
      if (child.state == ChildOrderState::Pending || child.state == ChildOrderState::Open) {
        child.state = ChildOrderState::Cancelled;
        ++report.cancel_count;
      }
    }
    submit_child(parent.end_time, OrderType::Market, parent.target_quantity - report.filled);
    activate_children(parent.end_time);
  }
  const auto* final_book = historical.book_for(parent.stock_locate);
  if (final_book != nullptr) end_mid = final_book->mid_price();
  report.fill_rate = static_cast<double>(report.filled) / static_cast<double>(parent.target_quantity);
  report.child_order_count = report.children.size();
  if (report.filled > 0) report.average_price = gross_notional / static_cast<double>(report.filled);
  if (arrival_mid && report.average_price) {
    report.implementation_shortfall_ticks =
        parent.side == Side::Buy ? *report.average_price - *arrival_mid : *arrival_mid - *report.average_price;
  }
  if (end_mid && report.unfilled > 0 && arrival_mid) {
    const double missed_cost = parent.side == Side::Buy ? *end_mid - *arrival_mid : *arrival_mid - *end_mid;
    report.opportunity_cost_ticks =
        static_cast<Money>(std::llround(std::max(0.0, missed_cost) * static_cast<double>(report.unfilled)));
  }
  if (report.filled > 0) {
    Quantity passive = 0;
    for (const auto& fill : report.fills)
      if (fill.liquidity_role == LiquidityRole::Maker) passive += fill.quantity;
    report.passive_ratio = static_cast<double>(passive) / static_cast<double>(report.filled);
    report.aggressive_ratio = 1.0 - report.passive_ratio;
  }
  static_cast<void>(observed_trade_volume);
  return report;
}

void ExecutionSimulator::write(const std::vector<ExecutionReport>& reports, const std::filesystem::path& out) const {
  std::filesystem::create_directories(out);
  std::ofstream csv(out / "execution_comparison.csv");
  if (!csv) throw std::runtime_error("could not open execution output");
  csv << "strategy,filled_quantity,unfilled_quantity,fill_rate,average_execution_price_ticks,implementation_shortfall_"
         "ticks,"
         "fees_ticks,opportunity_cost_ticks,passive_fill_ratio,aggressive_fill_ratio,child_order_count,depth_"
         "consumed\n";
  for (const auto& report : reports) {
    csv << to_string(report.strategy) << ',' << report.filled << ',' << report.unfilled << ',' << report.fill_rate
        << ',' << (report.average_price ? std::to_string(*report.average_price) : "") << ','
        << (report.implementation_shortfall_ticks ? std::to_string(*report.implementation_shortfall_ticks) : "") << ','
        << report.fees << ',' << report.opportunity_cost_ticks << ',' << report.passive_ratio << ','
        << report.aggressive_ratio << ',' << report.child_order_count << ',' << report.depth_consumed << '\n';
  }
  std::ofstream children(out / "child_orders.csv");
  children << "strategy,stock_locate,symbol,child_id,parent_id,type,side,limit_price,submitted,remaining,state,"
              "submission_time,queue_ahead\n";
  for (const auto& report : reports) {
    for (const auto& child : report.children) {
      children << to_string(report.strategy) << ',' << report.stock_locate << ',' << report.symbol << ',' << child.id
               << ',' << child.parent_id << ',' << (child.type == OrderType::Market ? "market" : "limit") << ','
               << to_string(child.side) << ',' << child.limit_price << ',' << child.submitted_quantity << ','
               << child.remaining_quantity << ',' << static_cast<int>(child.state) << ',' << child.submission_time
               << ',' << child.queue_ahead << '\n';
    }
  }
  std::ofstream fills(out / "execution_fills.csv");
  fills << "strategy,stock_locate,symbol,child_id,timestamp_ns,price_ticks,quantity,liquidity_role,fee_ticks\n";
  for (const auto& report : reports) {
    for (const auto& fill : report.fills) {
      fills << to_string(report.strategy) << ',' << report.stock_locate << ',' << report.symbol << ','
            << fill.child_order_id << ',' << fill.timestamp << ',' << fill.price << ',' << fill.quantity << ','
            << to_string(fill.liquidity_role) << ',' << fill.fee << '\n';
    }
  }
}

}  // namespace aegisx
