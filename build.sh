#!/usr/bin/env bash
# Build SmoothWheelScroll (REAPER extension).
#
#   ./build.sh                      Windows x64, release
#   ./build.sh --debug-log          Windows x64, with the %TEMP% debug log
#   ./build.sh --wheel-log          Windows x64, DEV (research log beside the DLL)
#   ./build.sh --no-settings-ui     Windows x64, headless (no panel, settings from extstate)
#   ./build.sh --mac                macOS universal, TEST BUILD (needs a Mac or a macOS SDK)
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

mkdir -p "$OUT"

# Collect the arguments first, because --mac changes which of them even apply.
DEFS=()
DEV=0
MAC=0
NOSET_UI=0
for arg in "$@"; do
  case "$arg" in
    --debug-log) DEFS+=(-DSWS_DEBUG_LOG) ;;
    --no-settings-ui) NOSET_UI=1 ;;
    --wheel-log) DEFS+=(-DSWS_WHEEL_LOG) ; DEV=1 ;;
    --mac) MAC=1 ;;
    *) echo "unknown build option: $arg" >&2; exit 2 ;;
  esac
done

# The panel is compiled OUT on macOS. Not because it is unwanted -- it is the obvious next piece of
# work -- but because SWELL provides no window creation there (no CreateWindowEx, no RegisterClass,
# and SWELL_CreateXBridgeWindow is compiled out on OSX), so a Cocoa panel is a from-scratch UI rather
# than a translation. A build with no panel still honours every saved setting: the plugin reads them
# from REAPER's extstate, and LoadSettings() is compiled unconditionally.
if [ "$MAC" = "1" ]; then
  NOSET_UI=1
fi
if [ "$NOSET_UI" = "1" ]; then
  DEFS+=(-DSWS_NO_SETTINGS_UI)
fi

# ---------------------------------------------------------------------------
# WINDOWS
# ---------------------------------------------------------------------------
if [ "$MAC" = "0" ]; then
  if [ "$DEV" = "1" ]; then
    DLL="$OUT/reaper_smoothwheelscroll-x64-DEV.dll"
  else
    DLL="$OUT/reaper_smoothwheelscroll-x64.dll"
  fi
  echo "== target =="
  echo "$DLL"

  echo "== g++ =="
  g++ --version | head -1
  echo "== compiling =="
  # platform/win32/ holds the platform-facing pieces of the frame clock and the wheel query.
  g++ -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ \
    -DWIN32 -D_WIN32 -DNOMINMAX -DUNICODE -D_UNICODE \
    "${DEFS[@]}" \
    -I"$SDK" -I"$ROOT/src" \
    "$SRC" \
    "$ROOT/src/platform/win32/time_win32.cpp" \
    "$ROOT/src/platform/win32/wheel_win32.cpp" \
    -o "$DLL" \
    -luser32 -lgdi32 -lwinmm

  echo "== built =="
  ls -l "$DLL"
  echo "== exported entry point =="
  objdump -p "$DLL" | grep -i ReaperPluginEntry || { echo "MISSING EXPORT"; exit 1; }
  exit 0
fi

# ---------------------------------------------------------------------------
# MACOS
#
# * THIS BRANCH HAS NEVER BEEN RUN. The Windows development machine has no macOS SDK; the Mac-side
# sources were checked with Zig's clang for a macOS TARGET, which proved they parse and typecheck,
# but no .dylib has been produced or loaded. Treat every line below as a starting point.
#
# WHAT REAPER EXPECTS: a .dylib in ~/Library/Application Support/REAPER/UserPlugins exporting
# ReaperPluginEntry with C linkage and default visibility.
#
# WHY -fno-rtti AND NOT -fobjc-arc: nothing here owns an Objective-C object with a lifetime -- the
# tap and the timer are CoreFoundation / dispatch, managed by CFRelease and dispatch_suspend -- so
# ARC would add runtime dependencies for nothing.
#
# WHY MACSDK IS A VARIABLE: `xcrun --show-sdk-path` is how a Mac finds it. A cross build elsewhere
# must set it by hand; the macOS SDK cannot legally be redistributed, so this script bundles none.
# ---------------------------------------------------------------------------
if command -v xcrun >/dev/null 2>&1; then
  MACSDK="$(xcrun --show-sdk-path)"
