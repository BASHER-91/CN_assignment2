# Running the experiments

This file is the runbook for Section 6 of the handout. The C++ server and clients are the system under test; the experiments themselves use FreeBSD tools (`sockstat`, `netstat`, `tcpdump`, `procstat`) plus the helper scripts in [`experiments/`](experiments/).

Course-provided `experiment.py` (from Moodle) is **not** in this repository. Place it in the repo root if you have it. You can still complete every experiment with the procedures below.

## 0. One-time setup

Work **inside the FreeBSD VM**. Clients and server must be separate processes on that VM (loopback is fine).

```sh
cd /path/to/CN_assignment2
cd src && make
cd ..
chmod +x server/run-server client/run-trader client/run-market-data experiments/*.py experiments/*.sh
```

Install capture tools if they are missing:

```sh
pkg install -y tcpdump
```

Useful binaries already on FreeBSD: `sockstat`, `netstat`, `procstat`, `ps`, `ktrace`.

Default bind used in the examples:

```sh
./server/run-server 127.0.0.1 5000
```

Launchers:

| Script | Typical invocation |
| :--- | :--- |
| `server/run-server` | `./server/run-server 127.0.0.1 5000` |
| `client/run-trader` | `./client/run-trader 127.0.0.1 5000 alice` |
| `client/run-market-data` | `./client/run-market-data 127.0.0.1 5000 JNST` |

Helper scripts (this repo):

| Script | Role |
| :--- | :--- |
| `experiments/observe.sh` | Print listening/connected sockets and TCP states for a port |
| `experiments/helpers.py partial-send` | Send a protocol line in several `send()` calls (Exp 3) |
| `experiments/helpers.py hang` | Connect and send nothing (Exp 4) |
| `experiments/helpers.py rst` | Abrupt close with `SO_LINGER 0` → RST (Exp 6 / 8) |
| `experiments/helpers.py fin` | Orderly `shutdown`/`close` → FIN (Exp 6) |
| `experiments/helpers.py slow-md` | Subscribe, then stop reading (Exp 7) |
| `experiments/helpers.py trade-flood` | Rapid matching orders to generate `TRADE`/`BOUGHT` traffic (Exp 7–8) |
| `experiments/helpers.py idle` | Hold N idle TCP connections (bonus) |

If Moodle’s script is present:

```sh
python3 experiment.py <experiment_number>
```

It is expected to spawn `./server/run-server`, `./client/run-trader`, and `./client/run-market-data` with a host and port (for example `127.0.0.1 5000`). Keep a second terminal open for `sockstat` / `tcpdump` while it runs.

**Report:** for each experiment, save the terminal screenshot(s), the exact commands, a short description of the approach, and the answer to the handout question. Put everything in `report.pdf`.

---

## Experiment 1 — Listening vs connected sockets

**Question:** After a client has connected, identify the TCP sockets on the Exchange Server. How does the listening socket differ from the socket for that client?

1. Terminal A: `./server/run-server 127.0.0.1 5000`
2. Note the server PID (print from `sockstat` or `pgrep exchange_server`).
3. Terminal B: `./client/run-trader 127.0.0.1 5000 alice`
4. Terminal C:

```sh
./experiments/observe.sh 5000
# or:
sockstat -4 -p 5000
netstat -an -p tcp | grep 5000
```

**What to look for**

- One socket in `LISTEN` on `127.0.0.1:5000` (the `listen()` fd).
- One (or more) `ESTABLISHED` socket(s) with local address `127.0.0.1:<ephemeral>` and the same server PID.

The listening socket only accepts new connections. Each accepted client is a different fd with its own 4-tuple.

---

## Experiment 2 — TCP connection states

**Question:** How does the TCP state of the client–server connection change, and what events cause those changes?

1. Start the server. In another terminal run `netstat` in a loop:

```sh
while true; do date; netstat -an -p tcp | grep 5000; echo; sleep 1; done
```

2. Start a trader, type a `BUY`, then `QUIT` (orderly).
3. Repeat with `experiments/helpers.py rst 127.0.0.1 5000` for an abortive close.

**States you should be able to name:** `LISTEN` (server only) → `ESTABLISHED` after handshake → `FIN_WAIT_*` / `TIME_WAIT` / `CLOSE_WAIT` / `LAST_ACK` on orderly close, versus a fast drop to `CLOSED` (often via `RST`) on abort. Screenshot the `netstat` snapshots next to the action that caused each change.

---

## Experiment 3 — TCP as a byte stream

**Question:** Does the server receive an application message as one unit, or can it arrive in pieces?

1. Start the server.
2. Optional: `tcpdump -i lo0 -n -X tcp port 5000`
3. Run:

```sh
python3 experiments/helpers.py partial-send 127.0.0.1 5000 'LOGIN alice'
```

This issues several small `send()` calls for one `\n`-terminated line. The server should still reply `OK`.

That is message framing: TCP has no application record boundary; `\n` is reconstructed in `in_buf`.

---

## Experiment 4 — One idle client must not stall others

**Question:** If Client 1 stays connected and sends no data, can the server still accept and service Client 2? Which server operation decides that?

