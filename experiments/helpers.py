#!/usr/bin/env python3
"""Helper clients for the handout experiments. Not used by the C++ server.

Usage:
  python3 experiments/helpers.py partial-send HOST PORT LINE
  python3 experiments/helpers.py hang HOST PORT
  python3 experiments/helpers.py fin HOST PORT
  python3 experiments/helpers.py rst HOST PORT
  python3 experiments/helpers.py slow-md HOST PORT [INSTRUMENT]
  python3 experiments/helpers.py trade-flood HOST PORT USERNAME [N]
  python3 experiments/helpers.py idle HOST PORT N
"""

from __future__ import print_function

import argparse
import socket
import struct
import sys
import time


def connect(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    return s


def recv_some(sock, timeout=1.0):
    sock.settimeout(timeout)
    try:
        data = sock.recv(4096)
        return data.decode("utf-8", "replace")
    except socket.timeout:
        return ""
    except OSError:
        return ""


def cmd_partial_send(args):
    line = args.line
    if not line.endswith("\n"):
        line += "\n"
    payload = line.encode("utf-8")
    sock = connect(args.host, args.port)
    step = max(1, args.chunk)
    for i in range(0, len(payload), step):
        sock.sendall(payload[i : i + step])
        time.sleep(args.delay)
    sys.stdout.write(recv_some(sock, timeout=2.0))
    sock.close()


def cmd_hang(args):
    sock = connect(args.host, args.port)
    print("connected fd held idle; Ctrl-C to exit", file=sys.stderr)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    sock.close()


def cmd_fin(args):
    sock = connect(args.host, args.port)
    sock.sendall(b"QUIT\n")
    sock.shutdown(socket.SHUT_WR)
    sys.stdout.write(recv_some(sock, timeout=1.0))
    sock.close()


def cmd_rst(args):
    sock = connect(args.host, args.port)
    sock.setsockopt(
        socket.SOL_SOCKET,
        socket.SO_LINGER,
        struct.pack("ii", 1, 0),
    )
    sock.close()
    print("closed with SO_LINGER=0 (RST)", file=sys.stderr)


def cmd_slow_md(args):
    inst = args.instrument
    sock = connect(args.host, args.port)
    sock.sendall(("SUBSCRIBE %s\n" % inst).encode("utf-8"))
    sock.settimeout(2.0)
    try:
        print(recv_some(sock, timeout=2.0), end="")
    except OSError:
        pass
    print("subscribed; not reading further. Ctrl-C to exit", file=sys.stderr)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    sock.close()


def cmd_trade_flood(args):
    sock = connect(args.host, args.port)
    sock.sendall(("LOGIN %s\n" % args.username).encode("utf-8"))
    sys.stdout.write(recv_some(sock, timeout=2.0))
    n = args.count
    for i in range(n):
        sock.sendall(b"BUY JNST 1 7\nSELL JNST 1 7\n")
        if i % 100 == 99:
            time.sleep(0.01)
    time.sleep(0.2)
    sock.settimeout(0.5)
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            sys.stdout.write(chunk.decode("utf-8", "replace"))
    except (socket.timeout, OSError):
        pass
    sock.sendall(b"QUIT\n")
    sock.close()


def cmd_idle(args):
    n = args.count
    socks = []
    print("opening %d idle connections to %s:%d" % (n, args.host, args.port), file=sys.stderr)
    try:
        for i in range(n):
            s = connect(args.host, args.port)
            socks.append(s)
            if (i + 1) % 1000 == 0:
                print("  %d connected" % (i + 1), file=sys.stderr)
        print("holding %d connections. Ctrl-C to drop them." % len(socks), file=sys.stderr)
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    except OSError as exc:
        print("stopped after %d connections: %s" % (len(socks), exc), file=sys.stderr)
        try:
            while True:
                time.sleep(3600)
        except KeyboardInterrupt:
            pass
    for s in socks:
        try:
            s.close()
        except OSError:
            pass


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd")

    def add_hp(sp):
        sp.add_argument("host")
        sp.add_argument("port", type=int)

    sp = sub.add_parser("partial-send")
    add_hp(sp)
    sp.add_argument("line")
    sp.add_argument("--chunk", type=int, default=1)
    sp.add_argument("--delay", type=float, default=0.05)
    sp.set_defaults(func=cmd_partial_send)

    sp = sub.add_parser("hang")
    add_hp(sp)
    sp.set_defaults(func=cmd_hang)

    sp = sub.add_parser("fin")
    add_hp(sp)
    sp.set_defaults(func=cmd_fin)

    sp = sub.add_parser("rst")
    add_hp(sp)
    sp.set_defaults(func=cmd_rst)

    sp = sub.add_parser("slow-md")
    add_hp(sp)
    sp.add_argument("instrument", nargs="?", default="JNST")
    sp.set_defaults(func=cmd_slow_md)

    sp = sub.add_parser("trade-flood")
    add_hp(sp)
    sp.add_argument("username")
    sp.add_argument("count", nargs="?", type=int, default=1000)
    sp.set_defaults(func=cmd_trade_flood)

    sp = sub.add_parser("idle")
    add_hp(sp)
    sp.add_argument("count", type=int)
    sp.set_defaults(func=cmd_idle)

    args = p.parse_args()
    if not getattr(args, "cmd", None):
        p.print_help()
        sys.exit(2)
    args.func(args)


if __name__ == "__main__":
    main()
