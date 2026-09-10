#include "exchange.hpp"

#include "net.hpp"

#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>

namespace {

constexpr std::size_t kRecvChunk = 4096;

}  // namespace

Exchange::Exchange(int kq) : kq_(kq), clients_(kClientSlots) {}

bool Exchange::valid_fd(int fd) const {
  return fd >= 0 && fd < static_cast<int>(clients_.size());
}

bool Exchange::live(int fd, std::uint64_t session) const {
  return session != 0 && valid_fd(fd) && clients_[fd].session_id == session;
}

std::uint64_t& Exchange::sub_session(Connection& c, int instrument_id) {
  return (instrument_id == kInstJnst) ? c.sub_session_jnst : c.sub_session_imct;
}

std::vector<int>& Exchange::subs(int instrument_id) {
  return (instrument_id == kInstJnst) ? subs_jnst_ : subs_imct_;
}

std::uint64_t Exchange::session_of(int fd) const {
  if (!valid_fd(fd)) {
    return 0;
  }
  return clients_[fd].session_id;
}

void Exchange::erase_from_subs(int fd) {
  for (int inst = 0; inst < kNumInstruments; ++inst) {
    std::vector<int>& v = subs(inst);
    std::size_t i = 0;
    while (i < v.size()) {
      if (v[i] == fd) {
        v[i] = v.back();
        v.pop_back();
        continue;
      }
      ++i;
    }
  }
}

std::uint64_t Exchange::add_client(int fd) {
  if (fd < 0) {
    return 0;
  }
  if (fd >= static_cast<int>(clients_.size())) {
    // Grow geometrically. Resizing to exactly fd + 1 would move the whole
    // table on every accept once descriptors climb past the initial size,
    // making accepts quadratic during the 70k-connection experiment.
    const std::size_t needed = static_cast<std::size_t>(fd) + 1;
    clients_.resize(std::max(needed, clients_.size() * 2));
  }
  Connection& c = clients_[fd];
  c = Connection{};
  c.session_id = next_session_++;
  return c.session_id;
}

void Exchange::teardown(int fd) {
  if (!valid_fd(fd)) {
    return;
  }
  Connection& c = clients_[fd];
  if (c.session_id == 0) {
    return;
  }
  if (c.role == Role::Trader && !c.username.empty()) {
    auto it = usernames_.find(c.username);
    if (it != usernames_.end() && it->second == fd) {
      usernames_.erase(it);
    }
  }
  erase_from_subs(fd);
  c.session_id = 0;
  c.role = Role::Unknown;
  c.username.clear();
  c.sub_session_jnst = 0;
  c.sub_session_imct = 0;
  c.in_buf.clear();
  c.in_buf.shrink_to_fit();
  c.out_buf.clear();
  c.out_buf.shrink_to_fit();
  c.write_armed = false;
  c.closing_after_write = false;
  close_fd(fd);
}

void Exchange::close_after_flush(int fd) {
  if (!valid_fd(fd) || clients_[fd].session_id == 0) {
    return;
  }
  Connection& c = clients_[fd];
  c.closing_after_write = true;
  c.in_buf.clear();
  if (c.out_buf.empty()) {
    teardown(fd);
    return;
  }
  struct kevent ev;
  EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
  kevent(kq_, &ev, 1, nullptr, 0, nullptr);
  arm_write(fd);
}

void Exchange::arm_write(int fd) {
  if (!valid_fd(fd) || clients_[fd].write_armed) {
    return;
  }
  struct kevent ev;
  EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD, 0, 0,
         reinterpret_cast<void*>(
             static_cast<std::uintptr_t>(clients_[fd].session_id)));
  if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) < 0) {
    teardown(fd);
    return;
  }
  clients_[fd].write_armed = true;
}

void Exchange::disarm_write(int fd) {
  if (!valid_fd(fd) || !clients_[fd].write_armed) {
    return;
  }
  struct kevent ev;
  EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
  if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) < 0 && errno != ENOENT) {
    // The filter is still registered, so leave write_armed set rather than
    // letting our state drift from the kernel's.
    return;
  }
  clients_[fd].write_armed = false;
}

