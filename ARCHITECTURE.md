# Architecture and design choices

This document records how the Exchange Server and clients are structured, and why those choices were made. It is meant to feed the “Implementation Decisions” section of `report.pdf`.

## 1. System overview

Three process types talk over TCP. Clients never talk to each other.

```
Trader  --LOGIN/BUY/SELL/CANCEL/QUIT-->  Exchange Server
                                     <-- OK / ERROR / ORDER_* / BOUGHT / SOLD

Market-Data --SUBSCRIBE/UNSUBSCRIBE/QUIT-->  Exchange Server
                                         <-- OK / ERROR / TRADE
```

The server listens on IPv4 (`127.0.0.1` in the usual lab setup), accepts many connections, and keeps each connection until `QUIT`, FIN, or a hard error.

Role is **not** known at `accept()`. A connection starts as `Unknown`. `LOGIN` makes it a Trader; `SUBSCRIBE` makes it Market-Data. `BUY`/`SELL`/`CANCEL` before login return `ERROR Not logged in`. `QUIT` always closes the socket.

## 2. Source layout

| File | Responsibility |
| :--- | :--- |
| `src/protocol.cpp` | Parse one `\n`-delimited line; format replies; qty/price/id ranges |
| `src/net.cpp` | POSIX `socket`/`bind`/`listen`/`accept`/`connect`, non-blocking, `SO_REUSEADDR`, `SO_NOSIGPIPE`, `SIGPIPE` ignore |
| `src/order_book.cpp` | Global `by_id`, per-instrument price deques, exact-price match, lazy cancel |
| `src/exchange.cpp` | Connection table, command dispatch, write buffers, `TRADE` fan-out |
| `src/server.cpp` | `kqueue` event loop, `accept` loop |
| `src/trader.cpp` / `src/md_client.cpp` | Interactive clients; `poll` on stdin + socket |
| `src/client_io.cpp` | Shared client send/recv line loop |
| `server/run-server`, `client/run-*` | Graders / `experiment.py` entry points |

Language: C++17. Networking: POSIX sockets only (no Boost.Asio, libevent, asyncio).

## 3. I/O model: level-triggered kqueue

**Choice:** one thread, non-blocking fds, **level-triggered** `kqueue` (`EV_ADD` without `EV_CLEAR` or `EV_ONESHOT` on reads).

**Read path:** on an ordinary `EVFILT_READ`, call `recv()` once, append to `in_buf`, and split on `\n`. If more data remains, kqueue fires again. When `EV_EOF` is present, drain `recv()` through `0`/`EAGAIN` before closing so a final batch larger than 4096 bytes is not lost.

**Write path:** append the reply to `out_buf` and `send()` immediately. `send()` may copy only a prefix (short write) or fail with `EAGAIN`. Leftover bytes stay in `out_buf` and we `EV_ADD` `EVFILT_WRITE`. When the buffer is empty we `EV_DELETE` the write filter so a writable socket does not busy-spin.

**Why not blocking `recv()` in a single thread?** Experiment 4: an idle client would stall `accept()` and every other client.

**Why not thread-per-connection?** Allowed by the handout, but each thread has a large stack. Tens of thousands of idle connections (bonus) would exhaust memory long before kqueue does. Matching and `TRADE` fan-out would also need locks.

**Why not `select()`?** `FD_SETSIZE` is often 1024. The bonus target is 70k fds. `select()` also scans the whole set every wait; `kqueue` returns only ready events.

**Why not edge-triggered `EV_CLEAR`?** Correct ET requires draining `recv()` until `EAGAIN` and one-shot writes. LT is enough for correctness and for the experiments, and it is simpler to explain in a viva.

**Cost of LT on the listening socket:** an unaccepted connection is reported on every `kevent()`. So a bare `break` on `EMFILE` would spin a core. The server keeps a reserved descriptor open, and on descriptor exhaustion it closes that descriptor, `accept()`s and immediately `close()`s the queued connections to clear the backlog, then reopens the reserve. This is the difference between graceful degradation and a pegged CPU in the 70k table.

## 4. Connection table and session ids

Connections live in a flat vector, **indexed by file descriptor**:

```cpp
std::vector<Connection> clients(100000);  // clients[fd]
```

`session_id` is a monotonic `uint64_t` (starting at 1) assigned on `accept()`. Slot `session_id == 0` is empty.

The kernel reuses fds after `close()`. A new occupant of `clients[fd]` gets a **new** `session_id`. Orders and market-data subscriptions store that id. A reconnecting username does not inherit old orders or `BOUGHT`/`SOLD` (clarification on Piazza). Notifications are sent only if `clients[owner_fd].session_id` still matches.

