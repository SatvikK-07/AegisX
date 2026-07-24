#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

#include "aegisx/aegisx.h"

namespace aegisx {
namespace {

constexpr std::array<ItchMessageDefinition, 22> kDefinitions{{
    {'S', 12, true, "System Event"},
    {'R', 39, true, "Stock Directory"},
    {'H', 25, true, "Stock Trading Action"},
    {'Y', 20, false, "Reg SHO Restriction"},
    {'L', 26, false, "Market Participant Position"},
    {'V', 35, false, "MWCB Decline Level"},
    {'W', 12, false, "MWCB Status"},
    {'K', 28, false, "IPO Quoting Period Update"},
    {'J', 35, false, "LULD Auction Collar"},
    {'h', 21, false, "Operational Halt"},
    {'A', 36, true, "Add Order"},
    {'F', 40, true, "Add Order with MPID Attribution"},
    {'E', 31, true, "Order Executed"},
    {'C', 36, true, "Order Executed With Price"},
    {'X', 23, true, "Order Cancel"},
    {'D', 19, true, "Order Delete"},
    {'U', 35, true, "Order Replace"},
    {'P', 44, true, "Trade"},
    {'Q', 40, true, "Cross Trade"},
    {'B', 19, true, "Broken Trade"},
    {'I', 50, false, "Net Order Imbalance Indicator"},
    {'N', 20, false, "Retail Price Improvement Indicator"},
}};

class Reader {
 public:
  explicit Reader(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  std::uint8_t u8() {
    if (cursor_ >= bytes_.size()) throw std::runtime_error("ITCH decoder read past message boundary");
    return bytes_[cursor_++];
  }
  std::uint16_t u16() { return static_cast<std::uint16_t>((static_cast<std::uint16_t>(u8()) << 8U) | u8()); }
  std::uint32_t u32() {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) value = (value << 8U) | u8();
    return value;
  }
  std::uint64_t u48() {
    std::uint64_t value = 0;
    for (int index = 0; index < 6; ++index) value = (value << 8U) | u8();
    return value;
  }
  std::uint64_t u64() {
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) value = (value << 8U) | u8();
    return value;
  }
  std::string text(std::size_t count) {
    std::string value;
    value.reserve(count);
    while (count-- > 0) value.push_back(static_cast<char>(u8()));
    while (!value.empty() && value.back() == ' ') value.pop_back();
    return value;
  }
  void skip_to_end() { cursor_ = bytes_.size(); }
  [[nodiscard]] bool consumed() const { return cursor_ == bytes_.size(); }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t cursor_{};
};

const ItchMessageDefinition* definition_for(const char type) {
  const auto found = std::find_if(kDefinitions.begin(), kDefinitions.end(),
                                  [type](const ItchMessageDefinition& definition) { return definition.type == type; });
  return found == kDefinitions.end() ? nullptr : &*found;
}

std::optional<Side> parse_side(const char value) {
  if (value == 'B') return Side::Buy;
  if (value == 'S') return Side::Sell;
  return {};
}

ParseResult failure(const std::size_t offset, const char type, std::string message, DecoderStatistics statistics) {
  return {{}, ParseError{offset, type, std::move(message)}, std::move(statistics)};
}

void add_symbol(DecoderStatistics& statistics, const std::string& symbol) {
  if (!symbol.empty() && std::find(statistics.symbols_discovered.begin(), statistics.symbols_discovered.end(),
                                   symbol) == statistics.symbols_discovered.end()) {
    statistics.symbols_discovered.push_back(symbol);
  }
}

void observe_timestamp(DecoderStatistics& statistics, const Timestamp timestamp) {
  if (!statistics.first_timestamp || timestamp < *statistics.first_timestamp) statistics.first_timestamp = timestamp;
  if (!statistics.last_timestamp || timestamp > *statistics.last_timestamp) statistics.last_timestamp = timestamp;
}

struct DecodedFrame {
  std::optional<Event> event;
  std::optional<ParseError> error;
};

DecodedFrame decode_frame(const std::span<const std::uint8_t> body, const std::size_t offset,
                          const ItchParserOptions options, DecoderStatistics& statistics) {
  if (body.empty()) return {{}, ParseError{offset, '?', "zero-length ITCH message"}};
  const char type = static_cast<char>(body.front());
  const auto* definition = definition_for(type);
  if (definition == nullptr) {
    ++statistics.unknown_messages;
    if (options.unknown_message_policy == UnknownMessagePolicy::Strict) {
      return {{}, ParseError{offset, type, "unknown ITCH message type in strict mode"}};
    }
    ++statistics.skipped_messages;
    ++statistics.skipped_by_type[type];
    return {};
  }
  if (body.size() != definition->body_length) {
    return {{}, ParseError{offset, type, "incorrect ITCH message length for " + std::string(definition->name)}};
  }

  Reader reader(body);
  static_cast<void>(reader.u8());
  const StockLocate stock_locate = reader.u16();
  const TrackingNumber tracking_number = reader.u16();
  const Timestamp timestamp = reader.u48();
  observe_timestamp(statistics, timestamp);
  if (!definition->decoded) {
    ++statistics.skipped_messages;
    ++statistics.skipped_by_type[type];
    return {};
  }

  Event event{timestamp, System{'?'}, stock_locate, tracking_number};
  if (type == 'S') {
    event.payload = System{static_cast<char>(reader.u8())};
  } else if (type == 'R') {
    const std::string symbol = reader.text(8);
    const char market_category = static_cast<char>(reader.u8());
    const char financial_status = static_cast<char>(reader.u8());
    const std::uint32_t round_lot_size = reader.u32();
    const bool round_lot_only = reader.u8() == 'Y';
    static_cast<void>(reader.u8());
    static_cast<void>(reader.text(2));
    static_cast<void>(reader.u8());
    static_cast<void>(reader.u8());
    static_cast<void>(reader.u8());
    static_cast<void>(reader.u8());
    static_cast<void>(reader.u8());
    static_cast<void>(reader.u32());
    static_cast<void>(reader.u8());
    reader.skip_to_end();
    add_symbol(statistics, symbol);
    event.payload = StockDirectory{symbol, market_category, financial_status, round_lot_only, round_lot_size};
  } else if (type == 'H') {
    const std::string symbol = reader.text(8);
    const char state = static_cast<char>(reader.u8());
    static_cast<void>(reader.u8());
    const std::string reason = reader.text(4);
    add_symbol(statistics, symbol);
    event.payload = StockTradingAction{symbol, state, reason};
  } else if (type == 'A' || type == 'F') {
    const OrderId id = reader.u64();
    const auto side = parse_side(static_cast<char>(reader.u8()));
    const Quantity quantity = static_cast<Quantity>(reader.u32());
    const std::string symbol = reader.text(8);
    const Price price = static_cast<Price>(reader.u32());
    std::optional<std::string> mpid;
    if (type == 'F') mpid = reader.text(4);
    if (!side || quantity <= 0) return {{}, ParseError{offset, type, "invalid add side or quantity"}};
    add_symbol(statistics, symbol);
    event.payload = Add{id, *side, quantity, price, symbol, std::move(mpid)};
  } else if (type == 'E' || type == 'C') {
    const OrderId id = reader.u64();
    const Quantity quantity = static_cast<Quantity>(reader.u32());
    const MatchNumber match_number = reader.u64();
    if (quantity <= 0) return {{}, ParseError{offset, type, "zero executed quantity"}};
    if (type == 'C') {
      const bool printable = reader.u8() == 'Y';
      event.payload = ExecuteWithPrice{id, quantity, match_number, printable, static_cast<Price>(reader.u32())};
    } else {
      event.payload = Execute{id, quantity, match_number};
    }
  } else if (type == 'X') {
    const OrderId id = reader.u64();
    const Quantity quantity = static_cast<Quantity>(reader.u32());
    if (quantity <= 0) return {{}, ParseError{offset, type, "zero cancel quantity"}};
    event.payload = Cancel{id, quantity};
  } else if (type == 'D') {
    event.payload = Delete{reader.u64()};
  } else if (type == 'U') {
    const OrderId old_id = reader.u64();
    const OrderId new_id = reader.u64();
    const Quantity quantity = static_cast<Quantity>(reader.u32());
    const Price price = static_cast<Price>(reader.u32());
    if (quantity <= 0 || price < 0) return {{}, ParseError{offset, type, "invalid replacement fields"}};
    event.payload = Replace{old_id, new_id, quantity, price};
  } else if (type == 'P') {
    const OrderId order_id = reader.u64();
    const auto side = parse_side(static_cast<char>(reader.u8()));
    const Quantity quantity = static_cast<Quantity>(reader.u32());
    const std::string symbol = reader.text(8);
    const Price price = static_cast<Price>(reader.u32());
    const MatchNumber match_number = reader.u64();
    if (!side || quantity <= 0) return {{}, ParseError{offset, type, "invalid trade side or quantity"}};
    add_symbol(statistics, symbol);
    event.payload = Trade{order_id, *side, quantity, price, symbol, match_number, true};
  } else if (type == 'Q') {
    const Quantity quantity = static_cast<Quantity>(reader.u64());
    const std::string symbol = reader.text(8);
    const Price price = static_cast<Price>(reader.u32());
    const MatchNumber match_number = reader.u64();
    const char cross_type = static_cast<char>(reader.u8());
    if (quantity < 0) return {{}, ParseError{offset, type, "invalid cross quantity"}};
    add_symbol(statistics, symbol);
    event.payload = CrossTrade{quantity, symbol, price, match_number, cross_type};
  } else {  // 'B'
    event.payload = BrokenTrade{reader.u64()};
  }
  if (!reader.consumed()) return {{}, ParseError{offset, type, "decoder did not consume message body"}};
  ++statistics.decoded_messages;
  ++statistics.decoded_by_type[type];
  return {std::move(event), {}};
}

template <typename FrameConsumer>
ParseResult parse_bytes(const std::span<const std::uint8_t> bytes, const ItchParserOptions options,
                        FrameConsumer&& consumer) {
  DecoderStatistics statistics;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < 2) return failure(offset, '?', "truncated two-byte message length", statistics);
    const std::size_t length = (static_cast<std::size_t>(bytes[offset]) << 8U) | bytes[offset + 1];
    if (length == 0 || length > bytes.size() - offset - 2) {
      const char type = offset + 2 < bytes.size() ? static_cast<char>(bytes[offset + 2]) : '?';
      return failure(offset, type, "invalid or truncated ITCH message body", statistics);
    }
    ++statistics.framed_messages;
    statistics.bytes_processed += length + 2;
    const auto decoded = decode_frame(bytes.subspan(offset + 2, length), offset, options, statistics);
    if (decoded.error) return failure(decoded.error->offset, decoded.error->type, decoded.error->message, statistics);
    if (decoded.event) consumer(*decoded.event);
    offset += length + 2;
  }
  return {{}, std::nullopt, std::move(statistics)};
}

}  // namespace