void Exchange::flush(int fd) {
  if (!valid_fd(fd) || clients_[fd].session_id == 0) {
    return;
  }
  Connection& c = clients_[fd];
  while (!c.out_buf.empty()) {
    const ssize_t n =
        send(fd, c.out_buf.data(), c.out_buf.size(), 0);
    if (n > 0) {
      c.out_buf.erase(0, static_cast<std::size_t>(n));
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      arm_write(fd);
      return;
    }
    teardown(fd);
    return;
  }
  disarm_write(fd);
  if (valid_fd(fd) && clients_[fd].session_id != 0 &&
      clients_[fd].closing_after_write) {
    teardown(fd);
  }
}

void Exchange::enqueue(int fd, const std::string& line) {
  if (!valid_fd(fd) || clients_[fd].session_id == 0) {
    return;
  }
  Connection& c = clients_[fd];
  if (c.out_buf.size() + line.size() + 1 > kMaxOutBuf) {
    // The peer has stopped reading and is now consuming unbounded memory.
    teardown(fd);
    return;
  }
  c.out_buf += line;
  c.out_buf += '\n';
  flush(fd);
}

void Exchange::on_write(int fd) {
  if (!valid_fd(fd) || clients_[fd].session_id == 0) {
    return;
  }
  flush(fd);
}

void Exchange::process_buffer(int fd) {
  Connection& c = clients_[fd];
  while (c.session_id != 0 && !c.closing_after_write) {
    const std::size_t pos = c.in_buf.find('\n');
    if (pos == std::string::npos) {
      break;
    }
    std::string line = c.in_buf.substr(0, pos);
    c.in_buf.erase(0, pos + 1);
    if (line.empty()) {
      continue;
    }
    const Command cmd = parse_command(line);
    handle_command(fd, cmd);
  }
}

void Exchange::on_read(int fd, bool eof) {
  if (!valid_fd(fd) || clients_[fd].session_id == 0) {
    return;
  }

  char buf[kRecvChunk];
  for (;;) {
    const ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
      Connection& c = clients_[fd];
      if (c.in_buf.size() + static_cast<std::size_t>(n) > kMaxInBuf) {
        teardown(fd);
        return;
      }
      c.in_buf.append(buf, static_cast<std::size_t>(n));
      process_buffer(fd);
      if (!valid_fd(fd) || clients_[fd].session_id == 0 ||
          clients_[fd].closing_after_write) {
        return;
      }
      if (!eof) {
        return;
      }
      // EV_EOF can arrive with more than one recv chunk still buffered.
      continue;
    }

    if (n == 0) {
      process_buffer(fd);
      close_after_flush(fd);
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (eof) {
        process_buffer(fd);
        close_after_flush(fd);
      }
      return;
    }

    // Hard error (RST / ECONNRESET / EPIPE): the peer cannot receive replies.
    teardown(fd);
    return;
  }
}

void Exchange::handle_command(int fd, const Command& cmd) {
  Connection& c = clients_[fd];
  if (cmd.type == CmdType::Invalid) {
    enqueue(fd, msg_error(cmd.error.empty() ? "invalid message" : cmd.error));
    return;
  }
  if (cmd.type == CmdType::Quit) {
    close_after_flush(fd);
    return;
  }

  if (c.role == Role::Unknown) {
    if (cmd.type == CmdType::Login) {
      handle_login(fd, cmd);
      return;
    }
    if (cmd.type == CmdType::Subscribe) {
      handle_subscribe(fd, cmd);
      return;
    }
    if (cmd.type == CmdType::Buy || cmd.type == CmdType::Sell ||
        cmd.type == CmdType::Cancel) {
      enqueue(fd, msg_error("Not logged in"));
      return;
    }
    enqueue(fd, msg_error("invalid message"));
    return;
  }

  if (c.role == Role::Trader) {
    if (cmd.type == CmdType::Buy) {
      handle_order(fd, cmd, true);
      return;
    }
    if (cmd.type == CmdType::Sell) {
      handle_order(fd, cmd, false);
      return;
    }
    if (cmd.type == CmdType::Cancel) {
      handle_cancel(fd, cmd);
      return;
    }
    if (cmd.type == CmdType::Login) {
      enqueue(fd, msg_error("already logged in"));
      return;
    }
    enqueue(fd, msg_error("not a market-data client"));
    return;
  }

  // MarketData
  if (cmd.type == CmdType::Subscribe) {
    handle_subscribe(fd, cmd);
    return;
  }
  if (cmd.type == CmdType::Unsubscribe) {
    handle_unsubscribe(fd, cmd);
    return;
  }
  enqueue(fd, msg_error("not a trader"));
}

