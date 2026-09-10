#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <string>

constexpr int kQtyPriceMin = 1;
constexpr int kQtyPriceMax = 2147483647;
constexpr int kOrderIdMin = 0;
constexpr int kOrderIdMax = 2147483647;

constexpr int kInstJnst = 0;
constexpr int kInstImct = 1;
constexpr int kInstInvalid = -1;
constexpr int kNumInstruments = 2;

constexpr std::size_t kMaxInBuf = 64 * 1024;

// Outbound backlog allowed per connection once the kernel send buffer is full.
// Generous on purpose: a slow reader must be able to build a visible Send-Q
// before we give up on it, but a client that never reads cannot grow the
// server's memory without bound.
constexpr std::size_t kMaxOutBuf = 4 * 1024 * 1024;

enum class CmdType {
  Login,
  Buy,
  Sell,
  Cancel,
  Subscribe,
  Unsubscribe,
  Quit,
  Invalid
};

struct Command {
  CmdType type = CmdType::Invalid;
  std::string username;
  int instrument = kInstInvalid;
  int qty = 0;
  int price = 0;
  int order_id = 0;
  std::string error;
};

int parse_instrument(const std::string& token);
const char* instrument_name(int id);

// Parse one complete application line (no trailing newline).
Command parse_command(const std::string& line);

std::string msg_ok();
std::string msg_error(const std::string& reason);
std::string msg_order_accepted(int order_id);
std::string msg_order_cancelled(int order_id);
std::string msg_bought(int instrument, int qty, int price);
std::string msg_sold(int instrument, int qty, int price);
std::string msg_trade(int instrument, int qty, int price);

#endif
