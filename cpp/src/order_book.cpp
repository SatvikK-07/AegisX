#include <limits>
#include <stdexcept>
#include <type_traits>

#include "aegisx/aegisx.h"

namespace aegisx {
namespace {

Quantity checked_quantity_add(const Quantity lhs, const Quantity rhs) {
  if ((rhs > 0 && lhs > std::numeric_limits<Quantity>::max() - rhs) ||
      (rhs < 0 && lhs < std::numeric_limits<Quantity>::min() - rhs)) {
    throw std::runtime_error("quantity overflow");
  }
  return lhs + rhs;
}

std::uint64_t hash_value(std::uint64_t hash, const std::uint64_t value) {
  for (int byte_index = 0; byte_index < 8; ++byte_index) {
    hash ^= (value >> (byte_index * 8)) & 0xffU;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::uint64_t hash_side(const std::uint64_t hash, const Side side) {
  return hash_value(hash, side == Side::Buy ? 1U : 2U);
}

}  // namespace

void OrderBook::add(const OrderId id, const Side side, const Quantity quantity, const Price price) {
  if (quantity <= 0 || price < 0) throw std::runtime_error("invalid add fields");
  if (orders_.contains(id)) throw std::runtime_error("duplicate order ID in book");
  const auto append = [&](auto& levels) {
    auto [level, inserted] = levels.try_emplace(price);
    static_cast<void>(inserted);
    level->second.total = checked_quantity_add(level->second.total, quantity);
    level->second.fifo.push_back({id, quantity});
    try {
      orders_.emplace(id, Locator{side, price, std::prev(level->second.fifo.end())});
    } catch (...) {
      level->second.fifo.pop_back();
      level->second.total -= quantity;
      if (level->second.fifo.empty()) levels.erase(level);
      throw;
    }
  };
  if (side == Side::Buy)
    append(bids_);
  else
    append(asks_);
}

void OrderBook::reduce(const OrderId id, const Quantity quantity) {
  if (quantity <= 0) throw std::runtime_error("invalid reduction quantity");
  const auto locator = orders_.find(id);
  if (locator == orders_.end()) throw std::runtime_error("unknown order ID");
  if (locator->second.iterator->id != id) throw std::runtime_error("order locator identity invariant failed");
  if (quantity > locator->second.iterator->quantity) throw std::runtime_error("reduction exceeds resting quantity");
  const auto update = [&](auto& levels) {
    const auto level = levels.find(locator->second.price);
    if (level == levels.end()) throw std::runtime_error("missing price level for order locator");
    level->second.total -= quantity;
    if (quantity == locator->second.iterator->quantity) {
      level->second.fifo.erase(locator->second.iterator);
      orders_.erase(locator);
      if (level->second.fifo.empty()) levels.erase(level);
    } else {
      locator->second.iterator->quantity -= quantity;
    }
  };
  if (locator->second.side == Side::Buy)
    update(bids_);
  else
    update(asks_);
}

void OrderBook::cancel(const OrderId id, const Quantity quantity) { reduce(id, quantity); }
void OrderBook::execute(const OrderId id, const Quantity quantity) { reduce(id, quantity); }

void OrderBook::erase(const OrderId id) {
  const auto existing = order(id);
  if (!existing) throw std::runtime_error("unknown order ID");
  reduce(id, existing->quantity);
}

void OrderBook::replace(const OrderId old_id, const OrderId new_id, const Quantity quantity, const Price price) {
  const auto prior = order(old_id);
  if (!prior) throw std::runtime_error("unknown replacement order ID");
  if (new_id == old_id) throw std::runtime_error("replacement order ID must differ from original ID");
  if (orders_.contains(new_id)) throw std::runtime_error("duplicate replacement order ID");
  if (quantity <= 0 || price < 0) throw std::runtime_error("invalid replacement fields");

  // Add first. If allocation or locator insertion fails, the original order is
  // untouched. Erasing a known valid order cannot allocate or otherwise fail.
  add(new_id, prior->side, quantity, price);
  erase(old_id);
}

void OrderBook::apply(const Event& event) {
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Add>)
          add(value.id, value.side, value.quantity, value.price);
        else if constexpr (std::is_same_v<T, Execute> || std::is_same_v<T, ExecuteWithPrice>)
          execute(value.id, value.quantity);
        else if constexpr (std::is_same_v<T, Cancel>)
          cancel(value.id, value.quantity);
        else if constexpr (std::is_same_v<T, Delete>)
          erase(value.id);
        else if constexpr (std::is_same_v<T, Replace>)
          replace(value.old_id, value.new_id, value.quantity, value.price);
      },
      event.payload);
}

std::optional<Price> OrderBook::best_bid() const {
  return bids_.empty() ? std::optional<Price>{} : std::optional<Price>{bids_.begin()->first};
}

std::optional<Price> OrderBook::best_ask() const {
  return asks_.empty() ? std::optional<Price>{} : std::optional<Price>{asks_.begin()->first};
}

std::optional<double> OrderBook::mid_price() const {
  const auto bid = best_bid();
  const auto ask = best_ask();
  if (!bid || !ask) return {};
  return (static_cast<double>(*bid) + static_cast<double>(*ask)) / 2.0;
}

