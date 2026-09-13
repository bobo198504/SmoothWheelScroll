#!/usr/bin/env bash
# Build SmoothWheelScroll (REAPER extension) for Windows x64.
#
# Toolchain lives in the shared _tools dir per the global convention.
set -euo pipefail

TOOLS="${TOOLS:-/d/Projects/Code/_tools}"
W64="$TOOLS/w64devkit"
export PATH="$W64/bin:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
SDK="$ROOT/third_party/reaper-sdk-git/sdk"
SRC="$ROOT/src/smooth_wheel_scroll.cpp"
OUT="$ROOT/build"
DLL="$OUT/reaper_smoothwheelscroll-x64.dll"

mkdir -p "$OUT"

DEFS=()
for arg in "$@"; do
  case "$arg" in
    # Compile-time switch for the diagnostic logging (writes to %TEMP%).
    --debug-log) DEFS+=(-DSWS_DEBUG_LOG) ;;
    # Compile the settings window OUT. The window is part of the normal build (the
    # plugin ships a settings entry); pass this only to build a headless DLL.
    --no-settings-ui) DEFS+=(-DSWS_NO_SETTINGS_UI) ;;
    *) echo "unknown build option: $arg" >&2; exit 2 ;;
  esac
done

echo "== g++ =="
g++ --version | head -1
echo "== compiling =="
g++ -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ \
  -DWIN32 -D_WIN32 -DNOMINMAX -DUNICODE -D_UNICODE \
  "${DEFS[@]}" \
  -I"$SDK" \
  "$SRC" \
  -o "$DLL" \
  -luser32 -lgdi32 -lole32 -lwinmm -lcomctl32

echo "== built =="
ls -l "$DLL"
echo "== exported entry point =="
objdump -p "$DLL" | grep -i ReaperPluginEntry || { echo "MISSING EXPORT"; exit 1; }
