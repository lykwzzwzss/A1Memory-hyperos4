#!/sbin/sh
#
# A1-内存管理 [HyperOS4] installer
# Copyright (C) 2026 A1 Community
#
# This program is free module: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

if [ "$ARCH" != "arm64" ]; then
  abort "- 仅支持 arm64 平台, 当前: $ARCH"
fi

SDK=$(getprop ro.build.version.sdk 2>/dev/null)
ui_print "- Android SDK: ${SDK:-unknown}"
if [ -n "$SDK" ] && [ "$SDK" -lt 35 ]; then
  ui_print "! 本模块面向 Android 15+ 新机, 低版本未充分测试"
fi

PAGE=$(getprop ro.product.cpu.pagesize.max 2>/dev/null)
[ -z "$PAGE" ] && PAGE=$(getconf PAGESIZE 2>/dev/null)
ui_print "- Page size: ${PAGE:-unknown}"

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/bin/a1guard" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

ui_print "- 安装完成, 重启后生效"
ui_print "- 配置: /data/adb/modules/a1memory_hyperos4/config/"
ui_print "- 兼容 Magisk / KernelSU / APatch"
