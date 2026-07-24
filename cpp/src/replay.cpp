#include <fstream>
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

void write_type_counts(std::ostream& stream, const std::map<char, std::uint64_t>& counts) {
  stream << '{';
  bool first = true;
  for (const auto& [type, count] : counts) {
    if (!first) stream << ',';
    stream << "\"" << type << "\":" << count;
    first = false;
  }
  stream << '}';
}

void write_symbols(std::ostream& stream, const std::vector<std::string>& symbols) {
  stream << '[';
  for (std::size_t index = 0; index < symbols.size(); ++index) {
    if (index > 0) stream << ',';
    stream << "\"" << symbols[index] << "\"";
  }
  stream << ']';
}

void write_optional_timestamp(std::ostream& stream, const std::optional<Timestamp> timestamp) {
  if (timestamp)
    stream << *timestamp;
  else
    stream << "null";
}

struct Metrics {
  std::uint64_t adds{};
  std::uint64_t cancels{};
  std::uint64_t executions{};
  std::uint64_t trade_count{};
  Quantity printable_trade_volume{};
};

class ReplayAccumulator {
 public:
  explicit ReplayAccumulator(ReplayConfig config) : config_(std::move(config)) {}

  void observe(const Event& event) {
    market_.apply(event);
    ++result_.processed_events;
    auto& metrics = metrics_[event.stock_locate];
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, Add>)
            ++metrics.adds;
          else if constexpr (std::is_same_v<T, Cancel>)
            ++metrics.cancels;
          else if constexpr (std::is_same_v<T, Execute> || std::is_same_v<T, ExecuteWithPrice>)
            ++metrics.executions;
          else if constexpr (std::is_same_v<T, Trade>) {
            ++metrics.trade_count;
            if (value.printable) metrics.printable_trade_volume += value.quantity;
          }
        },
        event.payload);

    if (!within_requested_range(event) || !matches_filter(event.stock_locate)) return;
    if (result_.processed_events <= config_.start_event) return;
    if (config_.end_event && result_.processed_events > *config_.end_event) return;
    if (!snapshot_due(event.stock_locate, event.timestamp_ns)) return;
    const auto* book = market_.book_for(event.stock_locate);
    if (book == nullptr) return;
    if (const auto error = book->invariant_error(); !error.empty())
      throw std::runtime_error("replay book invariant: " + error);
    const auto snapshot = make_snapshot(event.stock_locate, event.timestamp_ns, *book, metrics);
    hash_snapshot(snapshot);
    result_.snapshots.push_back(snapshot);
  }

  [[nodiscard]] ReplayResult finish(DecoderStatistics statistics = {}) {
    result_.decoder_statistics = std::move(statistics);
    result_.full_state_checksum = market_.full_state_checksum();
    result_.logical_checksum = logical_checksum_;
    if (const auto error = market_.invariant_error(); !error.empty())
      throw std::runtime_error("replay market invariant: " + error);
    return std::move(result_);
  }

 private:
  [[nodiscard]] bool matches_filter(const StockLocate stock_locate) const {
    if (config_.stock_locate && *config_.stock_locate != stock_locate) return false;
    if (config_.symbol) {
      const auto symbol = market_.symbol_for(stock_locate);
      return symbol && *symbol == *config_.symbol;
    }
    return true;
  }

  [[nodiscard]] bool within_requested_range(const Event& event) const {
    if (config_.start_timestamp && event.timestamp_ns < *config_.start_timestamp) return false;
    if (config_.end_timestamp && event.timestamp_ns > *config_.end_timestamp) return false;
    return true;
  }

  bool snapshot_due(const StockLocate stock_locate, const Timestamp timestamp) {
    if (config_.snapshot_every_events == 0) throw std::runtime_error("snapshot_every_events must be positive");
    const std::size_t index = snapshots_per_instrument_[stock_locate]++;
    if (index % config_.snapshot_every_events != 0) return false;
    if (config_.snapshot_every_ns) {
      const auto previous = last_snapshot_timestamp_.find(stock_locate);
      if (previous != last_snapshot_timestamp_.end() && timestamp - previous->second < *config_.snapshot_every_ns)
        return false;
      last_snapshot_timestamp_[stock_locate] = timestamp;
    }
    return true;
  }

  Snapshot make_snapshot(const StockLocate stock_locate, const Timestamp timestamp, const OrderBook& book,
                         const Metrics& metrics) const {
    Snapshot snapshot;
    snapshot.stock_locate = stock_locate;
    snapshot.symbol = market_.symbol_for(stock_locate).value_or("");
    snapshot.timestamp_ns = timestamp;
    snapshot.bid = book.best_bid();
    snapshot.ask = book.best_ask();
    snapshot.spread = snapshot.bid && snapshot.ask ? std::optional<Price>{*snapshot.ask - *snapshot.bid} : std::nullopt;
    snapshot.mid = book.mid_price();
    snapshot.microprice = book.microprice();
    snapshot.bid_depth = snapshot.bid ? book.depth_at(Side::Buy, *snapshot.bid) : 0;
    snapshot.ask_depth = snapshot.ask ? book.depth_at(Side::Sell, *snapshot.ask) : 0;
    if (snapshot.bid_depth + snapshot.ask_depth > 0) {
      snapshot.level_one_imbalance = static_cast<double>(snapshot.bid_depth - snapshot.ask_depth) /
                                     static_cast<double>(snapshot.bid_depth + snapshot.ask_depth);
    }
    Quantity total_bids = 0;
    Quantity total_asks = 0;
    for (const auto& level : book.top(Side::Buy, config_.top_levels)) total_bids += level.quantity;
    for (const auto& level : book.top(Side::Sell, config_.top_levels)) total_asks += level.quantity;
    if (total_bids + total_asks > 0) {
      snapshot.top_n_imbalance =
          static_cast<double>(total_bids - total_asks) / static_cast<double>(total_bids + total_asks);
    }
    snapshot.active_orders = book.active_orders();
    snapshot.adds = metrics.adds;
    snapshot.cancels = metrics.cancels;
    snapshot.executions = metrics.executions;
    snapshot.trade_count = metrics.trade_count;
    snapshot.printable_trade_volume = metrics.printable_trade_volume;
    return snapshot;
  }

  void hash_snapshot(const Snapshot& snapshot) {
    logical_checksum_ = hash_value(logical_checksum_, snapshot.stock_locate);
    logical_checksum_ = hash_value(logical_checksum_, snapshot.timestamp_ns);
    logical_checksum_ = hash_value(logical_checksum_, static_cast<std::uint64_t>(snapshot.bid.value_or(-1)));
    logical_checksum_ = hash_value(logical_checksum_, static_cast<std::uint64_t>(snapshot.ask.value_or(-1)));
    logical_checksum_ = hash_value(logical_checksum_, static_cast<std::uint64_t>(snapshot.active_orders));
  }

  ReplayConfig config_;
  MarketState market_;
  ReplayResult result_;
  std::unordered_map<StockLocate, Metrics> metrics_;
  std::unordered_map<StockLocate, std::size_t> snapshots_per_instrument_;
  std::unordered_map<StockLocate, Timestamp> last_snapshot_timestamp_;
  std::uint64_t logical_checksum_{1469598103934665603ULL};
};

}  // namespace

