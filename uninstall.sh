#!/system/bin/sh
# A1-内存管理 [HyperOS4] uninstaller

PIDFILE=/data/local/tmp/a1guard.pid
MODDIR=/data/adb/modules/a1memory_hyperos4

if [ -f "$PIDFILE" ]; then
  kill -9 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null
fi
rm -f "$PIDFILE" /data/local/tmp/a1guard.log /data/local/tmp/a1guard.lru

if [ -f "$MODDIR/config/game.conf" ]; then
  while IFS= read -r pkg; do
    case "$pkg" in
      ""|"#"*) continue ;;
    esac
    pkg=$(echo "$pkg" | tr -d '[:space:]')
    cmd deviceidle whitelist -"$pkg" >/dev/null 2>&1
    appops reset "$pkg" >/dev/null 2>&1
  done < "$MODDIR/config/game.conf"
fi

echo "- A1-内存管理 [HyperOS4] 测试版已卸载"
