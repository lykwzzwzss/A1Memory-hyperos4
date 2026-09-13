#!/system/bin/sh
# A1-内存管理 [HyperOS4] service

MODDIR=${0%/*}
PIDFILE=/data/local/tmp/a1guard.pid

COUNT=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ "$COUNT" -lt 30 ]; do
  sleep 10
  COUNT=$((COUNT + 1))
done

start() {
  [ -x "$MODDIR/bin/a1guard" ] || return 1
  if [ -f "$PIDFILE" ]; then
    DPID=$(cat "$PIDFILE" 2>/dev/null)
    [ -n "$DPID" ] && kill -0 "$DPID" 2>/dev/null && return 0
  fi
  nohup setsid "$MODDIR/bin/a1guard" >/dev/null 2>&1 &
}

start

while true; do
  sleep 600
  start
done &
