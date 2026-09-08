# Assignment 2: The Socket Exchange

Simplified TCP trading system: an Exchange Server, Trader Clients, and Market-Data Clients. All networking uses the POSIX socket API (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, `shutdown`, `close`) plus `kqueue` on the server. There are no high-level networking libraries.

Further documentation:

- [EXPERIMENTS.md](EXPERIMENTS.md) — how to run Experiments 1–8 and the 70k bonus, including helper scripts in `experiments/`
- [ARCHITECTURE.md](ARCHITECTURE.md) — I/O model, order book, framing, and design trade-offs (for `report.pdf`)

## Makefile location

The Makefile is located in the `src/` directory. To build, run:

```
cd src && make
```

This produces `src/exchange_server`, `src/trader`, and `src/market_data`. `cd src && make clean` removes objects and binaries.

The launcher scripts build automatically if a binary is missing.

## Running

From the repository root (also the interface `experiment.py` uses):

```
./server/run-server 127.0.0.1 5000
./client/run-trader 127.0.0.1 5000 alice
./client/run-market-data 127.0.0.1 5000 JNST
```

Defaults if the server is started with no arguments: bind `127.0.0.1:9000`.

A single extra argument to the server is treated as a port (`./server/run-server 5000`).

Trader stdin (LOGIN is sent on connect): `BUY`, `SELL`, `CANCEL`, `QUIT`.

Market-data stdin: `SUBSCRIBE`, `UNSUBSCRIBE`, `QUIT`. If a third launcher argument is given (`JNST` or `IMCT`), `SUBSCRIBE` is sent immediately after connect.

## Configuration / defensive design

While not mandated by the spec, a **64 KiB** read buffer limit per connection is enforced to prevent memory exhaustion from malicious/unterminated streams. A connection that exceeds the cap is dropped and its `in_buf` / `out_buf` storage is released.

## Implementation notes (for the experiment report)

**I/O:** The server is a single-threaded, **level-triggered `kqueue`** loop. Each `EVFILT_READ` does one `recv()`. Outbound data is written with non-blocking `send()` (including short counts). Leftover bytes arm `EVFILT_WRITE`; an empty buffer deletes that filter so a writable socket does not busy-spin.

This is the right scaling story for the 70k idle-connection bonus:

- `select()` is limited by `FD_SETSIZE` (often 1024) and scans the whole fd set every time.
- Thread-per-connection needs a stack per client (on the order of megabytes × 70,000).
- `kqueue` waits only on ready fds and keeps per-client state in a flat `clients[fd]` array.

**SIGPIPE:** `signal(SIGPIPE, SIG_IGN)` in every process, plus `SO_NOSIGPIPE` on sockets. FreeBSD does not reliably support `MSG_NOSIGNAL`. A reset while writing becomes `EPIPE` / `ECONNRESET` and tears down that connection only.

**Orders after disconnect:** Resting orders stay in the book. Later fills still emit `TRADE` to subscribers; `BOUGHT` / `SOLD` are skipped if the owner `session_id` no longer matches.

**Matching:** Exact price, FIFO at that price, partial fills, self-match allowed. `CANCEL` is lazy (`remaining_qty = 0`); the match loop pops dead deque fronts.

## 70,000 idle connections

Raise process and system fd limits before the bonus experiment, for example on FreeBSD:

```
ulimit -n 100000
# may also need:
# sysctl kern.maxfiles=200000
# sysctl kern.maxfilesperproc=100000
```

Then connect many idle clients to `./server/run-server 127.0.0.1 5000`.