On teardown we remove this `fd` from both market-data subscriber lists (every occurrence), then `close(fd)` and clear `in_buf`/`out_buf` (including `shrink_to_fit()` after an oversize read) so a 64 KiB garbage stream does not leak. Price deques are still not scanned on disconnect. `UNSUBSCRIBE` still lazy-clears `sub_session_*`; the next `TRADE` swap-and-pops that slot. kqueue events carry `session_id` in `udata` so a recycled fd does not consume a stale READ/WRITE from the previous occupant.

## 5. Framing and protocol

TCP is a byte stream. Application messages end with `\n`. The server uses `std::string in_buf`, `find('\n')`, and `erase` — no ring buffer, no `regex`. Messages are small; the shift cost is irrelevant next to syscalls.

**Defensive cap:** `in_buf` is limited to **64 KiB** and `out_buf` to **4 MiB**. The spec does not require either limit; unterminated streams and non-reading peers are out of scope. The caps exist so one client cannot grow RSS without bound at 70k connections. The outbound limit is far larger than a kernel send buffer, so Experiment 7 still shows `Send-Q` filling before the peer is dropped.

Numeric rules from the handout: quantity and price in `1..2147483647`; order ids in `0..2147483647`. Values outside those ranges yield `ERROR` and do not enter the book.

## 6. Order book

Matching is **exact price only** (same instrument, opposite side, identical integer price). There is no best-price walk, so the book is not a sorted `std::map`.

```
by_id:           unordered_map<order_id, Order>     // CANCEL has no instrument
buys[2][price]:  deque of order ids                 // 0=JNST, 1=IMCT
sells[2][price]: deque of order ids
```

FIFO at a price (oldest resting order first). One incoming order can produce several trades; each trade emits its own `BOUGHT`/`SOLD` pair and `TRADE`. Self-match is allowed. `ORDER_ACCEPTED` is sent **before** matching.

**Lazy cancel:** `CANCEL` sets `remaining_qty = 0` in O(1). It never erases the middle of a deque. The match loop pops fronts with `remaining_qty == 0`. Validation: order exists, `session_id` owns it, qty still `> 0`; otherwise `ERROR`.

Disconnect does not cancel resting orders. Later fills still `TRADE`; private `BOUGHT`/`SOLD` are skipped if the owner session is gone.

## 7. Market-data fan-out

Subscribers are `std::vector<int>` of fds (`subs_jnst`, `subs_imct`), not a scan of all 100k slots.

- **De-dup:** `SUBSCRIBE` sets `sub_session_*` and `push_back`s `fd` only if it is not already in that instrument’s vector (covers `UNSUBSCRIBE` then `SUBSCRIBE` with no intervening `TRADE`).
- **Disconnect:** teardown erases this `fd` from both lists so a recycled fd cannot pair with a leftover entry and double-send `TRADE`.
- **Lazy delete:** `UNSUBSCRIBE` only clears `sub_session_*`. On each `TRADE`, a slot is valid only if `role == MarketData` and `session_id == sub_session_*`. Otherwise swap-and-pop.

`UNSUBSCRIBE` clears `sub_session_*` and replies `OK`; the next trade drops the stale fd.

## 8. SIGPIPE and teardown

FreeBSD does not reliably support `MSG_NOSIGNAL`. An unhandled `SIGPIPE` on `send()` after a peer RST **kills the whole server** (Experiment 8).

Every process calls `signal(SIGPIPE, SIG_IGN)`. Every socket gets `SO_NOSIGPIPE`. `EPIPE` / `ECONNRESET` tear down that connection only.

**FIN vs unread data (Experiment 6):** `EV_EOF` does not mean the receive buffer is empty. The handler drains `recv()`, parses complete lines, and then closes after pending output has flushed. `QUIT` likewise stops command processing but preserves replies already queued before the socket closes.

## 9. Clients

Traders send `LOGIN <username>` immediately after `connect()`. Market-data clients send `SUBSCRIBE <instrument>` when that argument is present (`./client/run-market-data 127.0.0.1 5000 JNST`).

Both switch the connected socket to non-blocking mode and use `poll()` on stdin plus socket `POLLIN`/`POLLOUT`. User commands are buffered, so a blocked send cannot delay asynchronous `BOUGHT`/`SOLD`/`TRADE`. Stdin EOF or `QUIT` queues `QUIT`, flushes it, then issues an orderly `shutdown(SHUT_WR)`.

## 10. Trade-offs (for the bonus write-up)

| Design | Idle 70k | Failure mode |
| :--- | :--- | :--- |
| Thread per client | Poor | ~MB stack × 70k; many kernel threads |
| `select()` | Poor | `FD_SETSIZE`; O(n) scan |
| Level-triggered `kqueue` + `clients[fd]` | Intended path | First wall is usually `ulimit` / `kern.maxfiles` / socket-buffer memory, not the event API |

Remaining cost at scale is **kernel socket memory** (2× buffers per fd) and fd table size, not the userspace event loop. Switching to edge-triggered kqueue would not fix that bottleneck; it would only reduce wakeup rate under heavy traffic.
