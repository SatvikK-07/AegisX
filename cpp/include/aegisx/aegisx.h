#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace aegisx {

// Monetary values are signed fixed-point price ticks: displayed price × 10,000.
using Price = std::int64_t;
using Quantity = std::int64_t;
using Money = std::int64_t;
using OrderId = std::uint64_t;
using ParentOrderId = std::uint64_t;
using ChildOrderId = std::uint64_t;
using Timestamp = std::uint64_t;  // Nanoseconds since midnight in ITCH.
using StockLocate = std::uint16_t;
using TrackingNumber = std::uint16_t;
using MatchNumber = std::uint64_t;

constexpr Price kPriceScale = 10'000;

enum class Side : std::uint8_t { Buy, Sell };
std::string to_string(Side side);

struct Add {
  OrderId id;
  Side side;
  Quantity quantity;
  Price price;
  std::string symbol;
  std::optional<std::string> mpid;
};
struct Execute {
  OrderId id;
  Quantity quantity;
  MatchNumber match_number;
};
struct ExecuteWithPrice {
  OrderId id;
  Quantity quantity;
  MatchNumber match_number;
  bool printable;
  Price execution_price;
};
struct Cancel {
  OrderId id;
  Quantity quantity;
};
struct Delete {
  OrderId id;
};
struct Replace {
  OrderId old_id;
  OrderId new_id;
  Quantity quantity;
  Price price;
};
struct Trade {
  OrderId order_id;
  Side side;
  Quantity quantity;
  Price price;
  std::string symbol;
  MatchNumber match_number;
  bool printable{true};
};
struct CrossTrade {
  Quantity quantity;
  std::string symbol;
  Price price;
  MatchNumber match_number;
  char cross_type;
};
struct BrokenTrade {
  MatchNumber match_number;
};
struct StockDirectory {
  std::string symbol;
  char market_category;
  char financial_status{};
  bool round_lot_only{};
  std::uint32_t round_lot_size{};
};
struct StockTradingAction {
  std::string symbol;
  char state;
  std::string reason;
};
struct System {
  char code;
};
using Payload = std::variant<Add, Execute, ExecuteWithPrice, Cancel, Delete, Replace, Trade, CrossTrade, BrokenTrade,
                             StockDirectory, StockTradingAction, System>;
struct Event {
  Timestamp timestamp_ns{};
  Payload payload{System{'?'}};
  StockLocate stock_locate{};
  TrackingNumber tracking_number{};
};

struct ParseError {
  std::size_t offset{};
  char type{'?'};
  std::string message;
};
enum class UnknownMessagePolicy { Strict, Permissive };
struct ItchParserOptions {
  UnknownMessagePolicy unknown_message_policy{UnknownMessagePolicy::Strict};
};
struct ItchMessageDefinition {
  char type;
  std::size_t body_length;
  bool decoded;
  std::string_view name;
};
struct DecoderStatistics {
  std::uint64_t framed_messages{};
  std::uint64_t decoded_messages{};
  std::uint64_t skipped_messages{};
  std::uint64_t unknown_messages{};
  std::uint64_t bytes_processed{};
  std::map<char, std::uint64_t> decoded_by_type;
  std::map<char, std::uint64_t> skipped_by_type;
  std::optional<Timestamp> first_timestamp;
  std::optional<Timestamp> last_timestamp;
  std::vector<std::string> symbols_discovered;
};
struct ParseResult {
  std::vector<Event> events;
  std::optional<ParseError> error;
  DecoderStatistics statistics;
  explicit operator bool() const { return !error; }
};

using EventConsumer = std::function<void(const Event&)>;
class NasdaqItchParser {
 public:
  explicit NasdaqItchParser(ItchParserOptions options = {});
  static std::span<const ItchMessageDefinition> definitions();
  ParseResult parse(std::span<const std::uint8_t> bytes) const;
  ParseResult parse(const std::vector<std::uint8_t>& bytes) const { return parse(std::span(bytes)); }
  ParseResult parse_file(const std::filesystem::path& path) const;
  ParseResult parse_file(const std::filesystem::path& path, const EventConsumer& consumer) const;

 private:
  ItchParserOptions options_;
};

