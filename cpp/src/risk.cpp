#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "aegisx/aegisx.h"

namespace aegisx {
namespace {

std::int64_t checked_add(const std::int64_t lhs, const std::int64_t rhs, const char* context) {
  if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
      (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
    throw std::runtime_error(context);
  }
  return lhs + rhs;
}

std::int64_t checked_subtract(const std::int64_t lhs, const std::int64_t rhs, const char* context) {
  if ((rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs) ||
      (rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs)) {
    throw std::runtime_error(context);
  }
  return lhs - rhs;
}

std::int64_t checked_multiply(const std::int64_t lhs, const std::int64_t rhs, const char* context) {
  if (lhs == 0 || rhs == 0) return 0;
  if ((lhs == -1 && rhs == std::numeric_limits<std::int64_t>::min()) ||
      (rhs == -1 && lhs == std::numeric_limits<std::int64_t>::min())) {
    throw std::runtime_error(context);
  }
  if ((lhs > 0 && rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() / rhs) ||
      (lhs > 0 && rhs < 0 && rhs < std::numeric_limits<std::int64_t>::min() / lhs) ||
      (lhs < 0 && rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() / rhs) ||
      (lhs < 0 && rhs < 0 && lhs < std::numeric_limits<std::int64_t>::max() / rhs)) {
    throw std::runtime_error(context);
  }
  return lhs * rhs;
}

Quantity absolute_quantity(const Quantity value) {
  if (value == std::numeric_limits<Quantity>::min()) throw std::runtime_error("quantity absolute-value overflow");
  return value < 0 ? -value : value;
}

Money notional(const Price price, const Quantity quantity) {
  if (price < 0 || quantity < 0) throw std::runtime_error("negative notional input");
  return checked_multiply(price, quantity, "notional overflow");
}

Quantity checked_quantity_add(const Quantity lhs, const Quantity rhs) {
  if ((rhs > 0 && lhs > std::numeric_limits<Quantity>::max() - rhs) ||
      (rhs < 0 && lhs < std::numeric_limits<Quantity>::min() - rhs)) {
    throw std::runtime_error("position quantity overflow");
  }
  return lhs + rhs;
}

RiskDecision reject(const RiskRejectReason reason, const std::string_view name, const std::int64_t observed,
                    const std::int64_t limit) {
  return {false, reason, std::string(name), observed, limit};
}

}  // namespace

RiskEngine::RiskEngine(const RiskLimits limits) : limits_(limits) {}

void RiskEngine::mark(const StockLocate stock_locate, std::string symbol, const Price price,
                      const Timestamp timestamp) {
  if (price < 0 || symbol.empty()) throw std::runtime_error("invalid market mark");
  marks_[stock_locate] = {std::move(symbol), price, timestamp};
  auto& position = positions_[stock_locate];
  position.unrealized_pnl = checked_multiply(
      position.net_quantity, checked_subtract(price, position.average_open_price, "unrealized P&L overflow"),
      "unrealized P&L overflow");
  position.total_pnl = checked_add(position.realized_pnl, position.unrealized_pnl, "position total P&L overflow");
  high_water_pnl_ = std::max(high_water_pnl_, total_pnl());
}

RiskDecision RiskEngine::approve(const RiskRequest& request) {
  const auto audited = [&](const RiskDecision decision) {
    if (limits_.enable_audit_log)
      audit_log_.push_back(
          {request.timestamp, request.id, "approve", decision.approved, decision.reason, request.quantity,
           request.price >= 0 && request.quantity > 0 ? notional(request.price, request.quantity) : 0});
    return decision;
  };
  if (request.id.empty() || request.symbol.empty() || request.quantity <= 0 || request.price < 0) {
    return audited(reject(RiskRejectReason::InvalidOrder, "order_fields", 0, 0));
  }
  if (!limits_.enabled) return audited(reject(RiskRejectReason::TradingDisabled, "trading_enabled", 0, 1));
  if (limits_.kill_switch) return audited(reject(RiskRejectReason::KillSwitchActive, "kill_switch", 1, 0));
  if (reservations_.contains(request.id)) return audited(reject(RiskRejectReason::InvalidOrder, "request_id", 1, 0));
  if (request.quantity > limits_.max_order_quantity) {
    return audited(reject(RiskRejectReason::OrderQuantityLimit, "max_order_quantity", request.quantity,
                          limits_.max_order_quantity));
  }
  const Money request_notional = notional(request.price, request.quantity);
  if (request_notional > limits_.max_order_notional_ticks) {
    return audited(reject(RiskRejectReason::OrderNotionalLimit, "max_order_notional_ticks", request_notional,
                          limits_.max_order_notional_ticks));
  }
  const auto mark = marks_.find(request.stock_locate);
  if (mark == marks_.end() || mark->second.symbol != request.symbol || request.timestamp < mark->second.timestamp ||
      request.timestamp - mark->second.timestamp > limits_.stale_ns) {
    return audited(
        reject(RiskRejectReason::StaleMarketData, "stale_ns", 0, static_cast<std::int64_t>(limits_.stale_ns)));
  }
  const double permitted_distance = static_cast<double>(mark->second.price) * limits_.collar_bps / 10'000.0;
  const double distance = std::abs(static_cast<double>(request.price) - static_cast<double>(mark->second.price));
  if (distance > permitted_distance) {
    return audited(reject(RiskRejectReason::PriceCollarViolation, "collar_bps", static_cast<std::int64_t>(distance),
                          static_cast<std::int64_t>(permitted_distance)));
  }
  if (!approval_timestamps_.empty() && request.timestamp < approval_timestamps_.back())
    return audited(reject(RiskRejectReason::InvalidOrder, "monotonic_timestamp",
                          static_cast<std::int64_t>(request.timestamp),
                          static_cast<std::int64_t>(approval_timestamps_.back())));
  while (!approval_timestamps_.empty() &&
         request.timestamp - approval_timestamps_.front() >= limits_.order_rate_window_ns)
    approval_timestamps_.pop_front();
  if (approval_timestamps_.size() >= limits_.max_orders_per_window) {
    return audited(reject(RiskRejectReason::OrderRateLimit, "max_orders_per_window",
                          static_cast<std::int64_t>(approval_timestamps_.size()), limits_.max_orders_per_window));
  }
  if (reservations_.size() >= limits_.max_open_orders) {
    return audited(reject(RiskRejectReason::OpenOrderExposureLimit, "max_open_orders",
                          static_cast<std::int64_t>(reservations_.size()),
                          static_cast<std::int64_t>(limits_.max_open_orders)));
  }
  Quantity pending_quantity = 0;
  for (const auto& [id, reservation] : reservations_) {
    static_cast<void>(id);
    if (reservation.stock_locate == request.stock_locate) {
      pending_quantity = checked_quantity_add(
          pending_quantity, reservation.side == Side::Buy ? reservation.remaining : -reservation.remaining);
    }
  }
  const Quantity signed_request = request.side == Side::Buy ? request.quantity : -request.quantity;
  const Quantity projected_quantity = checked_quantity_add(
      checked_quantity_add(position(request.stock_locate) ? position(request.stock_locate)->net_quantity : 0,
                           pending_quantity),
      signed_request);
  if (absolute_quantity(projected_quantity) > limits_.max_position) {
    return audited(reject(RiskRejectReason::PositionLimit, "max_position", absolute_quantity(projected_quantity),
                          limits_.max_position));
  }
  const Money projected_symbol_exposure = notional(mark->second.price, absolute_quantity(projected_quantity));
  if (projected_symbol_exposure > limits_.max_symbol_exposure_ticks) {
    return audited(reject(RiskRejectReason::ConcentrationLimit, "max_symbol_exposure_ticks", projected_symbol_exposure,
                          limits_.max_symbol_exposure_ticks));
  }
  const Money projected_reserved = checked_add(reserved_exposure(), request_notional, "reserved exposure overflow");
  if (projected_reserved > limits_.max_open_order_exposure_ticks) {
    return audited(reject(RiskRejectReason::OpenOrderExposureLimit, "max_open_order_exposure_ticks", projected_reserved,
                          limits_.max_open_order_exposure_ticks));
  }
  const Money projected_gross =
      checked_add(checked_add(gross_exposure(), reserved_exposure(), "gross exposure overflow"), request_notional,
                  "gross exposure overflow");
  if (projected_gross > limits_.max_gross_exposure_ticks) {
    return audited(reject(RiskRejectReason::GrossExposureLimit, "max_gross_exposure_ticks", projected_gross,
                          limits_.max_gross_exposure_ticks));
  }
  const Money projected_net =
      checked_add(net_exposure(), checked_multiply(signed_request, request.price, "net exposure overflow"),
                  "net exposure overflow");
  if (absolute_quantity(projected_net) > limits_.max_net_exposure_ticks) {
    return audited(reject(RiskRejectReason::NetExposureLimit, "max_net_exposure_ticks",
                          absolute_quantity(projected_net), limits_.max_net_exposure_ticks));
  }
  if (projected_gross > 0 && static_cast<double>(projected_symbol_exposure) / static_cast<double>(projected_gross) >
                                 limits_.max_concentration_fraction) {
    return audited(reject(RiskRejectReason::ConcentrationLimit, "max_concentration_fraction_bps",
                          static_cast<std::int64_t>(std::llround(static_cast<double>(projected_symbol_exposure) /
                                                                 static_cast<double>(projected_gross) * 10'000.0)),
                          static_cast<std::int64_t>(std::llround(limits_.max_concentration_fraction * 10'000.0))));
  }
  if (-total_pnl() > limits_.max_loss_ticks) {
    return audited(reject(RiskRejectReason::DailyLossLimit, "max_loss_ticks", -total_pnl(), limits_.max_loss_ticks));
  }
  if (drawdown() > limits_.max_drawdown_ticks) {
    return audited(
        reject(RiskRejectReason::DrawdownLimit, "max_drawdown_ticks", drawdown(), limits_.max_drawdown_ticks));
  }
  reservations_.emplace(
      request.id, Reservation{request.stock_locate, request.side, request.quantity, request.price, request.timestamp});
  approval_timestamps_.push_back(request.timestamp);
  return audited({true, RiskRejectReason::None, "", 0, 0});
}

void RiskEngine::release(const std::string_view request_id, const Timestamp timestamp) {
  const auto reservation = reservations_.find(std::string(request_id));
  if (reservation == reservations_.end()) return;
  if (limits_.enable_audit_log)
    audit_log_.push_back({timestamp == 0 ? reservation->second.approval_timestamp : timestamp, std::string(request_id),
                          "release", true, RiskRejectReason::None, reservation->second.remaining,
                          notional(reservation->second.price, reservation->second.remaining)});
  reservations_.erase(reservation);
}

void RiskEngine::fill(const std::string_view request_id, const Quantity quantity, const Price price,
                      const Money fee_ticks, const Timestamp timestamp) {
  const auto reservation = reservations_.find(std::string(request_id));
  if (reservation == reservations_.end()) throw std::runtime_error("unknown risk reservation");
  if (quantity <= 0 || quantity > reservation->second.remaining)
    throw std::runtime_error("invalid reserved fill quantity");
  const auto mark = marks_.find(reservation->second.stock_locate);
  if (mark == marks_.end()) throw std::runtime_error("missing mark for reserved fill");
  fill(reservation->second.stock_locate, mark->second.symbol, reservation->second.side, quantity, price, fee_ticks);
  if (limits_.enable_audit_log)
    audit_log_.push_back({timestamp == 0 ? mark->second.timestamp : timestamp, std::string(request_id), "fill", true,
                          RiskRejectReason::None, quantity, notional(price, quantity)});
  reservation->second.remaining -= quantity;
  if (reservation->second.remaining == 0) reservations_.erase(reservation);
}

void RiskEngine::fill(const StockLocate stock_locate, const std::string_view symbol, const Side side,
                      const Quantity quantity, const Price price, const Money fee_ticks) {
  if (quantity <= 0 || price < 0 || symbol.empty()) throw std::runtime_error("invalid fill");
  auto& state = positions_[stock_locate];
  const Quantity delta = side == Side::Buy ? quantity : -quantity;
  if (state.net_quantity == 0 || (state.net_quantity > 0) == (delta > 0)) {
    const Quantity old_quantity = absolute_quantity(state.net_quantity);
    const Quantity added_quantity = absolute_quantity(delta);
    const Quantity total_quantity = checked_quantity_add(old_quantity, added_quantity);
    const Money weighted_total = checked_add(
        checked_multiply(state.average_open_price, old_quantity, "average open price overflow"),
        checked_multiply(price, added_quantity, "average open price overflow"), "average open price overflow");
    state.average_open_price = weighted_total / total_quantity;
  } else {
    const Quantity closed_quantity = std::min(absolute_quantity(state.net_quantity), absolute_quantity(delta));
    const Money difference = state.net_quantity > 0
                                 ? checked_subtract(price, state.average_open_price, "realized P&L overflow")
                                 : checked_subtract(state.average_open_price, price, "realized P&L overflow");
    state.realized_pnl =
        checked_add(state.realized_pnl, checked_multiply(difference, closed_quantity, "realized P&L overflow"),
                    "realized P&L overflow");
  }
  const Quantity prior_quantity = state.net_quantity;
  state.net_quantity = checked_quantity_add(state.net_quantity, delta);
  if (state.net_quantity == 0)
    state.average_open_price = 0;
  else if (prior_quantity != 0 && (state.net_quantity > 0) != (prior_quantity > 0))
    state.average_open_price = price;
  state.fees = checked_add(state.fees, fee_ticks, "fee overflow");
  state.realized_pnl = checked_subtract(state.realized_pnl, fee_ticks, "realized P&L overflow");
  const auto mark = marks_.find(stock_locate);
  if (mark != marks_.end()) {
    if (mark->second.symbol != symbol) throw std::runtime_error("fill symbol conflicts with market mark");
    state.unrealized_pnl = checked_multiply(
        state.net_quantity, checked_subtract(mark->second.price, state.average_open_price, "unrealized P&L overflow"),
        "unrealized P&L overflow");
  }
  state.total_pnl = checked_add(state.realized_pnl, state.unrealized_pnl, "position total P&L overflow");
  high_water_pnl_ = std::max(high_water_pnl_, total_pnl());
}

void RiskEngine::kill(const bool active, const Timestamp timestamp) {
  limits_.kill_switch = active;
  if (limits_.enable_audit_log)
    audit_log_.push_back({timestamp, "", active ? "kill_activate" : "kill_release", true, RiskRejectReason::None, 0,
                          reserved_exposure()});
}

const PositionState* RiskEngine::position(const StockLocate stock_locate) const {
  const auto found = positions_.find(stock_locate);
  return found == positions_.end() ? nullptr : &found->second;
}

Money RiskEngine::total_realized_pnl() const {
  Money total = 0;
  for (const auto& [stock_locate, state] : positions_) {
    static_cast<void>(stock_locate);
    total = checked_add(total, state.realized_pnl, "total realized P&L overflow");
  }
  return total;
}

Money RiskEngine::total_unrealized_pnl() const {
  Money total = 0;
  for (const auto& [stock_locate, state] : positions_) {
    static_cast<void>(stock_locate);
    total = checked_add(total, state.unrealized_pnl, "total unrealized P&L overflow");
  }
  return total;
}

Money RiskEngine::total_pnl() const {
  return checked_add(total_realized_pnl(), total_unrealized_pnl(), "total P&L overflow");
}

Money RiskEngine::drawdown() const { return std::max<Money>(0, high_water_pnl_ - total_pnl()); }

Money RiskEngine::gross_exposure() const {
  Money total = 0;
  for (const auto& [stock_locate, state] : positions_) {
    const auto mark = marks_.find(stock_locate);
    if (mark != marks_.end())
      total = checked_add(total, notional(mark->second.price, absolute_quantity(state.net_quantity)),
                          "gross exposure overflow");
  }
  return total;
}

Money RiskEngine::net_exposure() const {
  Money total = 0;
  for (const auto& [stock_locate, state] : positions_) {
    const auto mark = marks_.find(stock_locate);
    if (mark != marks_.end())
      total = checked_add(total, checked_multiply(state.net_quantity, mark->second.price, "net exposure overflow"),
                          "net exposure overflow");
  }
  for (const auto& [id, reservation] : reservations_) {
    static_cast<void>(id);
    const Money signed_notional = checked_multiply(
        reservation.price, reservation.side == Side::Buy ? reservation.remaining : -reservation.remaining,
        "reserved net exposure overflow");
    total = checked_add(total, signed_notional, "net exposure overflow");
  }
  return total;
}

Money RiskEngine::reserved_exposure() const {
  Money total = 0;
  for (const auto& [id, reservation] : reservations_) {
    static_cast<void>(id);
    total = checked_add(total, notional(reservation.price, reservation.remaining), "reserved exposure overflow");
  }
  return total;
}

std::size_t RiskEngine::open_order_count() const { return reservations_.size(); }

const std::vector<RiskAuditRecord>& RiskEngine::audit_log() const { return audit_log_; }

const RiskLimits& RiskEngine::limits() const { return limits_; }

}  // namespace aegisx
