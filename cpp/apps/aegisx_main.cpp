#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>

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

}  // namespace

int main(const int argc, char** argv) {
  try {
    if (argc < 2) {
      throw std::runtime_error("usage: aegisx replay|simulate|risk-demo|benchmark --input FILE --output DIR");
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
    } else {
      throw std::runtime_error("unknown command: " + command);
    }
  } catch (const std::exception& error) {
    std::cerr << "aegisx: " << error.what() << '\n';
    return 1;
  }
}