1. Start the server.
2. Terminal B (idle peer):

```sh
python3 experiments/helpers.py hang 127.0.0.1 5000
```

3. Terminal C: `./client/run-trader 127.0.0.1 5000 bob` and send `BUY JNST 1 1`.

Client 2 must get `ORDER_ACCEPTED`. The server is in `kevent()` (level-triggered `kqueue`), not blocked in `recv()` on Client 1.

---

## Experiment 5 — I/O multiplexing (optional)

**Question:** At a given moment, which connections are actually ready, and what evidence shows that?

1. Start the server, one hanging client, one active trader typing commands, one market-data client.
2. `sockstat -4 -p 5000` shows all established fds; it does **not** show kqueue readiness.
3. Evidence that only ready fds are serviced: Client 1 idle, Client 2 still gets replies (same as Exp 4). Optional: `ktrace -p <server-pid>` / `kdump` and look for `kevent` returning a small `nready`, not a scan of every fd.

`select()` would scan a bitmap; this server waits on the kqueue, which reports only triggered filters.

---

## Experiment 6 — FIN vs RST

**Question:** How does abrupt termination differ from orderly shutdown? What TCP event is on the wire, and what happens to the server socket?

**Orderly (FIN):**

```sh
python3 experiments/helpers.py fin 127.0.0.1 5000
# or type QUIT in a trader
```

`tcpdump -i lo0 -n tcp port 5000` should show `Flags [F.]`. The server `recv()` returns 0 (or `EV_EOF`); it drains any complete lines, then `close()`s that fd. Other clients keep running.

**Abrupt (RST):**

```sh
python3 experiments/helpers.py rst 127.0.0.1 5000
```

`tcpdump` should show `Flags [R.]`. The server sees `ECONNRESET` / `EV_EOF` and tears down only that connection.

Screenshot both `tcpdump` traces and `observe.sh` before/after.

---

## Experiment 7 — Backpressure / slow receiver

**Question:** What happens on the TCP connection to a client that stops reading? Does that stall the server for everyone else?

1. Start the server.
2. Slow market-data client (subscribes to `JNST`, then never `recv`s):

```sh
python3 experiments/helpers.py slow-md 127.0.0.1 5000 JNST
```

3. Flood matching trades (another terminal):

```sh
python3 experiments/helpers.py trade-flood 127.0.0.1 5000 flooduser 2000
```

4. Watch the slow socket’s send queue and state:

```sh
netstat -an -p tcp | grep 5000
./experiments/observe.sh 5000
```

5. In a **third** terminal, log in a normal trader. It should still get `OK` / `ORDER_ACCEPTED`.

The server uses non-blocking `send()` and a per-connection `out_buf`. If the kernel send buffer fills, that socket gets `EAGAIN` and `EVFILT_WRITE`; the event loop continues to service other fds. Screenshot `netstat` `Send-Q` on the slow connection plus a live trader still working.

---

## Experiment 8 — Unexpected disconnect while the server is writing

**Question:** If a client vanishes while the server is sending to it, what happens to the TCP connection, and how does the server notice?

1. Start the server and `slow-md` (or a normal MD client).
2. Start `tcpdump -i lo0 -n tcp port 5000`.
3. Start `trade-flood` so the server is writing `TRADE` lines.
4. Kill the receiver: Ctrl-C on `slow-md`, or:

```sh
python3 experiments/helpers.py rst 127.0.0.1 5000
```

(Use a dedicated MD client you then RST if you want a clean single-connection capture.)

**Expected:** RST or unread-FIN on the wire. The next `send()`/`recv()` fails with `EPIPE`/`ECONNRESET`. The process does **not** die: `SIGPIPE` is ignored and `SO_NOSIGPIPE` is set. Only that client is removed from `clients[fd]`.

---

## Bonus — 70,000 idle connections

Raise limits **before** starting the server (FreeBSD; values are examples):

```sh
ulimit -n 100000
# as root, if accept() fails with EMFILE:
# sysctl kern.maxfiles=200000
# sysctl kern.maxfilesperproc=100000
```

```sh
./server/run-server 127.0.0.1 5000
python3 experiments/helpers.py idle 127.0.0.1 5000 10000
# repeat / increase --count through 70000
```

Measure at each step (replace `PID` with the `exchange_server` pid):

```sh
ps -o pid,rss,%cpu -p PID
procstat -f PID | wc -l
sysctl kern.openfiles
netstat -m
sockstat -4 -p 5000 | wc -l
```

Fill the handout table (RSS, CPU, server fds, system fds, socket-buffer stats, connections actually established). Then answer the five bonus questions. For this codebase, contrast `kqueue` with `select()` (`FD_SETSIZE`) and thread-per-connection (stack memory) in `report.pdf` — details are in [`ARCHITECTURE.md`](ARCHITECTURE.md).

---

## Screenshot checklist

For every required experiment: tool output (`sockstat` / `netstat` / `tcpdump` / client+server terminals) plus the written answer. Optional Exp 5 and the bonus table if you attempt them.