ReplayResult MarketReplayEngine::run(const std::vector<Event>& events, const ReplayConfig& config) const {
  ReplayAccumulator accumulator(config);
  for (const auto& event : events) accumulator.observe(event);
  return accumulator.finish();
}

ReplayResult MarketReplayEngine::run_file(const std::filesystem::path& input, const NasdaqItchParser& parser,
                                          const ReplayConfig& config) const {
  ReplayAccumulator accumulator(config);
  const auto parsed = parser.parse_file(input, [&accumulator](const Event& event) { accumulator.observe(event); });
  if (!parsed)
    throw std::runtime_error("ITCH parse error at byte " + std::to_string(parsed.error->offset) + ": " +
                             parsed.error->message);
  return accumulator.finish(parsed.statistics);
}

void MarketReplayEngine::write(const ReplayResult& result, const std::filesystem::path& out, const std::string& input,
                               const std::string_view input_checksum, const std::string_view data_classification,
                               const std::filesystem::path& provenance) const {
  std::filesystem::create_directories(out);
  if (!provenance.empty()) {
    if (!std::filesystem::is_regular_file(provenance)) throw std::runtime_error("data provenance file does not exist");
    std::filesystem::copy_file(provenance, out / "data_provenance.json",
                               std::filesystem::copy_options::overwrite_existing);
  }
  std::ofstream csv(out / "snapshots.csv");
  if (!csv) throw std::runtime_error("could not open snapshots output");
  csv << "stock_locate,symbol,timestamp_ns,best_bid_price_ticks,best_ask_price_ticks,spread_ticks,mid_price_ticks,"
         "microprice_ticks,bid_depth,ask_depth,level_one_imbalance,top_n_imbalance,active_orders,adds,cancels,"
         "executions,trade_count,printable_trade_volume\n";
  for (const auto& snapshot : result.snapshots) {
    csv << snapshot.stock_locate << ',' << snapshot.symbol << ',' << snapshot.timestamp_ns << ','
        << (snapshot.bid ? std::to_string(*snapshot.bid) : "") << ','
        << (snapshot.ask ? std::to_string(*snapshot.ask) : "") << ','
        << (snapshot.spread ? std::to_string(*snapshot.spread) : "") << ','
        << (snapshot.mid ? std::to_string(*snapshot.mid) : "") << ','
        << (snapshot.microprice ? std::to_string(*snapshot.microprice) : "") << ',' << snapshot.bid_depth << ','
        << snapshot.ask_depth << ','
        << (snapshot.level_one_imbalance ? std::to_string(*snapshot.level_one_imbalance) : "") << ','
        << (snapshot.top_n_imbalance ? std::to_string(*snapshot.top_n_imbalance) : "") << ',' << snapshot.active_orders
        << ',' << snapshot.adds << ',' << snapshot.cancels << ',' << snapshot.executions << ',' << snapshot.trade_count
        << ',' << snapshot.printable_trade_volume << '\n';
  }
  std::ofstream metadata(out / "metadata.json");
  if (!metadata) throw std::runtime_error("could not open replay metadata output");
  metadata << "{\n"
           << "  \"schema_version\": 3,\n"
           << "  \"input_path\": \"" << input << "\",\n"
           << "  \"input_sha256\": \"" << input_checksum << "\",\n"
           << "  \"data_classification\": \"" << data_classification << "\",\n"
           << "  \"provenance_file\": " << (provenance.empty() ? "null" : "\"data_provenance.json\"") << ",\n"
           << "  \"price_scale\": " << kPriceScale << ",\n"
           << "  \"processed_events\": " << result.processed_events << ",\n"
           << "  \"framed_messages\": " << result.decoder_statistics.framed_messages << ",\n"
           << "  \"decoded_messages\": " << result.decoder_statistics.decoded_messages << ",\n"
           << "  \"skipped_messages\": " << result.decoder_statistics.skipped_messages << ",\n"
           << "  \"unknown_messages\": " << result.decoder_statistics.unknown_messages << ",\n"
           << "  \"bytes_processed\": " << result.decoder_statistics.bytes_processed << ",\n"
           << "  \"first_timestamp_ns\": ";
  write_optional_timestamp(metadata, result.decoder_statistics.first_timestamp);
  metadata << ",\n  \"last_timestamp_ns\": ";
  write_optional_timestamp(metadata, result.decoder_statistics.last_timestamp);
  metadata << ",\n  \"decoded_by_type\": ";
  write_type_counts(metadata, result.decoder_statistics.decoded_by_type);
  metadata << ",\n  \"skipped_by_type\": ";
  write_type_counts(metadata, result.decoder_statistics.skipped_by_type);
  metadata << ",\n  \"symbols_discovered\": ";
  write_symbols(metadata, result.decoder_statistics.symbols_discovered);
  metadata << ",\n"
           << "  \"logical_output_checksum\": " << result.logical_checksum << ",\n"
           << "  \"full_state_checksum\": " << result.full_state_checksum << "\n"
           << "}\n";
}

}  // namespace aegisx
