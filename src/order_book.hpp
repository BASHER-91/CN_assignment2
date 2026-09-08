#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

struct Order {
  int id = 0;
  std::uint64_t session_id = 0;
  int owner_fd = -1;
  int remaining_qty = 0;
  int price = 0;
  bool is_buy = false;
  int instrument_id = 0;
};

struct Trade {
  int instrument_id = 0;
  int qty = 0;
  int price = 0;
  int buy_fd = -1;
  std::uint64_t buy_session = 0;
  int sell_fd = -1;
  std::uint64_t sell_session = 0;
};

class OrderBook {
 public:
  enum class CancelStatus { Ok, NotFound, NotOwner, AlreadyGone };

  // Insert a new order, match at the same price, rest any remainder.
  // Returns the assigned order id, or -1 if ids are exhausted.
  int submit(bool is_buy, int instrument_id, int qty, int price, int owner_fd,
             std::uint64_t session_id, std::vector<Trade>* trades);

  CancelStatus cancel(int order_id, std::uint64_t session_id);

 private:
  void match_incoming(Order& incoming, std::vector<Trade>* trades);

  std::unordered_map<int, Order> by_id_;
  std::unordered_map<int, std::deque<int>> buys_[2];
  std::unordered_map<int, std::deque<int>> sells_[2];
  int next_order_id_ = 0;
};

#endif
