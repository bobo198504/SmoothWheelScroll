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

# The shared Win32 panel is compiled OUT on macOS because SWELL provides no RegisterClass /
# CreateWindowEx route there. platform/mac/panel_mac.mm supplies a native Cocoa panel instead, so
# the settings action and live persistence remain available without translating hand-drawn Win32.
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
# * The macOS branch is built by GitHub's macOS runner. REAPER runtime validation still has to happen
#   on a real Mac; a green CI job proves the SDK/link/export contract, not wheel behaviour.
#
# WHAT REAPER EXPECTS: a .dylib in ~/Library/Application Support/REAPER/UserPlugins exporting
# ReaperPluginEntry with C linkage and default visibility.
#
# WHY -fobjc-arc: the NSEvent monitor and notification observer are Objective-C tokens. ARC gives
# their globals the ownership they need; manual collection previously left the monitor token at risk
# of being autoreleased. Dispatch objects use OS_OBJECT_USE_OBJC under ARC.
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

echo "== target =="
echo "$OUT/reaper_smoothwheelscroll-mac-{arm64,x86_64}.dylib"
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
  # ★ SWELL'S MODULE STUB -- not optional, and the second thing the CI build taught us.
  #
  # Defining SWELL_PROVIDED_BY_APP makes swell-functions.h declare every SWELL call as a function
  # POINTER. Something still has to DEFINE those pointers and fill them in at load time, and this
  # file is that something: it emits the pointer definitions, then at static-init asks REAPER's app
  # delegate for `swellGetAPPAPIFunc` and assigns each one by name, substituting a harmless dummy for
  # anything the host does not offer.
  #
  # Without it the pointers are declared-and-never-defined, which is a link error naming exactly the
  # five calls this build actually makes (and it would be the same error for all of them, so the
  # count is not a hint that only five are needed).
  "$ROOT/third_party/reaper-sdk-git/WDL/swell/swell-modstub.mm"
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
# -mmacosx-version-min: Intel supports 10.13; Apple Silicon starts at 11.0. Using 10.13 for arm64
# produces a deployment target that never existed and invites linker/notarisation warnings.
build_arch() {
  local arch="$1"
  local out="$OUT/reaper_smoothwheelscroll-mac-$arch.dylib"
  local minver="10.13"
  if [ "$arch" = "arm64" ]; then
    minver="11.0"
  fi
  echo "== compiling ($arch) =="
  clang++ -std=c++17 -O2 -dynamiclib -fno-rtti -fblocks -fobjc-arc \
    -arch "$arch" -mmacosx-version-min="$minver" \
    -isysroot "$MACSDK" \
    -DNOMINMAX -DUNICODE -D_UNICODE -DSWELL_PROVIDED_BY_APP \
    -framework Cocoa \
    "${DEFS[@]}" \
    "${INCS[@]}" \
    "${SOURCES[@]}" \
    -o "$out"

  # Ad-hoc signing is enough for a local/CI plugin bundle and makes Gatekeeper diagnostics clearer.
  # It creates no developer identity and does not imply notarisation.
  codesign --force --sign - "$out"
  codesign --verify --strict "$out"

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
HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
  arm64|x86_64) ;;
  *)
    echo "unsupported Mac architecture: $HOST_ARCH" >&2
    exit 1
    ;;
esac
echo "  cp \"$OUT/reaper_smoothwheelscroll-mac-$HOST_ARCH.dylib\" \"\$HOME/Library/Application Support/REAPER/UserPlugins/\""
echo "== log =="
echo "  open -e \"\$TMPDIR/SmoothWheelScroll.log\""