NasdaqItchParser::NasdaqItchParser(const ItchParserOptions options) : options_(options) {}

std::span<const ItchMessageDefinition> NasdaqItchParser::definitions() { return kDefinitions; }

ParseResult NasdaqItchParser::parse(const std::span<const std::uint8_t> bytes) const {
  std::vector<Event> events;
  auto result = parse_bytes(bytes, options_, [&events](const Event& event) { events.push_back(event); });
  if (result) result.events = std::move(events);
  return result;
}

ParseResult NasdaqItchParser::parse_file(const std::filesystem::path& path) const {
  std::vector<Event> events;
  auto result = parse_file(path, [&events](const Event& event) { events.push_back(event); });
  if (result) result.events = std::move(events);
  return result;
}

ParseResult NasdaqItchParser::parse_file(const std::filesystem::path& path, const EventConsumer& consumer) const {
  std::ifstream input(path, std::ios::binary);
  if (!input) return failure(0, '?', "could not open ITCH input", {});
  DecoderStatistics statistics;
  std::size_t offset = 0;
  while (true) {
    std::array<char, 2> header{};
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    const std::streamsize read_header = input.gcount();
    if (read_header == 0 && input.eof()) break;
    if (read_header != static_cast<std::streamsize>(header.size())) {
      return failure(offset, '?', "truncated two-byte message length", statistics);
    }
    const std::size_t length =
        (static_cast<std::size_t>(static_cast<unsigned char>(header[0])) << 8U) | static_cast<unsigned char>(header[1]);
    if (length == 0) return failure(offset, '?', "zero-length ITCH message", statistics);
    std::vector<std::uint8_t> body(length);
    input.read(reinterpret_cast<char*>(body.data()), static_cast<std::streamsize>(body.size()));
    if (input.gcount() != static_cast<std::streamsize>(body.size())) {
      const char type = input.gcount() > 0 ? static_cast<char>(body.front()) : '?';
      return failure(offset, type, "truncated ITCH message body", statistics);
    }
    ++statistics.framed_messages;
    statistics.bytes_processed += length + 2;
    const auto decoded = decode_frame(body, offset, options_, statistics);
    if (decoded.error) return failure(decoded.error->offset, decoded.error->type, decoded.error->message, statistics);
    if (decoded.event) consumer(*decoded.event);
    offset += length + 2;
  }
  return {{}, std::nullopt, std::move(statistics)};
}

}  // namespace aegisx
