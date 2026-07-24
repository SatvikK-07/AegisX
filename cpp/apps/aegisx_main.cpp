#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <vector>

#include "aegisx/aegisx.h"

namespace {

std::string option(const int argc, char** argv, const std::string_view name, const std::string_view fallback = {}) {
  for (int index = 2; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  return std::string(fallback);
}

bool flag(const int argc, char** argv, const std::string_view name) {
  for (int index = 2; index < argc; ++index) {
    if (argv[index] == name) return true;
  }
  return false;
}

std::optional<aegisx::StockLocate> optional_stock_locate_option(const int argc, char** argv) {
  const std::string value = option(argc, argv, "--stock-locate");
  if (value.empty()) return std::nullopt;
  const unsigned long parsed = std::stoul(value);
  if (parsed > std::numeric_limits<aegisx::StockLocate>::max())
    throw std::runtime_error("stock locate is out of range");
  return static_cast<aegisx::StockLocate>(parsed);
}

aegisx::StockLocate stock_locate_option(const int argc, char** argv) {
  return optional_stock_locate_option(argc, argv).value_or(1);
}

std::optional<aegisx::Timestamp> timestamp_option(const int argc, char** argv, const std::string_view name) {
  const std::string value = option(argc, argv, name);
  if (value.empty()) return std::nullopt;
  return static_cast<aegisx::Timestamp>(std::stoull(value));
}

std::optional<std::size_t> size_option(const int argc, char** argv, const std::string_view name) {
  const std::string value = option(argc, argv, name);
  if (value.empty()) return std::nullopt;
  const auto parsed = std::stoull(value);
  if (parsed > std::numeric_limits<std::size_t>::max()) throw std::runtime_error("size option is out of range");
  return static_cast<std::size_t>(parsed);
}

double double_option(const int argc, char** argv, const std::string_view name, const double fallback) {
  const std::string value = option(argc, argv, name);
  return value.empty() ? fallback : std::stod(value);
}

aegisx::Quantity quantity_option(const int argc, char** argv, const std::string_view name,
                                 const aegisx::Quantity fallback) {
  const std::string value = option(argc, argv, name);
  if (value.empty()) return fallback;
  const auto parsed = std::stoull(value);
  if (parsed > static_cast<unsigned long long>(std::numeric_limits<aegisx::Quantity>::max()))
    throw std::runtime_error("quantity option is out of range");
  return static_cast<aegisx::Quantity>(parsed);
}

aegisx::Side side_option(const int argc, char** argv) {
  const std::string value = option(argc, argv, "--side", "buy");
  if (value == "buy") return aegisx::Side::Buy;
  if (value == "sell") return aegisx::Side::Sell;
  throw std::runtime_error("--side must be buy or sell");
}

std::optional<aegisx::StockLocate> locate_for_symbol(const std::vector<aegisx::Event>& events,
                                                     const std::string_view symbol) {
  for (const auto& event : events) {
    const auto* directory = std::get_if<aegisx::StockDirectory>(&event.payload);
    if (directory != nullptr && directory->symbol == symbol) return event.stock_locate;
  }
  return std::nullopt;
}

aegisx::VolumeProfile load_volume_profile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open VWAP profile");
  aegisx::VolumeProfile profile;
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos) throw std::runtime_error("malformed VWAP profile line");
    const std::string key = line.substr(0, separator);
    const std::string value = line.substr(separator + 1);
    if (key == "training_session") {
      profile.training_session = value;
    } else if (key == "evaluation_session") {
      profile.evaluation_session = value;
    } else if (key == "source_checksum") {
      profile.source_checksum = value;
    } else if (key == "weights") {
      std::istringstream weights(value);
      std::string weight;
      while (std::getline(weights, weight, ',')) profile.interval_weights.push_back(std::stod(weight));
    } else {
      throw std::runtime_error("unknown VWAP profile key: " + key);
    }
  }
  if (profile.training_session.empty() || profile.evaluation_session.empty() || profile.source_checksum.empty() ||
      profile.interval_weights.empty())
    throw std::runtime_error("VWAP profile is missing required fields");
  return profile;
}