std::optional<double> OrderBook::microprice() const {
  const auto bid = best_bid();
  const auto ask = best_ask();
  if (!bid || !ask) return {};
  const Quantity bid_quantity = depth_at(Side::Buy, *bid);
  const Quantity ask_quantity = depth_at(Side::Sell, *ask);
  if (bid_quantity + ask_quantity <= 0) return {};
  return (static_cast<double>(*ask) * static_cast<double>(bid_quantity) +
          static_cast<double>(*bid) * static_cast<double>(ask_quantity)) /
         static_cast<double>(bid_quantity + ask_quantity);
}

Quantity OrderBook::depth_at(const Side side, const Price price) const {
  if (side == Side::Buy) {
    const auto level = bids_.find(price);
    return level == bids_.end() ? 0 : level->second.total;
  }
  const auto level = asks_.find(price);
  return level == asks_.end() ? 0 : level->second.total;
}

std::vector<OrderId> OrderBook::fifo_at(const Side side, const Price price) const {
  std::vector<OrderId> result;
  const auto append = [&](const auto& levels) {
    const auto level = levels.find(price);
    if (level == levels.end()) return;
    result.reserve(level->second.fifo.size());
    for (const auto& order : level->second.fifo) result.push_back(order.id);
  };
  if (side == Side::Buy)
    append(bids_);
  else
    append(asks_);
  return result;
}

std::vector<DepthLevel> OrderBook::top(const Side side, const std::size_t count) const {
  std::vector<DepthLevel> result;
  const auto append = [&](const auto& levels) {
    for (const auto& [price, level] : levels) {
      if (result.size() == count) break;
      result.push_back({price, level.total});
    }
  };
  if (side == Side::Buy)
    append(bids_);
  else
    append(asks_);
  return result;
}

std::optional<OrderView> OrderBook::order(const OrderId id) const {
  const auto found = orders_.find(id);
  if (found == orders_.end()) return {};
  const auto& locator = found->second;
  return OrderView{id, locator.side, locator.price, locator.iterator->quantity};
}

std::size_t OrderBook::active_orders() const { return orders_.size(); }

std::string OrderBook::invariant_error() const {
  std::size_t count = 0;
  const auto inspect = [&](const auto& levels, const Side expected_side) {
    for (const auto& [price, level] : levels) {
      if (level.fifo.empty() || level.total <= 0) return std::string("empty or non-positive price level");
      Quantity aggregate = 0;
      for (const auto& resting : level.fifo) {
        if (resting.quantity <= 0) return std::string("non-positive resting quantity");
        aggregate = checked_quantity_add(aggregate, resting.quantity);
        const auto locator = orders_.find(resting.id);
        if (locator == orders_.end() || locator->second.side != expected_side || locator->second.price != price ||
            locator->second.iterator->id != resting.id || locator->second.iterator->quantity != resting.quantity) {
          return std::string("locator does not point to exact resting order");
        }
        ++count;
      }
      if (aggregate != level.total) return std::string("bad level aggregate");
    }
    return std::string{};
  };
  if (const auto error = inspect(bids_, Side::Buy); !error.empty()) return error;
  if (const auto error = inspect(asks_, Side::Sell); !error.empty()) return error;
  return count == orders_.size() ? "" : "orphan order locator";
}

std::uint64_t OrderBook::top_of_book_checksum() const {
  std::uint64_t hash = 1469598103934665603ULL;
  hash = hash_value(hash, static_cast<std::uint64_t>(best_bid().value_or(-1)));
  hash = hash_value(hash, static_cast<std::uint64_t>(best_ask().value_or(-1)));
  return hash;
}

std::uint64_t OrderBook::aggregate_depth_checksum() const {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto append = [&](const auto& levels, const Side side) {
    for (const auto& [price, level] : levels) {
      hash = hash_side(hash, side);
      hash = hash_value(hash, static_cast<std::uint64_t>(price));
      hash = hash_value(hash, static_cast<std::uint64_t>(level.total));
    }
  };
  append(bids_, Side::Buy);
  append(asks_, Side::Sell);
  return hash;
}

std::uint64_t OrderBook::full_order_state_checksum() const {
  std::uint64_t hash = aggregate_depth_checksum();
  const auto append = [&](const auto& levels, const Side side) {
    for (const auto& [price, level] : levels) {
      for (const auto& resting : level.fifo) {
        hash = hash_side(hash, side);
        hash = hash_value(hash, static_cast<std::uint64_t>(price));
        hash = hash_value(hash, resting.id);
        hash = hash_value(hash, static_cast<std::uint64_t>(resting.quantity));
      }
    }
  };
  append(bids_, Side::Buy);
  append(asks_, Side::Sell);
  return hash;
}

std::uint64_t OrderBook::fifo_state_checksum() const {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto append = [&](const auto& levels, const Side side) {
    for (const auto& [price, level] : levels) {
      std::uint64_t position = 0;
      for (const auto& resting : level.fifo) {
        hash = hash_side(hash, side);
        hash = hash_value(hash, static_cast<std::uint64_t>(price));
        hash = hash_value(hash, position++);
        hash = hash_value(hash, resting.id);
        hash = hash_value(hash, static_cast<std::uint64_t>(resting.quantity));
      }
    }
  };
  append(bids_, Side::Buy);
  append(asks_, Side::Sell);
  return hash;
}

}  // namespace aegisx