struct DepthLevel {
  Price price{};
  Quantity quantity{};
};
struct OrderView {
  OrderId id{};
  Side side{};
  Price price{};
  Quantity quantity{};
};
class OrderBook {
 public:
  void apply(const Event& event);
  void add(OrderId id, Side side, Quantity quantity, Price price);
  void cancel(OrderId id, Quantity quantity);
  void execute(OrderId id, Quantity quantity);
  void erase(OrderId id);
  void replace(OrderId old_id, OrderId new_id, Quantity quantity, Price price);
  std::optional<Price> best_bid() const;
  std::optional<Price> best_ask() const;
  std::optional<double> mid_price() const;
  std::optional<double> microprice() const;
  Quantity depth_at(Side side, Price price) const;
  std::vector<OrderId> fifo_at(Side side, Price price) const;
  std::vector<DepthLevel> top(Side side, std::size_t count) const;
  std::optional<OrderView> order(OrderId id) const;
  std::size_t active_orders() const;
  std::string invariant_error() const;
  std::uint64_t top_of_book_checksum() const;
  std::uint64_t aggregate_depth_checksum() const;
  std::uint64_t full_order_state_checksum() const;
  std::uint64_t fifo_state_checksum() const;

 private:
  struct Resting {
    OrderId id{};
    Quantity quantity{};
  };
  struct Level {
    std::list<Resting> fifo;
    Quantity total{};
  };
  struct Locator {
    Side side{};
    Price price{};
    std::list<Resting>::iterator iterator;
  };
  std::map<Price, Level, std::greater<Price>> bids_;
  std::map<Price, Level> asks_;
  std::unordered_map<OrderId, Locator> orders_;
  void reduce(OrderId id, Quantity quantity);
};

class MarketState {
 public:
  void apply(const Event& event);
  const OrderBook* book_for(StockLocate stock_locate) const;
  const OrderBook* book_for_symbol(std::string_view symbol) const;
  std::optional<std::string> symbol_for(StockLocate stock_locate) const;
  std::optional<StockLocate> route_for(OrderId id) const;
  bool is_tradable(StockLocate stock_locate) const;
  std::size_t instrument_count() const;
  std::size_t active_order_count() const;
  std::uint64_t full_state_checksum() const;
  std::string invariant_error() const;

 private:
  struct Instrument {
    std::string symbol;
    bool tradable{true};
    OrderBook book;
  };
  std::unordered_map<StockLocate, Instrument> instruments_;
  std::unordered_map<OrderId, StockLocate> order_to_instrument_;
};

struct ReplayConfig {
  std::optional<StockLocate> stock_locate;
  std::optional<std::string> symbol;
  std::optional<Timestamp> start_timestamp;
  std::optional<Timestamp> end_timestamp;
  std::size_t start_event{};
  std::optional<std::size_t> end_event;
  std::size_t snapshot_every_events{1};
  std::optional<Timestamp> snapshot_every_ns;
  std::size_t top_levels{5};
};
struct Snapshot {
  StockLocate stock_locate{};
  std::string symbol;
  Timestamp timestamp_ns{};
  std::optional<Price> bid;
  std::optional<Price> ask;
  std::optional<Price> spread;
  std::optional<double> mid;
  std::optional<double> microprice;
  Quantity bid_depth{};
  Quantity ask_depth{};
  std::optional<double> level_one_imbalance;
  std::optional<double> top_n_imbalance;
  std::size_t active_orders{};
  std::uint64_t adds{};
  std::uint64_t cancels{};
  std::uint64_t executions{};
  std::uint64_t trade_count{};
  Quantity printable_trade_volume{};
};
struct ReplayResult {
  std::vector<Snapshot> snapshots;
  std::uint64_t logical_checksum{};
  std::uint64_t full_state_checksum{};
  std::size_t processed_events{};
  DecoderStatistics decoder_statistics;
};
class MarketReplayEngine {
 public:
  ReplayResult run(const std::vector<Event>& events, const ReplayConfig& config = {}) const;
  ReplayResult run_file(const std::filesystem::path& input, const NasdaqItchParser& parser,
                        const ReplayConfig& config = {}) const;
  void write(const ReplayResult& result, const std::filesystem::path& out, const std::string& input,
             std::string_view input_checksum = "") const;
};

