#!/system/bin/sh
#
# A1-内存管理 [HyperOS4] service
# Copyright (C) 2026 A1 Community
#
# This program is free module: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

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
