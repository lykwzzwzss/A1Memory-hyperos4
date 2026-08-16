#!/usr/bin/env bash
#
# Build a1guard for arm64 with 16KB ELF alignment (Android 15/16/17 new devices)
#
# Requirements: curl + unzip (NDK is downloaded automatically on first run)
# Usage:
#   scripts/build.sh              # build bin/a1guard
#   scripts/build.sh package      # build and create module zip in build/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK_VERSION="${NDK_VERSION:-r27c}"
NDK_DIR="${NDK_DIR:-$ROOT/.ndk}"
API="${API:-35}"
PAGE_SIZE=16384
OUT="$ROOT/bin/a1guard"

TOOLCHAIN="$NDK_DIR/android-ndk-$NDK_VERSION/toolchains/llvm/prebuilt/linux-x86_64/bin"
CC="$TOOLCHAIN/aarch64-linux-android${API}-clang"
READELF="$TOOLCHAIN/llvm-readelf"

if [ ! -x "$CC" ]; then
  mkdir -p "$NDK_DIR"
  ZIP="$NDK_DIR/android-ndk-$NDK_VERSION-linux.zip"
  if [ ! -f "$ZIP" ]; then
    echo "Downloading Android NDK $NDK_VERSION ..."
    curl -fL --retry 3 -o "$ZIP" \
      "https://dl.google.com/android/repository/android-ndk-$NDK_VERSION-linux.zip"
  fi
  echo "Extracting NDK ..."
  unzip -q "$ZIP" -d "$NDK_DIR"
fi

echo "Compiling (API $API, $PAGE_SIZE-byte page alignment) ..."
mkdir -p "$(dirname "$OUT")"
"$CC" -std=gnu11 -O2 -fomit-frame-pointer -ffunction-sections -fdata-sections \
  -Wl,--gc-sections -Wl,-z,max-page-size=$PAGE_SIZE -Wl,-z,common-page-size=$PAGE_SIZE \
  -s -o "$OUT" "$ROOT/src/a1guard.c"

echo "Verifying ELF alignment ..."
"$READELF" -l "$OUT" | awk '/LOAD/{print "  " $0}'
ALIGN=$("$READELF" -l "$OUT" | awk '/LOAD/{print $NF; exit}')
if [ "$ALIGN" != "0x4000" ]; then
  echo "ERROR: LOAD segment alignment is $ALIGN, expected 0x4000 (16KB)" >&2
  exit 1
fi

echo "OK: $OUT"

if [ "${1:-}" = "package" ]; then
  mkdir -p "$ROOT/build"
  ZOUT="$ROOT/build/A1Memory-hyperos4-$(date +%Y%m%d).zip"
  echo "Packaging $ZOUT ..."
  (cd "$ROOT" && zip -r -9 "$ZOUT" \
     module.prop customize.sh service.sh uninstall.sh sepolicy.rule config bin)
  echo "OK: $ZOUT"
fi
