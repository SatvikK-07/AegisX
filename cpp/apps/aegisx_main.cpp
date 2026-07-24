#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

#include "aegisx/aegisx.h"

namespace {

std::string option(const int argc, char** argv, const std::string_view name, const std::string_view fallback = {}) {
  for (int index = 2; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  return std::string(fallback);
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
  aegisx::RiskLimits limits;
  limits.max_order_quantity = 20;
  limits.max_open_order_exposure_ticks = 2'000;
  limits.max_orders_per_window = 2;
  limits.max_drawdown_ticks = 50;
  aegisx::RiskEngine risk(limits);
  risk.mark(1, "AAPL", 100, 1);
  const auto append = [&](const std::string_view scenario, const aegisx::RiskRequest& request) {
    const auto decision = risk.approve(request);
    csv << scenario << ',' << (decision.approved ? "true" : "false") << ',' << aegisx::to_string(decision.reason) << ','
        << decision.limit_name << ',' << decision.observed_value << ',' << decision.configured_limit << '\n';
  };
  append("valid", {"valid", 1, "AAPL", aegisx::Side::Buy, 10, 100, 2});
  append("quantity", {"quantity", 1, "AAPL", aegisx::Side::Buy, 21, 100, 3});
  append("reserved_exposure", {"reserved", 1, "AAPL", aegisx::Side::Buy, 20, 100, 4});
  append("second_valid", {"second", 1, "AAPL", aegisx::Side::Buy, 1, 100, 5});
  append("rate_limit", {"rate", 1, "AAPL", aegisx::Side::Buy, 1, 100, 6});
  append("stale", {"stale", 1, "AAPL", aegisx::Side::Buy, 1, 100, 6'000'000'002ULL});
  risk.kill(true);
  append("kill_switch", {"kill", 1, "AAPL", aegisx::Side::Buy, 1, 100, 6});
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

std::vector<aegisx::Event> execution_scenario(const aegisx::Price bid, const aegisx::Price ask) {
  return {
      execution_event(1, aegisx::StockDirectory{"AAPL", 'Q', 'N', false, 100}),
      execution_event(1, aegisx::Add{1, aegisx::Side::Buy, 10, bid, "AAPL", {}}),
      execution_event(1, aegisx::Add{3, aegisx::Side::Sell, 20, ask, "AAPL", {}}),
      execution_event(2, aegisx::System{'O'}),
      execution_event(5, aegisx::System{'Q'}),
      execution_event(6, aegisx::Execute{1, 10, 1}),
      execution_event(7, aegisx::Add{2, aegisx::Side::Buy, 10, bid, "AAPL", {}}),
      execution_event(8, aegisx::Execute{2, 10, 2}),
  };
}

void run_execution_benchmark(const std::filesystem::path& output) {
  struct Scenario {
    const char* name;
    aegisx::Price bid;
    aegisx::Price ask;
  };
  const std::vector<Scenario> scenarios{
      {"tight_spread", 100'000, 100'200}, {"wide_spread", 99'000, 100'000}, {"volatile_spread", 98'000, 101'000}};
  aegisx::ExecutionConfig config;
  config.intervals = 2;
  config.max_child_quantity = 5;
  config.force_completion_at_end = false;
  aegisx::ExecutionSimulator simulator;
  std::filesystem::create_directories(output);
  std::ofstream csv(output / "execution_benchmark.csv");
  if (!csv) throw std::runtime_error("could not open execution benchmark output");
  csv << "scenario,twap_average_price_ticks,adaptive_average_price_ticks,adaptive_savings_bps\n";
  double total_savings_bps = 0.0;
  for (const auto& scenario : scenarios) {
    const auto events = execution_scenario(scenario.bid, scenario.ask);
    const aegisx::ParentOrder parent{1, 1, "AAPL", aegisx::Side::Buy, 10, 2, 8};
    const auto twap = simulator.run(events, parent, aegisx::Strategy::Twap, config);
    const auto adaptive = simulator.run(events, parent, aegisx::Strategy::Adaptive, config);
    if (!twap.average_price || !adaptive.average_price || twap.filled != parent.target_quantity ||
        adaptive.filled != parent.target_quantity) {
      throw std::runtime_error("synthetic execution benchmark did not complete both parents");
    }
    const double savings_bps = (*twap.average_price - *adaptive.average_price) / *twap.average_price * 10'000.0;
    total_savings_bps += savings_bps;
    csv << scenario.name << ',' << *twap.average_price << ',' << *adaptive.average_price << ',' << savings_bps << '\n';
  }
  std::ofstream metadata(output / "execution_benchmark.json");
  metadata << "{\n  \"scenarios\": " << scenarios.size()
           << ",\n  \"mean_adaptive_savings_bps\": " << total_savings_bps / static_cast<double>(scenarios.size())
           << "\n}\n";
}

void run_risk_benchmark(const std::filesystem::path& output, const std::size_t iterations) {
  if (iterations < 100) throw std::runtime_error("risk benchmark requires at least 100 iterations");
  aegisx::RiskLimits limits;
  limits.max_order_quantity = 1;
  limits.max_order_notional_ticks = 1'000;
  limits.max_open_order_exposure_ticks = 1'000;
  limits.max_orders_per_window = std::numeric_limits<std::uint32_t>::max();
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
  const std::size_t p99_index = (samples.size() * 99U + 99U) / 100U - 1U;
  const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();
  const double checks_per_second = static_cast<double>(iterations) * 1'000'000'000.0 / static_cast<double>(elapsed_ns);
  std::filesystem::create_directories(output);
  std::ofstream metadata(output / "risk_benchmark.json");
  if (!metadata) throw std::runtime_error("could not open risk benchmark output");
  metadata << "{\n  \"iterations\": " << iterations << ",\n  \"p99_nanoseconds\": " << samples[p99_index]
           << ",\n  \"checks_per_second\": " << checks_per_second << "\n}\n";
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
    const std::filesystem::path input = option(argc, argv, "--input", "data/fixtures/aegisx_itch_sample.itch");
    const std::filesystem::path output =
        option(argc, argv, "--output", command == "replay" ? "runs/replay" : "runs/demo");
    const bool permissive = option(argc, argv, "--unknown-policy", "strict") == "permissive";
    aegisx::NasdaqItchParser parser(
        {permissive ? aegisx::UnknownMessagePolicy::Permissive : aegisx::UnknownMessagePolicy::Strict});

    if (command == "replay") {
      const aegisx::ReplayConfig config = replay_config_from(argc, argv);
      aegisx::MarketReplayEngine replay;
      const auto result = replay.run_file(input, parser, config);
      replay.write(result, output, input.string(), aegisx::sha256_file(input));
      std::cout << "AegisX replayed " << result.processed_events << " events; checksum " << result.logical_checksum
                << '\n';
    } else if (command == "simulate") {
      const auto parsed = parser.parse_file(input);
      if (!parsed)
        throw std::runtime_error("parse error at byte " + std::to_string(parsed.error->offset) + ": " +
                                 parsed.error->message);
      const auto locate = stock_locate_option(argc, argv);
      const std::string symbol = option(argc, argv, "--symbol", "AAPL");
      aegisx::ParentOrder parent{1, locate, symbol, aegisx::Side::Buy, 20, 2, 20};
      aegisx::ExecutionConfig execution_config;
      execution_config.max_child_quantity = 10;
      execution_config.vwap_weights = {0.1, 0.2, 0.3, 0.4};
      aegisx::ExecutionSimulator simulator;
      std::vector<aegisx::ExecutionReport> reports;
      for (const auto strategy :
           {aegisx::Strategy::Twap, aegisx::Strategy::Vwap, aegisx::Strategy::Pov, aegisx::Strategy::Adaptive}) {
        reports.push_back(simulator.run(parsed.events, parent, strategy, execution_config));
      }
      simulator.write(reports, output);
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
      std::cout << "AegisX wrote synthetic execution benchmark\n";
    } else if (command == "risk-benchmark") {
      run_risk_benchmark(output, size_option(argc, argv, "--iterations").value_or(100'000));
      std::cout << "AegisX wrote risk benchmark\n";
    } else {
      throw std::runtime_error("unknown command: " + command);
    }
  } catch (const std::exception& error) {
    std::cerr << "aegisx: " << error.what() << '\n';
    return 1;
  }
}