else
  MACSDK="${MACSDK:-}"
fi
if [ -z "${MACSDK:-}" ] || [ ! -d "$MACSDK" ]; then
  echo "macOS SDK not found." >&2
  echo "  On a Mac this is automatic (xcrun --show-sdk-path)." >&2
  echo "  Cross-compiling: set MACSDK=<path to MacOSX*.sdk>." >&2
  exit 1
fi

DYLIB="$OUT/reaper_smoothwheelscroll-mac.dylib"
echo "== target =="
echo "$DYLIB"
echo "== sdk =="
echo "$MACSDK"
echo "== clang =="
clang++ --version | head -1

SOURCES=(
  "$SRC"
  "$ROOT/src/platform/mac/entry_mac.mm"
  "$ROOT/src/platform/mac/time_mac.mm"
  "$ROOT/src/platform/mac/wheel_mac.mm"
  "$ROOT/src/platform/mac/panel_mac.mm"
  "$ROOT/src/platform/mac/win32_compat.mm"
)

# TWO include roots are needed: the SDK's sdk/ dir (reaper_plugin.h) and the SDK ROOT, because
# reaper_plugin.h pulls in "../WDL/swell/swell.h" and that WDL now ships beside sdk/ in third_party.
INCS=(-I"$SDK" -I"$ROOT/third_party/reaper-sdk-git" -I"$ROOT/src")

# ONE FILE PER ARCHITECTURE, not a universal binary.
#
# A universal (lipo-joined) dylib would also work -- REAPER loads either -- but 1.8 ships FOUR
# artifacts, one per platform and architecture, so that a tester downloads the file that matches
# their machine and a crash report names it unambiguously. Joining them would collapse that back
# into two files and make "which build reported this?" unanswerable.
#
# ★ SWELL_PROVIDED_BY_APP IS REQUIRED, and its absence was the first thing the CI build caught.
#
# On Windows every Win32 call is a real import from user32/gdi32. On macOS THERE IS NOTHING TO
# LINK AGAINST: REAPER itself is the implementation of SWELL and hands the function pointers to the
# plugin at load time. swell-functions.h switches on exactly this:
#
#     #if defined(SWELL_PROVIDED_BY_APP) || defined(SWELL_LOAD_SWELL_DYLIB)
#       #define SWELL_API_DEFINE(ret,func,parms) extern "C" ret (*func)parms;   // pointer
#     #else
#       #define SWELL_API_DEFINE(ret,func,parms) ret func parms ;                // needs a symbol
#     #endif
#
# Without it, SendMessage / GetClientRect / GetWindowLong / GetTextMetrics / SWELL_DrawText are
# declared as ordinary functions and the link fails with "undefined symbol". That is not a bug in
# the plugin: it is the definition that tells SWELL who owns those functions.
#
# -mmacosx-version-min=10.13: the oldest whose SDK still has every API used here (the event tap and
# dispatch are far older).
build_arch() {
  local arch="$1"
  local out="$OUT/reaper_smoothwheelscroll-mac-$arch.dylib"
  echo "== compiling ($arch) =="
  clang++ -std=c++17 -O2 -dynamiclib -fno-rtti \
    -arch "$arch" -mmacosx-version-min=10.13 \
    -isysroot "$MACSDK" \
    -DNOMINMAX -DUNICODE -D_UNICODE -DSWELL_PROVIDED_BY_APP \
    -framework CoreFoundation -framework ApplicationServices \
    "${DEFS[@]}" \
    "${INCS[@]}" \
    "${SOURCES[@]}" \
    -o "$out"

  echo "== built: $out =="
  ls -l "$out"
  echo "== architecture =="
  lipo -info "$out"
  echo "== exported entry point =="
  nm -gU "$out" | grep -i _ReaperPluginEntry || { echo "MISSING EXPORT"; exit 1; }
}

build_arch arm64
build_arch x86_64

echo ""
echo "== install =="
echo "  cp \"$DYLIB\" \"\$HOME/Library/Application Support/REAPER/UserPlugins/\""
echo "== log =="
echo "  open -e \"\$TMPDIR/SmoothWheelScroll.log\""
