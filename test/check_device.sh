#!/usr/bin/env bash
# Gate: the DEVICE classifier separates the three senders.
#
# Compiles _diag/device_probe.cpp against the real src/device.h and checks the rule the plugin
# uses to decide which wheels it animates (notched / free-spinning) and which it leaves to REAPER
# (touchpad).
#
# NOTE: the VALUE patterns in the probe are representative, not measured from real hardware. This
# gate proves the RULE separates the senders; the thresholds need the raw-data check on real devices
# (AGENTS.md 32) before they are called settled.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_device_probe.exe"
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/device_probe.cpp"
rc=0
"$OUT" || rc=$?
rm -f "$OUT"
exit $rc