void Exchange::handle_login(int fd, const Command& cmd) {
  Connection& c = clients_[fd];
  const auto it = usernames_.find(cmd.username);
  if (it != usernames_.end()) {
    const int other = it->second;
    if (valid_fd(other) && clients_[other].session_id != 0 &&
        clients_[other].username == cmd.username) {
      enqueue(fd, msg_error("username taken"));
      return;
    }
    usernames_.erase(it);
  }
  c.role = Role::Trader;
  c.username = cmd.username;
  usernames_[cmd.username] = fd;
  enqueue(fd, msg_ok());
}

void Exchange::handle_order(int fd, const Command& cmd, bool is_buy) {
  Connection& c = clients_[fd];
  std::vector<Trade> trades;
  const int oid =
      book_.submit(is_buy, cmd.instrument, cmd.qty, cmd.price, fd,
                   c.session_id, &trades);
  if (oid < 0) {
    enqueue(fd, msg_error("order id exhausted"));
    return;
  }
  enqueue(fd, msg_order_accepted(oid));
  // Every trade is reported even if this trader disconnects part-way through:
  // notify_trade() checks each side's session before sending BOUGHT/SOLD and
  // still broadcasts TRADE to subscribers.
  for (const Trade& t : trades) {
    notify_trade(t);
  }
}

void Exchange::handle_cancel(int fd, const Command& cmd) {
  const Connection& c = clients_[fd];
  switch (book_.cancel(cmd.order_id, c.session_id)) {
    case OrderBook::CancelStatus::Ok:
      enqueue(fd, msg_order_cancelled(cmd.order_id));
      break;
    case OrderBook::CancelStatus::NotFound:
      enqueue(fd, msg_error("unknown order"));
      break;
    case OrderBook::CancelStatus::NotOwner:
      enqueue(fd, msg_error("order not owned"));
      break;
    case OrderBook::CancelStatus::AlreadyGone:
      enqueue(fd, msg_error("order already filled or cancelled"));
      break;
  }
}

void Exchange::handle_subscribe(int fd, const Command& cmd) {
  Connection& c = clients_[fd];
  if (c.role == Role::Trader) {
    enqueue(fd, msg_error("not a market-data client"));
    return;
  }
  c.role = Role::MarketData;
  std::uint64_t& slot = sub_session(c, cmd.instrument);
  slot = c.session_id;
  std::vector<int>& v = subs(cmd.instrument);
  bool present = false;
  for (int existing : v) {
    if (existing == fd) {
      present = true;
      break;
    }
  }
  if (!present) {
    v.push_back(fd);
  }
  enqueue(fd, msg_ok());
}

void Exchange::handle_unsubscribe(int fd, const Command& cmd) {
  Connection& c = clients_[fd];
  sub_session(c, cmd.instrument) = 0;
  enqueue(fd, msg_ok());
}

void Exchange::notify_trade(const Trade& trade) {
  if (live(trade.buy_fd, trade.buy_session)) {
    enqueue(trade.buy_fd,
            msg_bought(trade.instrument_id, trade.qty, trade.price));
  }
  if (live(trade.sell_fd, trade.sell_session)) {
    enqueue(trade.sell_fd,
            msg_sold(trade.instrument_id, trade.qty, trade.price));
  }
  broadcast_trade(trade.instrument_id, trade.qty, trade.price);
}

void Exchange::broadcast_trade(int instrument_id, int qty, int price) {
  std::vector<int>& v = subs(instrument_id);
  std::size_t i = 0;
  while (i < v.size()) {
    const int sfd = v[i];
    if (!valid_fd(sfd)) {
      v[i] = v.back();
      v.pop_back();
      continue;
    }
    Connection& c = clients_[sfd];
    const std::uint64_t want = sub_session(c, instrument_id);
    if (c.role != Role::MarketData || c.session_id == 0 ||
        c.session_id != want) {
      v[i] = v.back();
      v.pop_back();
      continue;
    }
    enqueue(sfd, msg_trade(instrument_id, qty, price));
    // enqueue() can fail and teardown(sfd), which removes sfd from this
    // vector. In that case a different subscriber was swapped into index i;
    // process it next instead of deleting or skipping it.
    if (i < v.size() && v[i] == sfd) {
      ++i;
    }
  }
}
