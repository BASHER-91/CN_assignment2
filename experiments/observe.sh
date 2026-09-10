#!/bin/sh
PORT=${1:-5000}

echo "=== sockstat (port $PORT) ==="
if command -v sockstat >/dev/null 2>&1; then
  sockstat -4 -p "$PORT" 2>/dev/null || sockstat -4 | awk -v p=":$PORT" '$0 ~ p'
else
  echo "(sockstat not found — typical on Linux; use ss/netstat)"
fi

echo
echo "=== netstat TCP (grep $PORT) ==="
netstat -an -p tcp 2>/dev/null | grep "$PORT" || netstat -an | grep "$PORT"

echo
echo "=== exchange_server / trader / market_data pids ==="
pgrep -lf 'exchange_server|run-server' 2>/dev/null || true
pgrep -lf 'trader|market_data' 2>/dev/null || true