aegisx::ReplayConfig replay_config_from(const int argc, char** argv) {
  aegisx::ReplayConfig config;
  config.stock_locate = optional_stock_locate_option(argc, argv);
  const std::string symbol = option(argc, argv, "--symbol");
  if (!symbol.empty()) config.symbol = symbol;
  config.start_timestamp = timestamp_option(argc, argv, "--start-timestamp");
  config.end_timestamp = timestamp_option(argc, argv, "--end-timestamp");
  config.start_event = size_option(argc, argv, "--start-event").value_or(0);
  config.end_event = size_option(argc, argv, "--end-event");
  config.snapshot_every_events = size_option(argc, argv, "--snapshot-every-events").value_or(1);
  config.snapshot_every_ns = timestamp_option(argc, argv, "--snapshot-every-ns");
  config.top_levels = size_option(argc, argv, "--top-levels").value_or(5);
  return config;
}

void write_risk_demo(const std::filesystem::path& output) {
  std::ofstream csv(output / "risk_rejections.csv");
  if (!csv) throw std::runtime_error("could not open risk output");
  csv << "scenario,approved,reject_reason,limit_name,observed_value,configured_limit\n";
  std::ofstream audit(output / "risk_audit.csv");
  if (!audit) throw std::runtime_error("could not open risk audit output");
  audit << "scenario,timestamp_ns,request_id,event,approved,reject_reason,quantity,exposure_ticks\n";
  const auto append = [&](const std::string_view scenario, aegisx::RiskEngine& risk,
                          const aegisx::RiskRequest& request) {
    const auto decision = risk.approve(request);
    csv << scenario << ',' << (decision.approved ? "true" : "false") << ',' << aegisx::to_string(decision.reason) << ','
        << decision.limit_name << ',' << decision.observed_value << ',' << decision.configured_limit << '\n';
    for (const auto& record : risk.audit_log())
      audit << scenario << ',' << record.timestamp << ',' << record.request_id << ',' << record.event << ','
            << (record.approved ? "true" : "false") << ',' << aegisx::to_string(record.reason) << ',' << record.quantity
            << ',' << record.exposure_ticks << '\n';
  };
  const auto engine = [](aegisx::RiskLimits limits = {}) {
    aegisx::RiskEngine risk(limits);
    risk.mark(1, "AAPL", 100, 1);
    return risk;
  };
  {
    auto risk = engine();
    append("valid", risk, {"valid", 1, "AAPL", aegisx::Side::Buy, 10, 100, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_order_quantity = 5;
    auto risk = engine(limits);
    append("quantity_limit", risk, {"quantity", 1, "AAPL", aegisx::Side::Buy, 6, 100, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_order_notional_ticks = 500;
    auto risk = engine(limits);
    append("notional_limit", risk, {"notional", 1, "AAPL", aegisx::Side::Buy, 6, 100, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_position = 5;
    auto risk = engine(limits);
    append("position_limit", risk, {"position", 1, "AAPL", aegisx::Side::Buy, 6, 100, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_gross_exposure_ticks = 500;
    auto risk = engine(limits);
    append("gross_exposure", risk, {"gross", 1, "AAPL", aegisx::Side::Buy, 6, 100, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_net_exposure_ticks = 500;
    auto risk = engine(limits);
    append("net_exposure", risk, {"net", 1, "AAPL", aegisx::Side::Buy, 6, 100, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_symbol_exposure_ticks = 500;
    auto risk = engine(limits);
    append("concentration", risk, {"concentration", 1, "AAPL", aegisx::Side::Buy, 6, 100, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_open_order_exposure_ticks = 500;
    auto risk = engine(limits);
    append("open_order_exposure", risk, {"reservation", 1, "AAPL", aegisx::Side::Buy, 6, 100, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_orders_per_window = 1;
    auto risk = engine(limits);
    static_cast<void>(risk.approve({"rate-primer", 1, "AAPL", aegisx::Side::Buy, 1, 100, 2}));
    append("rate_limit", risk, {"rate", 1, "AAPL", aegisx::Side::Buy, 1, 100, 3});
  }
  {
    aegisx::RiskLimits limits;
    limits.collar_bps = 100.0;
    auto risk = engine(limits);
    append("price_collar", risk, {"collar", 1, "AAPL", aegisx::Side::Buy, 1, 110, 2});
  }
  {
    aegisx::RiskLimits limits;
    limits.stale_ns = 5;
    auto risk = engine(limits);
    append("stale_market", risk, {"stale", 1, "AAPL", aegisx::Side::Buy, 1, 100, 7});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_loss_ticks = 50;
    auto risk = engine(limits);
    risk.fill(1, "AAPL", aegisx::Side::Buy, 10, 100);
    risk.mark(1, "AAPL", 80, 2);
    append("daily_loss", risk, {"loss", 1, "AAPL", aegisx::Side::Buy, 1, 80, 3});
  }
  {
    aegisx::RiskLimits limits;
    limits.max_loss_ticks = 1'000'000;
    limits.max_drawdown_ticks = 50;
    auto risk = engine(limits);
    risk.fill(1, "AAPL", aegisx::Side::Buy, 10, 100);
    risk.mark(1, "AAPL", 120, 2);
    risk.mark(1, "AAPL", 100, 3);
    append("drawdown", risk, {"drawdown", 1, "AAPL", aegisx::Side::Buy, 1, 100, 4});
  }
  {
    auto risk = engine();
    risk.kill(true, 2);
    append("kill_switch", risk, {"kill", 1, "AAPL", aegisx::Side::Buy, 1, 100, 2});
  }
}

void run_benchmark(const std::filesystem::path& input, const std::filesystem::path& output) {
  const auto started = std::chrono::steady_clock::now();
  aegisx::NasdaqItchParser parser({aegisx::UnknownMessagePolicy::Permissive});
  const auto parsed = parser.parse_file(input);
  if (!parsed) throw std::runtime_error(parsed.error->message);
  const auto parsed_at = std::chrono::steady_clock::now();
  aegisx::MarketReplayEngine replay;
  const auto result = replay.run(parsed.events);
  const auto finished = std::chrono::steady_clock::now();
  const auto parser_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(parsed_at - started).count();
  const auto replay_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(finished - parsed_at).count();
  std::filesystem::create_directories(output);
  std::ofstream benchmark(output / "benchmark.json");
  benchmark << "{\n  \"input_bytes\": " << parsed.statistics.bytes_processed
            << ",\n  \"framed_messages\": " << parsed.statistics.framed_messages
            << ",\n  \"parser_nanoseconds\": " << parser_ns << ",\n  \"replay_nanoseconds\": " << replay_ns
            << ",\n  \"replay_events\": " << result.processed_events << "\n}\n";
}

aegisx::Event execution_event(const aegisx::Timestamp timestamp, aegisx::Payload payload) {
  return {timestamp, std::move(payload), 1, 1};
}

std::vector<aegisx::Event> execution_scenario(const aegisx::Price bid, const aegisx::Price ask,
                                              const aegisx::Quantity initial_bid_depth,
                                              const aegisx::Quantity early_volume, const aegisx::Quantity late_volume) {
  return {
      execution_event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      execution_event(1, aegisx::Add{1, aegisx::Side::Buy, initial_bid_depth, bid, "AAPL", {}}),
      execution_event(1, aegisx::Add{3, aegisx::Side::Sell, 20, ask, "AAPL", {}}),
      execution_event(2, aegisx::System{'O'}),
      execution_event(3, aegisx::Trade{10, aegisx::Side::Buy, early_volume, ask, "AAPL", 10, true}),
      execution_event(5, aegisx::System{'Q'}),
      execution_event(6, aegisx::Execute{1, initial_bid_depth, 1}),
      execution_event(7, aegisx::Trade{11, aegisx::Side::Buy, late_volume, ask, "AAPL", 11, true}),
      execution_event(7, aegisx::Add{2, aegisx::Side::Buy, 30, bid, "AAPL", {}}),
      execution_event(8, aegisx::Execute{2, 30, 2}),
  };
}

void run_execution_benchmark(const std::filesystem::path& output) {
  struct Scenario {
    const char* name;
    aegisx::Price bid;
    aegisx::Price ask;
    aegisx::Quantity initial_bid_depth;
    aegisx::Quantity early_volume;
    aegisx::Quantity late_volume;
  };
  const std::vector<Scenario> scenarios{
      {"normal_liquidity", 100'000, 100'200, 10, 50, 50},     {"thin_liquidity", 100'000, 100'500, 5, 20, 20},
      {"high_volatility", 98'000, 102'000, 10, 50, 50},       {"wide_spread", 99'000, 101'000, 10, 50, 50},
      {"front_loaded_volume", 100'000, 100'300, 10, 100, 10}, {"back_loaded_volume", 100'000, 100'300, 10, 10, 100},
  };
  aegisx::ExecutionConfig config;
  config.intervals = 4;
  config.max_child_quantity = 10;
  config.vwap_profile = aegisx::VolumeProfile{
      {0.50, 0.30, 0.15, 0.05}, "synthetic_prior_session", "synthetic_evaluation_session", "deterministic-profile-v1"};
  config.force_completion_at_end = true;
  aegisx::ExecutionSimulator simulator;
  std::filesystem::create_directories(output);
  std::ofstream csv(output / "execution_benchmark.csv");
  if (!csv) throw std::runtime_error("could not open execution benchmark output");
  csv << "scenario,strategy,average_price_ticks,filled,unfilled,fill_rate,implementation_shortfall_bps,"
         "passive_ratio,fees_ticks,schedule_deviation,savings_vs_twap_bps\n";
  double total_savings_bps = 0.0;
  std::vector<double> savings;
  for (const auto& scenario : scenarios) {
    const auto events = execution_scenario(scenario.bid, scenario.ask, scenario.initial_bid_depth,
                                           scenario.early_volume, scenario.late_volume);
    const aegisx::ParentOrder parent{1, 1, "AAPL", aegisx::Side::Buy, 10, 2, 8};
    const auto twap = simulator.run(events, parent, aegisx::Strategy::Twap, config);
    if (!twap.average_price || twap.filled != parent.target_quantity)
      throw std::runtime_error("synthetic TWAP benchmark did not complete");
    for (const auto strategy :
         {aegisx::Strategy::Twap, aegisx::Strategy::Vwap, aegisx::Strategy::Pov, aegisx::Strategy::Adaptive}) {
      const auto report = strategy == aegisx::Strategy::Twap ? twap : simulator.run(events, parent, strategy, config);
      const std::optional<double> savings_bps =
          report.average_price
              ? std::optional<double>{(*twap.average_price - *report.average_price) / *twap.average_price * 10'000.0}
              : std::nullopt;
      csv << scenario.name << ',' << aegisx::to_string(strategy) << ','
          << (report.average_price ? std::to_string(*report.average_price) : "") << ',' << report.filled << ','
          << report.unfilled << ',' << report.fill_rate << ','
          << (report.implementation_shortfall_bps ? std::to_string(*report.implementation_shortfall_bps) : "") << ','
          << report.passive_ratio << ',' << report.fees << ',' << report.maximum_schedule_deviation << ','
          << (savings_bps ? std::to_string(*savings_bps) : "") << '\n';
      if (strategy == aegisx::Strategy::Adaptive) {
        if (!savings_bps || report.filled != parent.target_quantity)
          throw std::runtime_error("synthetic adaptive benchmark did not complete");
        total_savings_bps += *savings_bps;
        savings.push_back(*savings_bps);
      }
    }
  }
  const double mean_savings = total_savings_bps / static_cast<double>(savings.size());
  double variance = 0.0;
  for (const double value : savings) variance += (value - mean_savings) * (value - mean_savings);
  const double standard_deviation = std::sqrt(variance / static_cast<double>(savings.size()));
  std::ofstream metadata(output / "execution_benchmark.json");
  metadata << "{\n  \"scenarios\": " << scenarios.size() << ",\n  \"strategies\": 4"
           << ",\n  \"mean_adaptive_savings_bps\": " << mean_savings
           << ",\n  \"standard_deviation_adaptive_savings_bps\": " << standard_deviation << ",\n  \"synthetic\": true"
           << ",\n  \"evidence_classification\": \"deterministic_regression_only\"\n}\n";
}

void run_risk_benchmark(const std::filesystem::path& output, const std::size_t iterations) {
  if (iterations < 100) throw std::runtime_error("risk benchmark requires at least 100 iterations");
  aegisx::RiskLimits limits;
  limits.max_order_quantity = 1;
  limits.max_order_notional_ticks = 1'000;
  limits.max_open_order_exposure_ticks = 1'000;
  limits.max_orders_per_window = std::numeric_limits<std::uint32_t>::max();
  limits.max_open_orders = 2;
  limits.enable_audit_log = false;
  aegisx::RiskEngine risk(limits);
  risk.mark(1, "AAPL", 100, 1);
  std::vector<std::int64_t> samples;
  samples.reserve(iterations);
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < iterations; ++index) {
    const std::string id = "benchmark-" + std::to_string(index);
    const auto before = std::chrono::steady_clock::now();
    const auto decision =
        risk.approve({id, 1, "AAPL", aegisx::Side::Buy, 1, 100, static_cast<aegisx::Timestamp>(index + 2)});
    const auto after = std::chrono::steady_clock::now();
    if (!decision.approved) throw std::runtime_error("risk benchmark approval failed");
    risk.release(id);
    samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
  }
  const auto finished = std::chrono::steady_clock::now();
  std::sort(samples.begin(), samples.end());
  const std::size_t p50_index = (samples.size() * 50U + 99U) / 100U - 1U;
  const std::size_t p95_index = (samples.size() * 95U + 99U) / 100U - 1U;
  const std::size_t p99_index = (samples.size() * 99U + 99U) / 100U - 1U;
  const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();
  const double checks_per_second = static_cast<double>(iterations) * 1'000'000'000.0 / static_cast<double>(elapsed_ns);
  std::filesystem::create_directories(output);
  std::ofstream metadata(output / "risk_benchmark.json");
  if (!metadata) throw std::runtime_error("could not open risk benchmark output");
  metadata << "{\n  \"iterations\": " << iterations << ",\n  \"timed_operation\": \"approve only\""
           << ",\n  \"throughput_operation\": \"approve plus reservation release\""
           << ",\n  \"p50_nanoseconds\": " << samples[p50_index] << ",\n  \"p95_nanoseconds\": " << samples[p95_index]
           << ",\n  \"p99_nanoseconds\": " << samples[p99_index] << ",\n  \"max_nanoseconds\": " << samples.back()
           << ",\n  \"checks_per_second\": " << checks_per_second
           << ",\n  \"enabled_limits\": [\"order_quantity\", \"order_notional\", \"position\", "
              "\"symbol_exposure\", \"open_order_exposure\", \"gross_exposure\", \"net_exposure\", "
              "\"concentration\", \"rate\", \"collar\", \"staleness\", \"loss\", \"drawdown\", \"kill_switch\"]"
           << ",\n  \"reservation_updates\": true\n}\n";
}

}  // namespace

int main(const int argc, char** argv) {
  try {
    if (argc < 2) {
      throw std::runtime_error(
          "usage: aegisx replay|simulate|risk-demo|benchmark|execution-benchmark|risk-benchmark --input FILE --output "
          "DIR");
    }
    const std::string command = argv[1];
    const std::filesystem::path input = option(argc, argv, "--input");
    const std::filesystem::path output =
        option(argc, argv, "--output", command == "replay" ? "runs/replay" : "runs/demo");
    const bool permissive = option(argc, argv, "--unknown-policy", "strict") == "permissive";
    aegisx::NasdaqItchParser parser(
        {permissive ? aegisx::UnknownMessagePolicy::Permissive : aegisx::UnknownMessagePolicy::Strict});
    if ((command == "replay" || command == "simulate" || command == "benchmark") && input.empty())
      throw std::runtime_error("--input is required; use scripts/run_real_pipeline.py for the real-data workflow");

    if (command == "replay") {
      const aegisx::ReplayConfig config = replay_config_from(argc, argv);
      aegisx::MarketReplayEngine replay;
      const auto result = replay.run_file(input, parser, config);
      const std::filesystem::path provenance = option(argc, argv, "--provenance");
      const std::string_view data_classification =
          provenance.empty() ? "deterministic_fixture_or_unspecified" : "real_exchange_historical_sample";
      replay.write(result, output, input.string(), aegisx::sha256_file(input), data_classification, provenance);
      std::cout << "AegisX replayed " << result.processed_events << " events; checksum " << result.logical_checksum
                << '\n';
    } else if (command == "simulate") {
      const auto parsed = parser.parse_file(input);
      if (!parsed)
        throw std::runtime_error("parse error at byte " + std::to_string(parsed.error->offset) + ": " +
                                 parsed.error->message);
      const std::string symbol = option(argc, argv, "--symbol", "AAPL");
      const auto locate =
          optional_stock_locate_option(argc, argv)
              .value_or(locate_for_symbol(parsed.events, symbol).value_or(stock_locate_option(argc, argv)));
      constexpr aegisx::Timestamp kRegularSessionOpen = 34'200'000'000'000ULL;
      constexpr aegisx::Timestamp kRegularSessionClose = 57'600'000'000'000ULL;
      const bool historical_session =
          parsed.statistics.last_timestamp && *parsed.statistics.last_timestamp > 1'000'000'000'000ULL;
      const aegisx::Timestamp arrival =
          timestamp_option(argc, argv, "--start-timestamp")
              .value_or(historical_session ? kRegularSessionOpen : static_cast<aegisx::Timestamp>(2));
      const aegisx::Timestamp end =
          timestamp_option(argc, argv, "--end-timestamp")
              .value_or(historical_session ? kRegularSessionClose : static_cast<aegisx::Timestamp>(20));
      const aegisx::Quantity quantity = quantity_option(argc, argv, "--quantity", historical_session ? 1'000 : 20);
      const std::size_t parent_count = size_option(argc, argv, "--parent-count").value_or(1);
      if (parent_count == 0) throw std::runtime_error("--parent-count must be positive");
      const aegisx::Timestamp parent_duration =
          timestamp_option(argc, argv, "--parent-duration-ns").value_or(end - arrival);
      if (parent_duration == 0 || parent_duration > end - arrival)
        throw std::runtime_error("--parent-duration-ns must fit inside the requested session");
      const bool alternate_sides = flag(argc, argv, "--alternate-sides");
      const aegisx::Side requested_side = side_option(argc, argv);
      aegisx::ExecutionConfig execution_config;
      const auto requested_intervals = size_option(argc, argv, "--intervals");
      const std::filesystem::path vwap_profile_path = option(argc, argv, "--vwap-profile");
      if (!vwap_profile_path.empty()) {
        execution_config.vwap_profile = load_volume_profile(vwap_profile_path);
        execution_config.intervals = execution_config.vwap_profile->interval_weights.size();
        if (requested_intervals && *requested_intervals != execution_config.intervals)
          throw std::runtime_error("--intervals does not match the supplied VWAP profile");
      } else {
        execution_config.intervals = requested_intervals.value_or(historical_session ? 13 : 4);
        execution_config.vwap_weights.assign(execution_config.intervals,
                                             1.0 / static_cast<double>(execution_config.intervals));
      }
      execution_config.max_child_quantity =
          quantity_option(argc, argv, "--max-child-quantity", historical_session ? 100 : 10);
      execution_config.pov_rate = double_option(argc, argv, "--pov-rate", 0.1);
      execution_config.adaptive_cancel_replace_on_urgency = historical_session;
      aegisx::ExecutionSimulator simulator;
      std::vector<aegisx::ExecutionReport> reports;
      const aegisx::Timestamp parent_step =
          parent_count == 1 ? 0 : (end - arrival - parent_duration) / static_cast<aegisx::Timestamp>(parent_count - 1);
      for (std::size_t parent_index = 0; parent_index < parent_count; ++parent_index) {
        const aegisx::Timestamp parent_arrival = arrival + parent_step * static_cast<aegisx::Timestamp>(parent_index);
        const aegisx::Side parent_side =
            alternate_sides && parent_index % 2 == 1
                ? (requested_side == aegisx::Side::Buy ? aegisx::Side::Sell : aegisx::Side::Buy)
                : requested_side;
        const aegisx::ParentOrder parent{static_cast<aegisx::ParentOrderId>(parent_index + 1),
                                         locate,
                                         symbol,
                                         parent_side,
                                         quantity,
                                         parent_arrival,
                                         parent_arrival + parent_duration};
        for (const auto strategy :
             {aegisx::Strategy::Twap, aegisx::Strategy::Vwap, aegisx::Strategy::Pov, aegisx::Strategy::Adaptive}) {
          reports.push_back(simulator.run(parsed.events, parent, strategy, execution_config));
        }
      }
      simulator.write(reports, output);
      const std::filesystem::path provenance = option(argc, argv, "--provenance");
      if (!provenance.empty()) {
        if (!std::filesystem::is_regular_file(provenance))
          throw std::runtime_error("data provenance file does not exist");
        std::filesystem::copy_file(provenance, output / "data_provenance.json",
                                   std::filesystem::copy_options::overwrite_existing);
      }
      if (!vwap_profile_path.empty()) {
        std::filesystem::copy_file(vwap_profile_path, output / "vwap_profile.txt",
                                   std::filesystem::copy_options::overwrite_existing);
        const std::filesystem::path audit = vwap_profile_path.string() + ".json";
        if (std::filesystem::is_regular_file(audit))
          std::filesystem::copy_file(audit, output / "vwap_profile.json",
                                     std::filesystem::copy_options::overwrite_existing);
      }
      std::ofstream context(output / "execution_context.json");
      if (!context) throw std::runtime_error("could not open execution context output");
      context << "{\n"
              << "  \"input_path\": \"" << input.string() << "\",\n"
              << "  \"input_sha256\": \"" << aegisx::sha256_file(input) << "\",\n"
              << "  \"data_classification\": \""
              << (provenance.empty() ? "deterministic_fixture_or_unspecified" : "real_exchange_historical_sample")
              << "\",\n"
              << "  \"symbol\": \"" << symbol << "\",\n"
              << "  \"stock_locate\": " << locate << ",\n"
              << "  \"side\": \"" << aegisx::to_string(requested_side) << "\",\n"
              << "  \"alternate_sides\": " << (alternate_sides ? "true" : "false") << ",\n"
              << "  \"quantity_per_parent\": " << quantity << ",\n"
              << "  \"parent_count\": " << parent_count << ",\n"
              << "  \"parent_duration_ns\": " << parent_duration << ",\n"
              << "  \"arrival_timestamp_ns\": " << arrival << ",\n"
              << "  \"end_timestamp_ns\": " << end << ",\n"
              << "  \"intervals\": " << execution_config.intervals << ",\n"
              << "  \"vwap_profile\": \""
              << (vwap_profile_path.empty() ? "uniform_baseline_no_prior_session" : "earlier_real_session") << "\"\n"
              << "}\n";
      std::cout << "AegisX simulated " << reports.size() << " strategies\n";
    } else if (command == "risk-demo") {
      std::filesystem::create_directories(output);
      write_risk_demo(output);
      std::cout << "AegisX wrote risk demonstration\n";
    } else if (command == "benchmark") {
      run_benchmark(input, output);
      std::cout << "AegisX wrote benchmark report\n";
    } else if (command == "execution-benchmark") {
      run_execution_benchmark(output);
      std::cout << "AegisX wrote deterministic fixture benchmark; it is not real-market evidence\n";
    } else if (command == "risk-benchmark") {
      run_risk_benchmark(output, size_option(argc, argv, "--iterations").value_or(1'000'000));
      std::cout << "AegisX wrote risk benchmark\n";
    } else {
      throw std::runtime_error("unknown command: " + command);
    }
  } catch (const std::exception& error) {
    std::cerr << "aegisx: " << error.what() << '\n';
    return 1;
  }
}
