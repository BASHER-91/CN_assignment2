#include "protocol.hpp"

#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <vector>

int parse_instrument(const std::string& token) {
  if (token == "JNST") {
    return kInstJnst;
  }
  if (token == "IMCT") {
    return kInstImct;
  }
  return kInstInvalid;
}

const char* instrument_name(int id) {
  if (id == kInstJnst) {
    return "JNST";
  }
  if (id == kInstImct) {
    return "IMCT";
  }
  return "?";
}

static std::vector<std::string> split_ws(const std::string& line) {
  std::vector<std::string> tokens;
  std::string cur;
  for (char c : line) {
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) {
        tokens.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) {
    tokens.push_back(cur);
  }
  return tokens;
}

static bool parse_bounded_int(const std::string& s, long long lo, long long hi,
                              int* out) {
  if (s.empty() || s[0] == '+' || s[0] == '-') {
    return false;
  }
  for (char c : s) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  errno = 0;
  char* end = nullptr;
  const long long v = std::strtoll(s.c_str(), &end, 10);
  if (errno == ERANGE || end == s.c_str() || *end != '\0') {
    return false;
  }
  if (v < lo || v > hi) {
    return false;
  }
  *out = static_cast<int>(v);
  return true;
}

Command parse_command(const std::string& raw) {
  Command cmd;
  std::string line = raw;
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  const std::vector<std::string> tok = split_ws(line);
  if (tok.empty()) {
    cmd.error = "invalid message";
    return cmd;
  }

  const std::string& op = tok[0];
  if (op == "LOGIN") {
    if (tok.size() != 2 || tok[1].empty()) {
      cmd.error = "invalid username";
      return cmd;
    }
    cmd.type = CmdType::Login;
    cmd.username = tok[1];
    return cmd;
  }
  if (op == "BUY" || op == "SELL") {
    if (tok.size() != 4) {
      cmd.error = "invalid message";
      return cmd;
    }
    cmd.instrument = parse_instrument(tok[1]);
    if (cmd.instrument == kInstInvalid) {
      cmd.error = "unknown instrument";
      return cmd;
    }
    if (!parse_bounded_int(tok[2], kQtyPriceMin, kQtyPriceMax, &cmd.qty)) {
      cmd.error = "invalid quantity";
      return cmd;
    }
    if (!parse_bounded_int(tok[3], kQtyPriceMin, kQtyPriceMax, &cmd.price)) {
      cmd.error = "invalid price";
      return cmd;
    }
    cmd.type = (op == "BUY") ? CmdType::Buy : CmdType::Sell;
    return cmd;
  }
  if (op == "CANCEL") {
    if (tok.size() != 2) {
      cmd.error = "invalid message";
      return cmd;
    }
    if (!parse_bounded_int(tok[1], kOrderIdMin, kOrderIdMax, &cmd.order_id)) {
      cmd.error = "invalid order id";
      return cmd;
    }
    cmd.type = CmdType::Cancel;
    return cmd;
  }
  if (op == "SUBSCRIBE" || op == "UNSUBSCRIBE") {
    if (tok.size() != 2) {
      cmd.error = "invalid message";
      return cmd;
    }
    cmd.instrument = parse_instrument(tok[1]);
    if (cmd.instrument == kInstInvalid) {
      cmd.error = "unknown instrument";
      return cmd;
    }
    cmd.type = (op == "SUBSCRIBE") ? CmdType::Subscribe : CmdType::Unsubscribe;
    return cmd;
  }
  if (op == "QUIT") {
    if (tok.size() != 1) {
      cmd.error = "invalid message";
      return cmd;
    }
    cmd.type = CmdType::Quit;
    return cmd;
  }

  cmd.error = "invalid message";
  return cmd;
}

std::string msg_ok() { return "OK"; }

std::string msg_error(const std::string& reason) { return "ERROR " + reason; }

std::string msg_order_accepted(int order_id) {
  return "ORDER_ACCEPTED " + std::to_string(order_id);
}

std::string msg_order_cancelled(int order_id) {
  return "ORDER_CANCELLED " + std::to_string(order_id);
}

std::string msg_bought(int instrument, int qty, int price) {
  std::ostringstream oss;
  oss << "BOUGHT " << instrument_name(instrument) << " " << qty << " " << price;
  return oss.str();
}

std::string msg_sold(int instrument, int qty, int price) {
  std::ostringstream oss;
  oss << "SOLD " << instrument_name(instrument) << " " << qty << " " << price;
  return oss.str();
}

std::string msg_trade(int instrument, int qty, int price) {
  std::ostringstream oss;
  oss << "TRADE " << instrument_name(instrument) << " " << qty << " " << price;
  return oss.str();
}
