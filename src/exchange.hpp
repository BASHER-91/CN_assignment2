#ifndef EXCHANGE_HPP
#define EXCHANGE_HPP

#include "order_book.hpp"
#include "protocol.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

enum class Role { Unknown, Trader, MarketData };

struct Connection {
  std::uint64_t session_id = 0;
  Role role = Role::Unknown;
  std::string username;
  std::uint64_t sub_session_jnst = 0;
  std::uint64_t sub_session_imct = 0;
  std::string in_buf;
  std::string out_buf;
  bool write_armed = false;
};

class Exchange {
 public:
  static constexpr int kClientSlots = 100000;

  explicit Exchange(int kq);

  void add_client(int fd);
  void on_read(int fd, bool eof);
  void on_write(int fd);
  void teardown(int fd);

 private:
  bool valid_fd(int fd) const;
  bool live(int fd, std::uint64_t session) const;
  void enqueue(int fd, const std::string& line);
  void flush(int fd);
  void arm_write(int fd);
  void disarm_write(int fd);
  void process_buffer(int fd);
  void handle_command(int fd, const Command& cmd);
  void handle_login(int fd, const Command& cmd);
  void handle_order(int fd, const Command& cmd, bool is_buy);
  void handle_cancel(int fd, const Command& cmd);
  void handle_subscribe(int fd, const Command& cmd);
  void handle_unsubscribe(int fd, const Command& cmd);
  void notify_trade(const Trade& trade);
  void broadcast_trade(int instrument_id, int qty, int price);
  std::uint64_t& sub_session(Connection& c, int instrument_id);
  std::vector<int>& subs(int instrument_id);

  int kq_;
  std::vector<Connection> clients_;
  OrderBook book_;
  std::vector<int> subs_jnst_;
  std::vector<int> subs_imct_;
  std::unordered_map<std::string, int> usernames_;
  std::uint64_t next_session_ = 1;
};

#endif
