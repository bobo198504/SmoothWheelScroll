#!/usr/bin/env bash
# Deploy the built extension into the REAPER portable install's UserPlugins dir.
#
# This is the ONLY place we touch inside the REAPER install, as agreed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
REAPER_DIR="${REAPER_DIR:-/d/REAPER}"
DEST="$REAPER_DIR/UserPlugins/reaper_smoothwheelscroll-x64.dll"
SRC="$ROOT/build/reaper_smoothwheelscroll-x64.dll"

if [[ ! -f "$SRC" ]]; then
  echo "not built yet: $SRC" >&2
  exit 1
fi

# Refuse to deploy while REAPER has the DLL loaded (would fail to overwrite).
if tasklist 2>/dev/null | grep -qi '^reaper\.exe'; then
  echo "REAPER is running; close it before deploying (DLL is locked)." >&2
  exit 1
fi

mkdir -p "$REAPER_DIR/UserPlugins"
cp -f "$SRC" "$DEST"
echo "deployed -> $DEST"
ls -l "$DEST"
