#include "order_book.hpp"

#include "protocol.hpp"

#include <algorithm>

int OrderBook::submit(bool is_buy, int instrument_id, int qty, int price,
                      int owner_fd, std::uint64_t session_id,
                      std::vector<Trade>* trades) {
  if (next_order_id_ < kOrderIdMin || next_order_id_ > kOrderIdMax) {
    return -1;
  }

  Order order;
  order.id = next_order_id_;
  if (next_order_id_ == kOrderIdMax) {
    next_order_id_ = kOrderIdMin - 1;  // exhausted
  } else {
    ++next_order_id_;
  }
  order.session_id = session_id;
  order.owner_fd = owner_fd;
  order.remaining_qty = qty;
  order.price = price;
  order.is_buy = is_buy;
  order.instrument_id = instrument_id;

  by_id_[order.id] = order;
  match_incoming(by_id_[order.id], trades);
  return order.id;
}

OrderBook::CancelStatus OrderBook::cancel(int order_id,
                                         std::uint64_t session_id) {
  const auto it = by_id_.find(order_id);
  if (it == by_id_.end()) {
    return CancelStatus::NotFound;
  }
  if (it->second.session_id != session_id) {
    return CancelStatus::NotOwner;
  }
  if (it->second.remaining_qty <= 0) {
    return CancelStatus::AlreadyGone;
  }
  it->second.remaining_qty = 0;
  return CancelStatus::Ok;
}

void OrderBook::match_incoming(Order& incoming, std::vector<Trade>* trades) {
  auto& opp_map = incoming.is_buy ? sells_[incoming.instrument_id]
                                  : buys_[incoming.instrument_id];
  // Look the price level up instead of indexing it: operator[] would insert an
  // empty deque for every price ever quoted on the opposite side and never
  // remove it again.
  const auto opp_it = opp_map.find(incoming.price);
  if (opp_it != opp_map.end()) {
    std::deque<int>& opp = opp_it->second;

    while (incoming.remaining_qty > 0) {
      // Skip fronts that were cancelled or fully filled (lazy deletion).
      auto rest_it = by_id_.end();
      while (!opp.empty()) {
        rest_it = by_id_.find(opp.front());
        if (rest_it == by_id_.end() || rest_it->second.remaining_qty == 0) {
          opp.pop_front();
          rest_it = by_id_.end();
          continue;
        }
        break;
      }
      if (rest_it == by_id_.end()) {
        break;
      }

      Order& rest = rest_it->second;
      const int tq = std::min(incoming.remaining_qty, rest.remaining_qty);
      incoming.remaining_qty -= tq;
      rest.remaining_qty -= tq;

      Trade tr;
      tr.instrument_id = incoming.instrument_id;
      tr.qty = tq;
      tr.price = incoming.price;
      if (incoming.is_buy) {
        tr.buy_fd = incoming.owner_fd;
        tr.buy_session = incoming.session_id;
        tr.sell_fd = rest.owner_fd;
        tr.sell_session = rest.session_id;
      } else {
        tr.buy_fd = rest.owner_fd;
        tr.buy_session = rest.session_id;
        tr.sell_fd = incoming.owner_fd;
        tr.sell_session = incoming.session_id;
      }
      trades->push_back(tr);

      if (rest.remaining_qty == 0) {
        opp.pop_front();
      }
    }

    if (opp.empty()) {
      opp_map.erase(opp_it);
    }
  }

  if (incoming.remaining_qty > 0) {
    auto& book_map = incoming.is_buy ? buys_[incoming.instrument_id]
                                     : sells_[incoming.instrument_id];
    book_map[incoming.price].push_back(incoming.id);
  }
}
