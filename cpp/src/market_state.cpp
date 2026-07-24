#include <stdexcept>
#include <type_traits>

#include "aegisx/aegisx.h"

namespace aegisx {
namespace {

std::uint64_t hash_value(std::uint64_t hash, const std::uint64_t value) {
  for (int byte_index = 0; byte_index < 8; ++byte_index) {
    hash ^= (value >> (byte_index * 8)) & 0xffU;
    hash *= 1099511628211ULL;
  }
  return hash;
}

void validate_or_set_symbol(std::string& existing_symbol, const std::string& symbol) {
  if (symbol.empty()) throw std::runtime_error("empty instrument symbol");
  if (!existing_symbol.empty() && existing_symbol != symbol) {
    throw std::runtime_error("stock locate symbol conflicts with existing directory mapping");
  }
  existing_symbol = symbol;
}

}  // namespace

void MarketState::apply(const Event& event) {
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, StockDirectory>) {
          validate_or_set_symbol(instruments_[event.stock_locate].symbol, value.symbol);
        } else if constexpr (std::is_same_v<T, StockTradingAction>) {
          auto& instrument = instruments_[event.stock_locate];
          validate_or_set_symbol(instrument.symbol, value.symbol);
          instrument.tradable = value.state == 'T';
        } else if constexpr (std::is_same_v<T, Add>) {
          if (order_to_instrument_.contains(value.id))
            throw std::runtime_error("duplicate active order ID across instruments");
          auto& instrument = instruments_[event.stock_locate];
          validate_or_set_symbol(instrument.symbol, value.symbol);
          instrument.book.add(value.id, value.side, value.quantity, value.price);
          try {
            const auto [route, inserted] = order_to_instrument_.emplace(value.id, event.stock_locate);
            static_cast<void>(route);
            if (!inserted) throw std::runtime_error("duplicate active order ID across instruments");
          } catch (...) {
            instrument.book.erase(value.id);
            throw;
          }
        } else if constexpr (std::is_same_v<T, Trade>) {
          validate_or_set_symbol(instruments_[event.stock_locate].symbol, value.symbol);
        } else if constexpr (std::is_same_v<T, CrossTrade>) {
          validate_or_set_symbol(instruments_[event.stock_locate].symbol, value.symbol);
        } else if constexpr (std::is_same_v<T, Execute> || std::is_same_v<T, ExecuteWithPrice> ||
                             std::is_same_v<T, Cancel> || std::is_same_v<T, Delete> || std::is_same_v<T, Replace>) {
          const OrderId id = [&]() {
            if constexpr (std::is_same_v<T, Delete>)
              return value.id;
            else if constexpr (std::is_same_v<T, Replace>)
              return value.old_id;
            else
              return value.id;
          }();
          const auto route = order_to_instrument_.find(id);
          if (route == order_to_instrument_.end()) throw std::runtime_error("unknown order routing reference");
          if (route->second != event.stock_locate) throw std::runtime_error("stock locate does not match routed order");
          auto& instrument = instruments_.at(route->second);
          if constexpr (std::is_same_v<T, Execute> || std::is_same_v<T, ExecuteWithPrice>) {
            instrument.book.execute(value.id, value.quantity);
            if (!instrument.book.order(value.id)) order_to_instrument_.erase(route);
          } else if constexpr (std::is_same_v<T, Cancel>) {
            instrument.book.cancel(value.id, value.quantity);
            if (!instrument.book.order(value.id)) order_to_instrument_.erase(route);
          } else if constexpr (std::is_same_v<T, Delete>) {
            instrument.book.erase(value.id);
            order_to_instrument_.erase(route);
          } else {
            if (order_to_instrument_.contains(value.new_id)) {
              throw std::runtime_error("duplicate replacement order ID across instruments");
            }
            // Reserve before mutating the book. Node-handle key replacement below
            // then performs the global route update without a second allocation.
            order_to_instrument_.reserve(order_to_instrument_.size() + 1);
            instrument.book.replace(value.old_id, value.new_id, value.quantity, value.price);
            auto node = order_to_instrument_.extract(route);
            node.key() = value.new_id;
            const auto inserted = order_to_instrument_.insert(std::move(node));
            if (!inserted.inserted) throw std::runtime_error("replacement route insertion invariant failed");
          }
        }
      },
      event.payload);
}

const OrderBook* MarketState::book_for(const StockLocate stock_locate) const {
  const auto found = instruments_.find(stock_locate);
  return found == instruments_.end() ? nullptr : &found->second.book;
}

const OrderBook* MarketState::book_for_symbol(const std::string_view symbol) const {
  for (const auto& [stock_locate, instrument] : instruments_) {
    static_cast<void>(stock_locate);
    if (instrument.symbol == symbol) return &instrument.book;
  }
  return nullptr;
}

std::optional<std::string> MarketState::symbol_for(const StockLocate stock_locate) const {
  const auto found = instruments_.find(stock_locate);
  if (found == instruments_.end() || found->second.symbol.empty()) return {};
  return found->second.symbol;
}

std::optional<StockLocate> MarketState::route_for(const OrderId id) const {
  const auto found = order_to_instrument_.find(id);
  return found == order_to_instrument_.end() ? std::optional<StockLocate>{} : std::optional<StockLocate>{found->second};
}

bool MarketState::is_tradable(const StockLocate stock_locate) const {
  const auto found = instruments_.find(stock_locate);
  return found != instruments_.end() && found->second.tradable;
}

std::size_t MarketState::instrument_count() const { return instruments_.size(); }
std::size_t MarketState::active_order_count() const { return order_to_instrument_.size(); }

std::uint64_t MarketState::full_state_checksum() const {
  std::uint64_t hash = 1469598103934665603ULL;
  std::map<StockLocate, const Instrument*> sorted;
  for (const auto& [stock_locate, instrument] : instruments_) sorted.emplace(stock_locate, &instrument);
  for (const auto& [stock_locate, instrument] : sorted) {
    hash = hash_value(hash, stock_locate);
    for (const char character : instrument->symbol) hash = hash_value(hash, static_cast<unsigned char>(character));
    hash = hash_value(hash, instrument->tradable ? 1U : 0U);
    hash = hash_value(hash, instrument->book.full_order_state_checksum());
    hash = hash_value(hash, instrument->book.fifo_state_checksum());
  }
  return hash;
}

std::string MarketState::invariant_error() const {
  std::size_t book_order_count = 0;
  for (const auto& [stock_locate, instrument] : instruments_) {
    if (const auto error = instrument.book.invariant_error(); !error.empty()) return error;
    book_order_count += instrument.book.active_orders();
    static_cast<void>(stock_locate);
  }
  if (book_order_count != order_to_instrument_.size()) return "global route count differs from book order count";
  for (const auto& [id, stock_locate] : order_to_instrument_) {
    const auto instrument = instruments_.find(stock_locate);
    if (instrument == instruments_.end() || !instrument->second.book.order(id)) {
      return "global route does not resolve to an active instrument order";
    }
  }
  return {};
}

}  // namespace aegisx