enum class Strategy { Twap, Vwap, Pov, Adaptive };
enum class OrderType { Market, Limit };
enum class ChildOrderState { Pending, Open, Filled, Cancelled, Rejected };
enum class LiquidityRole { Maker, Taker };
std::string to_string(Strategy strategy);
std::string to_string(LiquidityRole role);
struct ParentOrder {
  ParentOrderId id{};
  StockLocate stock_locate{};
  std::string symbol;
  Side side{};
  Quantity target_quantity{};
  Timestamp arrival_time{};
  Timestamp end_time{};
};
struct ChildOrder {
  ChildOrderId id{};
  ParentOrderId parent_id{};
  Side side{};
  OrderType type{};
  Price limit_price{};
  Quantity submitted_quantity{};
  Quantity remaining_quantity{};
  Timestamp submission_time{};
  ChildOrderState state{ChildOrderState::Pending};
  Quantity queue_ahead{};
};
struct Fill {
  ChildOrderId child_order_id{};
  Timestamp timestamp{};
  Price price{};
  Quantity quantity{};
  LiquidityRole liquidity_role{};
  Money fee{};
};
struct ExecutionConfig {
  std::size_t intervals{4};
  std::vector<double> vwap_weights;
  double pov_rate{0.1};
  Quantity max_child_quantity{100};
  Timestamp decision_latency_ns{};
  Timestamp transmission_latency_ns{};
  double cancellation_queue_fraction{0.5};
  Money maker_fee_per_share{};
  Money taker_fee_per_share{};
  bool force_completion_at_end{true};
};
struct ExecutionReport {
  Strategy strategy{};
  StockLocate stock_locate{};
  std::string symbol;
  Quantity filled{};
  Quantity unfilled{};
  double fill_rate{};
  std::optional<double> average_price;
  std::optional<double> implementation_shortfall_ticks;
  Money fees{};
  Money opportunity_cost_ticks{};
  double passive_ratio{};
  double aggressive_ratio{};
  std::size_t child_order_count{};
  std::size_t cancel_count{};
  Quantity depth_consumed{};
  std::vector<ChildOrder> children;
  std::vector<Fill> fills;
};
class ExecutionSimulator {
 public:
  ExecutionReport run(const std::vector<Event>& events, const ParentOrder& parent, Strategy strategy,
                      const ExecutionConfig& config = {}) const;
  void write(const std::vector<ExecutionReport>& reports, const std::filesystem::path& out) const;
};

enum class RiskRejectReason {
  None,
  InvalidOrder,
  OrderQuantityLimit,
  OrderNotionalLimit,
  PositionLimit,
  GrossExposureLimit,
  NetExposureLimit,
  ConcentrationLimit,
  OpenOrderExposureLimit,
  OrderRateLimit,
  PriceCollarViolation,
  StaleMarketData,
  DailyLossLimit,
  DrawdownLimit,
  TradingDisabled,
  KillSwitchActive,
};
std::string to_string(RiskRejectReason reason);
struct RiskLimits {
  Quantity max_order_quantity{100};
  Money max_order_notional_ticks{10'000'000};
  Quantity max_position{1'000};
  Money max_gross_exposure_ticks{100'000'000};
  Money max_net_exposure_ticks{100'000'000};
  Money max_symbol_exposure_ticks{50'000'000};
  Money max_open_order_exposure_ticks{20'000'000};
  std::uint32_t max_orders_per_window{100};
  Timestamp order_rate_window_ns{1'000'000'000ULL};
  double collar_bps{100.0};
  Timestamp stale_ns{5'000'000'000ULL};
  Money max_loss_ticks{1'000'000};
  Money max_drawdown_ticks{1'000'000};
  bool enabled{true};
  bool kill_switch{};
};
struct RiskRequest {
  std::string id;
  StockLocate stock_locate{};
  std::string symbol;
  Side side{};
  Quantity quantity{};
  Price price{};
  Timestamp timestamp{};
};
struct RiskDecision {
  bool approved{};
  RiskRejectReason reason{RiskRejectReason::None};
  std::string limit_name;
  std::int64_t observed_value{};
  std::int64_t configured_limit{};
};
struct PositionState {
  Quantity net_quantity{};
  Price average_open_price{};
  Money realized_pnl{};
  Money unrealized_pnl{};
  Money fees{};
};
class RiskEngine {
 public:
  explicit RiskEngine(RiskLimits limits = {});
  void mark(StockLocate stock_locate, std::string symbol, Price price, Timestamp timestamp);
  RiskDecision approve(const RiskRequest& request);
  void release(std::string_view request_id);
  void fill(std::string_view request_id, Quantity quantity, Price price, Money fee_ticks = 0);
  void fill(StockLocate stock_locate, std::string_view symbol, Side side, Quantity quantity, Price price,
            Money fee_ticks = 0);
  void kill(bool active);
  const PositionState* position(StockLocate stock_locate) const;
  Money total_realized_pnl() const;
  Money total_unrealized_pnl() const;
  Money total_pnl() const;
  Money drawdown() const;
  Money gross_exposure() const;
  Money reserved_exposure() const;

 private:
  struct Mark {
    std::string symbol;
    Price price{};
    Timestamp timestamp{};
  };
  struct Reservation {
    StockLocate stock_locate{};
    Side side{};
    Quantity remaining{};
    Price price{};
  };
  RiskLimits limits_;
  std::map<StockLocate, PositionState> positions_;
  std::map<StockLocate, Mark> marks_;
  std::map<std::string, Reservation, std::less<>> reservations_;
  std::vector<Timestamp> approval_timestamps_;
  Money high_water_pnl_{};
};

std::string price_text(std::optional<Price> price);
std::string sha256_file(const std::filesystem::path& path);

}  // namespace aegisx
