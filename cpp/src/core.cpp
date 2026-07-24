#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "aegisx/aegisx.h"

namespace aegisx {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256Constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::uint32_t rotate_right(const std::uint32_t value, const std::uint32_t count) {
  return (value >> count) | (value << (32U - count));
}

void sha256_block(const std::uint8_t* block, std::array<std::uint32_t, 8>& state) {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t index = 0; index < 16; ++index) {
    words[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24U) |
                   (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16U) |
                   (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8U) |
                   static_cast<std::uint32_t>(block[index * 4 + 3]);
  }
  for (std::size_t index = 16; index < words.size(); ++index) {
    const std::uint32_t s0 =
        rotate_right(words[index - 15], 7U) ^ rotate_right(words[index - 15], 18U) ^ (words[index - 15] >> 3U);
    const std::uint32_t s1 =
        rotate_right(words[index - 2], 17U) ^ rotate_right(words[index - 2], 19U) ^ (words[index - 2] >> 10U);
    words[index] = words[index - 16] + s0 + words[index - 7] + s1;
  }

  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  std::uint32_t e = state[4];
  std::uint32_t f = state[5];
  std::uint32_t g = state[6];
  std::uint32_t h = state[7];
  for (std::size_t index = 0; index < words.size(); ++index) {
    const std::uint32_t s1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
    const std::uint32_t choice = (e & f) ^ (~e & g);
    const std::uint32_t temp1 = h + s1 + choice + kSha256Constants[index] + words[index];
    const std::uint32_t s0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

}  // namespace

std::string to_string(const Side side) { return side == Side::Buy ? "buy" : "sell"; }

std::string to_string(const Strategy strategy) {
  switch (strategy) {
    case Strategy::Twap:
      return "twap";
    case Strategy::Vwap:
      return "vwap";
    case Strategy::Pov:
      return "pov";
    case Strategy::Adaptive:
      return "adaptive";
  }
  return "unknown";
}

std::string to_string(const LiquidityRole role) { return role == LiquidityRole::Maker ? "maker" : "taker"; }

std::string to_string(const ExecutionAction action) {
  switch (action) {
    case ExecutionAction::Wait:
      return "wait";
    case ExecutionAction::SubmitPassive:
      return "submit_passive";
    case ExecutionAction::SubmitAggressive:
      return "submit_aggressive";
    case ExecutionAction::CancelPassive:
      return "cancel_passive";
    case ExecutionAction::RepricePassive:
      return "reprice_passive";
  }
  return "unknown";
}

std::string to_string(const RiskRejectReason reason) {
  switch (reason) {
    case RiskRejectReason::None:
      return "none";
    case RiskRejectReason::InvalidOrder:
      return "invalid_order";
    case RiskRejectReason::OrderQuantityLimit:
      return "order_quantity_limit";
    case RiskRejectReason::OrderNotionalLimit:
      return "order_notional_limit";
    case RiskRejectReason::PositionLimit:
      return "position_limit";
    case RiskRejectReason::GrossExposureLimit:
      return "gross_exposure_limit";
    case RiskRejectReason::NetExposureLimit:
      return "net_exposure_limit";
    case RiskRejectReason::ConcentrationLimit:
      return "concentration_limit";
    case RiskRejectReason::OpenOrderExposureLimit:
      return "open_order_exposure_limit";
    case RiskRejectReason::OrderRateLimit:
      return "order_rate_limit";
    case RiskRejectReason::PriceCollarViolation:
      return "price_collar_violation";
    case RiskRejectReason::StaleMarketData:
      return "stale_market_data";
    case RiskRejectReason::DailyLossLimit:
      return "daily_loss_limit";
    case RiskRejectReason::DrawdownLimit:
      return "drawdown_limit";
    case RiskRejectReason::TradingDisabled:
      return "trading_disabled";
    case RiskRejectReason::KillSwitchActive:
      return "kill_switch_active";
  }
  return "unknown";
}

std::string price_text(const std::optional<Price> price) {
  if (!price) return "n/a";
  std::ostringstream output;
  output << std::fixed << std::setprecision(4) << static_cast<double>(*price) / static_cast<double>(kPriceScale);
  return output.str();
}

std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open file for SHA-256");
  std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                     0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<std::uint8_t, 64> block{};
  std::uint64_t total_bytes = 0;
  while (input.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()))) {
    sha256_block(block.data(), state);
    total_bytes += block.size();
  }
  const std::streamsize tail_size = input.gcount();
  if (tail_size < 0) throw std::runtime_error("failed while reading file for SHA-256");
  total_bytes += static_cast<std::uint64_t>(tail_size);
  const auto tail = block;
  block.fill(0);
  for (std::streamsize index = 0; index < tail_size; ++index) {
    block[static_cast<std::size_t>(index)] = tail[static_cast<std::size_t>(index)];
  }
  block[static_cast<std::size_t>(tail_size)] = 0x80U;
  if (tail_size >= 56) {
    sha256_block(block.data(), state);
    block.fill(0);
  }
  const std::uint64_t bit_length = total_bytes * 8U;
  for (std::size_t index = 0; index < 8; ++index) {
    block[63U - index] = static_cast<std::uint8_t>((bit_length >> (index * 8U)) & 0xffU);
  }
  sha256_block(block.data(), state);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const std::uint32_t word : state) output << std::setw(8) << word;
  return output.str();
}

}  // namespace aegisx
